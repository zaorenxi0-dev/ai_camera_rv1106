#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <algorithm>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"
#include "ipc_dbus_server.h"
#include "yolo_config.h"

// uClibc 版 OpenCV 不含 imgcodecs，用 stb 单头库替代 imread/imwrite
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ========== 模型配置（根据你的模型修改） ==========
#define MODEL_PATH YOLO_MODEL_PATH
#define INPUT_W 640
#define INPUT_H 640
#define CLASS_NUM 5               // 检测类别数
#define PROP_BOX_SIZE (5 + CLASS_NUM) // x,y,w,h,obj + 各类别置信度
#define CONF_THRESH 0.5           // 置信度阈值
#define NMS_THRESH 0.45           // NMS 阈值

// D-Bus 服务三元组（占位值见 yolo_config.h，部署前改回实际名称）
#define DBUS_BUS_NAME  YOLO_DBUS_BUS_NAME
#define DBUS_PATH      YOLO_DBUS_PATH
#define DBUS_INTERFACE YOLO_DBUS_INTERFACE

// 检测框结构体
typedef struct {
    float x1, y1, x2, y2; // 左上角 + 右下角坐标
    float conf;           // 置信度
    int class_id;         // 类别 ID
} DetectBox;

// 标准 YOLOv5 三个检测头的 anchor（按 stride 8/16/32 排列）
static const int anchors[3][6] = {
    {10, 13, 16, 30, 33, 23},
    {30, 61, 62, 45, 59, 119},
    {116, 90, 156, 198, 373, 326},
};

// ========== 全局模型状态（服务端常驻，加载一次） ==========
static rknn_context        g_ctx        = 0;
static rknn_tensor_mem *   g_input_mem  = NULL;
static rknn_tensor_attr *  g_output_attrs = NULL;
static rknn_tensor_mem **  g_output_mems  = NULL;
static uint32_t            g_n_output   = 0;
static unsigned char *     g_model_data = NULL;

// INT8 反量化：f32 = (qnt - zp) * scale
static inline float deqnt(int8_t q, int32_t zp, float scale) {
    return ((float)q - (float)zp) * scale;
}

static long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// letterbox 等比例缩放 + 黑边填充 + BGR 转 RGB（输出 uint8）
static void preprocess(const cv::Mat &src, cv::Mat &dst, float &scale, int &pad_x, int &pad_y) {
    int src_w = src.cols, src_h = src.rows;

    scale = std::min((float)INPUT_W / src_w, (float)INPUT_H / src_h);
    int new_w = (int)(src_w * scale);
    int new_h = (int)(src_h * scale);
    pad_x = (INPUT_W - new_w) / 2;
    pad_y = (INPUT_H - new_h) / 2;

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));
    cv::Mat padded(INPUT_H, INPUT_W, CV_8UC3, cv::Scalar(0, 0, 0));
    resized.copyTo(padded(cv::Rect(pad_x, pad_y, new_w, new_h)));

    // 量化时 quant_img_RGB2BGR=False，模型输入是 RGB
    cv::cvtColor(padded, dst, cv::COLOR_BGR2RGB);
}

// 单个检测头解码：INT8 反量化 + anchor 解码
static int process_i8(const int8_t *input, const int *anchor, int grid_h, int grid_w, int stride,
                      int32_t zp, float scale, float conf_thresh, std::vector<DetectBox> &boxes) {
    int valid = 0;
    const int align_c = PROP_BOX_SIZE * 3; // 每个网格点连续 30 个值

    for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
            for (int a = 0; a < 3; a++) {
                const int8_t *p = input + h * grid_w * align_c + w * align_c + a * PROP_BOX_SIZE;

                float obj_conf = deqnt(p[4], zp, scale);
                if (obj_conf < conf_thresh) continue;

                int max_cls = 0;
                float max_prob = deqnt(p[5], zp, scale);
                for (int k = 1; k < CLASS_NUM; k++) {
                    float prob = deqnt(p[5 + k], zp, scale);
                    if (prob > max_prob) {
                        max_prob = prob;
                        max_cls = k;
                    }
                }
                float score = obj_conf * max_prob;
                if (score < conf_thresh) continue;

                float bx = deqnt(p[0], zp, scale) * 2.0f - 0.5f;
                float by = deqnt(p[1], zp, scale) * 2.0f - 0.5f;
                float bw = deqnt(p[2], zp, scale) * 2.0f;
                float bh = deqnt(p[3], zp, scale) * 2.0f;
                bw = bw * bw;
                bh = bh * bh;

                bx = (bx + w) * (float)stride;
                by = (by + h) * (float)stride;
                bw *= (float)anchor[a * 2];
                bh *= (float)anchor[a * 2 + 1];

                DetectBox box;
                box.x1 = bx - bw / 2.0f;
                box.y1 = by - bh / 2.0f;
                box.x2 = bx + bw / 2.0f;
                box.y2 = by + bh / 2.0f;
                box.conf = score;
                box.class_id = max_cls;
                boxes.push_back(box);
                valid++;
            }
        }
    }
    return valid;
}

// NMS 非极大值抑制（按类别分组）
static void nms(std::vector<DetectBox> &boxes, std::vector<DetectBox> &result) {
    std::sort(boxes.begin(), boxes.end(),
              [](const DetectBox &a, const DetectBox &b) { return a.conf > b.conf; });

    std::vector<int> suppressed(boxes.size(), 0);
    for (size_t i = 0; i < boxes.size(); i++) {
        if (suppressed[i]) continue;
        result.push_back(boxes[i]);

        for (size_t j = i + 1; j < boxes.size(); j++) {
            if (suppressed[j]) continue;
            if (boxes[i].class_id != boxes[j].class_id) continue;

            float x1 = std::max(boxes[i].x1, boxes[j].x1);
            float y1 = std::max(boxes[i].y1, boxes[j].y1);
            float x2 = std::min(boxes[i].x2, boxes[j].x2);
            float y2 = std::min(boxes[i].y2, boxes[j].y2);
            float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);

            float area_i = (boxes[i].x2 - boxes[i].x1) * (boxes[i].y2 - boxes[i].y1);
            float area_j = (boxes[j].x2 - boxes[j].x1) * (boxes[j].y2 - boxes[j].y1);
            float iou = inter / (area_i + area_j - inter);

            if (iou > NMS_THRESH) suppressed[j] = 1;
        }
    }
}

// ========== 模型初始化 / 释放 ==========
static int init_model(const char *model_path) {
    FILE *fp = fopen(model_path, "rb");
    if (!fp) {
        printf("无法打开模型文件: %s\n", model_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long model_size = ftell(fp);
    rewind(fp);
    g_model_data = (unsigned char *)malloc(model_size);
    if (!g_model_data) {
        fclose(fp);
        printf("模型内存分配失败\n");
        return -1;
    }
    if (fread(g_model_data, 1, model_size, fp) != (size_t)model_size) {
        fclose(fp);
        printf("读取模型文件失败\n");
        return -1;
    }
    fclose(fp);

    int ret = rknn_init(&g_ctx, g_model_data, model_size, 0, NULL);
    if (ret < 0) {
        printf("模型加载失败，错误码: %d\n", ret);
        return -1;
    }
    printf("模型加载成功: %s\n", model_path);

    rknn_input_output_num io_num;
    ret = rknn_query(g_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        printf("查询输入输出数量失败: %d\n", ret);
        return -1;
    }
    printf("模型输入数: %d, 输出数: %d\n", io_num.n_input, io_num.n_output);

    // 零拷贝设置输入
    rknn_tensor_attr input_attr;
    memset(&input_attr, 0, sizeof(input_attr));
    ret = rknn_query(g_ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &input_attr, sizeof(input_attr));
    if (ret < 0) {
        printf("查询原生输入属性失败: %d\n", ret);
        return -1;
    }
    input_attr.type = RKNN_TENSOR_UINT8;
    input_attr.fmt = RKNN_TENSOR_NHWC;

    g_input_mem = rknn_create_mem(g_ctx, input_attr.size_with_stride);
    if (!g_input_mem) {
        printf("创建输入内存失败\n");
        return -1;
    }
    ret = rknn_set_io_mem(g_ctx, g_input_mem, &input_attr);
    if (ret < 0) {
        printf("设置输入内存失败: %d\n", ret);
        return -1;
    }

    // 零拷贝设置输出
    g_n_output = io_num.n_output;
    g_output_attrs = (rknn_tensor_attr *)calloc(g_n_output, sizeof(rknn_tensor_attr));
    g_output_mems = (rknn_tensor_mem **)calloc(g_n_output, sizeof(rknn_tensor_mem *));
    for (uint32_t i = 0; i < g_n_output; i++) {
        g_output_attrs[i].index = i;
        ret = rknn_query(g_ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &g_output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("查询原生输出属性失败[%d]: %d\n", i, ret);
            return -1;
        }
        printf("输出[%d]: dims=[%d,%d,%d,%d], zp=%d, scale=%f\n",
               i, g_output_attrs[i].dims[0], g_output_attrs[i].dims[1], g_output_attrs[i].dims[2],
               g_output_attrs[i].dims[3], g_output_attrs[i].zp, g_output_attrs[i].scale);

        g_output_mems[i] = rknn_create_mem(g_ctx, g_output_attrs[i].size_with_stride);
        if (!g_output_mems[i]) {
            printf("创建输出内存失败[%d]\n", i);
            return -1;
        }
        ret = rknn_set_io_mem(g_ctx, g_output_mems[i], &g_output_attrs[i]);
        if (ret < 0) {
            printf("设置输出内存失败[%d]: %d\n", i, ret);
            return -1;
        }
    }
    return 0;
}

static void deinit_model(void) {
    for (uint32_t i = 0; i < g_n_output; i++) {
        if (g_output_mems && g_output_mems[i]) rknn_destroy_mem(g_ctx, g_output_mems[i]);
    }
    if (g_input_mem) rknn_destroy_mem(g_ctx, g_input_mem);
    if (g_output_attrs) free(g_output_attrs);
    if (g_output_mems) free(g_output_mems);
    if (g_model_data) free(g_model_data);
    if (g_ctx) rknn_destroy(g_ctx);
    g_input_mem = NULL;
    g_output_attrs = NULL;
    g_output_mems = NULL;
    g_model_data = NULL;
    g_ctx = 0;
    g_n_output = 0;
}

// ========== 识别一张图 ==========
// 返回检测框数量，失败返回 -1
static int run_detect(const char *image_path, const char *output_path) {
    int w0 = 0, h0 = 0, c0 = 0;
    unsigned char *img_data = stbi_load(image_path, &w0, &h0, &c0, 3); // 强制 3 通道 RGB
    if (!img_data) {
        printf("图片读取失败: %s\n", image_path);
        return -1;
    }
    cv::Mat img_rgb(h0, w0, CV_8UC3, img_data);
    cv::Mat img;
    cv::cvtColor(img_rgb, img, cv::COLOR_RGB2BGR);
    stbi_image_free(img_data);

    cv::Mat input_rgb;
    float scale;
    int pad_x, pad_y;
    preprocess(img, input_rgb, scale, pad_x, pad_y);
    memcpy(g_input_mem->virt_addr, input_rgb.data, INPUT_W * INPUT_H * 3);

    long t_start = get_time_us();
    int ret = rknn_run(g_ctx, NULL);
    long t_end = get_time_us();
    if (ret < 0) {
        printf("推理失败: %d\n", ret);
        return -1;
    }
    printf("推理耗时: %.2f ms\n", (t_end - t_start) / 1000.0f);

    std::vector<DetectBox> all_boxes;
    for (uint32_t i = 0; i < g_n_output; i++) {
        int grid_h = g_output_attrs[i].dims[2];
        int grid_w = g_output_attrs[i].dims[1];
        int stride = INPUT_H / grid_h;
        process_i8((const int8_t *)g_output_mems[i]->virt_addr, anchors[i], grid_h, grid_w, stride,
                   g_output_attrs[i].zp, g_output_attrs[i].scale, CONF_THRESH, all_boxes);
    }

    std::vector<DetectBox> nms_boxes;
    nms(all_boxes, nms_boxes);

    for (auto &box : nms_boxes) {
        box.x1 = std::max(0.0f, std::min((float)img.cols - 1.0f, (box.x1 - pad_x) / scale));
        box.y1 = std::max(0.0f, std::min((float)img.rows - 1.0f, (box.y1 - pad_y) / scale));
        box.x2 = std::max(0.0f, std::min((float)img.cols - 1.0f, (box.x2 - pad_x) / scale));
        box.y2 = std::max(0.0f, std::min((float)img.rows - 1.0f, (box.y2 - pad_y) / scale));
    }
    printf("检测到 %d 个目标\n", (int)nms_boxes.size());

    for (auto &box : nms_boxes) {
        cv::rectangle(img, cv::Point((int)box.x1, (int)box.y1), cv::Point((int)box.x2, (int)box.y2),
                      cv::Scalar(0, 255, 0), 2);
        char text[32];
        snprintf(text, sizeof(text), "cls%d:%.2f", box.class_id, box.conf);
        cv::putText(img, text, cv::Point((int)box.x1, std::max(0, (int)box.y1 - 10)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
    }

    cv::Mat img_bgr_out;
    cv::cvtColor(img, img_bgr_out, cv::COLOR_BGR2RGB);
    if (!stbi_write_jpg(output_path, img_bgr_out.cols, img_bgr_out.rows, 3, img_bgr_out.data, 95)) {
        printf("图片保存失败: %s\n", output_path);
        return -1;
    }
    printf("结果已保存为 %s\n", output_path);
    return (int)nms_boxes.size();
}

// ========== D-Bus 方法处理函数 ==========
// 输入：图片路径字符串；输出：结果字符串（检测数量 + 结果图路径）
static char *handle_detect(const char *arg) {
    if (!arg || !arg[0]) {
        return strdup("ERR empty path");
    }
    const char *img_path = arg;
    char out_path[1024];
    snprintf(out_path, sizeof(out_path), "%s.detect.jpg", img_path);

    int n = run_detect(img_path, out_path);
    if (n < 0) {
        return strdup("ERR detect failed");
    }

    char buf[1200];
    snprintf(buf, sizeof(buf), "%d %s", n, out_path);
    return strdup(buf);
}

// ========== 服务端模式 ==========
static int run_server(const char *model_path) {
    if (init_model(model_path) < 0) {
        return -1;
    }

    ipc_method_entry methods[] = {
        {"Detect", handle_detect},
    };

    int ret = ipc_dbus_server_start(methods, 1, DBUS_BUS_NAME, DBUS_PATH, DBUS_INTERFACE);
    deinit_model();
    return ret;
}

// ========== 单次模式（命令行直接识别，便于调试） ==========
static int run_once(const char *img_path, const char *model_path, const char *output_path) {
    if (init_model(model_path) < 0) {
        return -1;
    }
    int n = run_detect(img_path, output_path);
    deinit_model();
    return n < 0 ? -1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // 服务端模式：./rknn_yolo [--server] [模型路径]
    if (argc == 1) {
        return run_server(MODEL_PATH);
    }
    if (strcmp(argv[1], "--server") == 0) {
        const char *model = (argc >= 3) ? argv[2] : MODEL_PATH;
        return run_server(model);
    }

    // 单个参数且是 .rknn 模型文件 -> 当作服务端启动
    if (argc == 2) {
        size_t len = strlen(argv[1]);
        if (len > 5 && strcmp(argv[1] + len - 5, ".rknn") == 0) {
            return run_server(argv[1]);
        }
    }

    // 单次模式：./rknn_yolo <图片路径> [模型路径] [输出图片]
    const char *img_path = argv[1];
    const char *model_path = (argc >= 3) ? argv[2] : MODEL_PATH;
    const char *output_path = (argc >= 4) ? argv[3] : "result.jpg";
    return run_once(img_path, model_path, output_path);
}
