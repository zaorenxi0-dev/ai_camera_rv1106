import os
from rknn.api import RKNN

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
ONNX_PATH = os.path.join(PROJECT_DIR, 'best.onnx')
DATASET_PATH = os.path.join(PROJECT_DIR, 'calib_list.txt')
TARGET_PLATFORM = 'rv1106'
DO_QUANTIZATION = os.getenv('RKNN_QUANTIZE', '1') == '1'
OUTPUT_NAME = 'beverage_v1_int8.rknn'
OUTPUT_PATH = os.path.join(PROJECT_DIR, OUTPUT_NAME)


def inspect_onnx():
    try:
        import onnx
    except ImportError:
        print('⚠️ 未安装 onnx，跳过模型结构预检查')
        return

    try:
        model = onnx.load(ONNX_PATH)
    except Exception as exc:
        print(f'⚠️ 读取 ONNX 失败，跳过结构预检查: {exc}')
        return

    print('\n[预检查] ONNX 输入输出:')
    for tensor in model.graph.input:
        dims = []
        for dim in tensor.type.tensor_type.shape.dim:
            dims.append(dim.dim_value if dim.dim_value else (dim.dim_param or '?'))
        print(f'  input  {tensor.name}: {dims}')

    output_shapes = []
    for tensor in model.graph.output:
        dims = []
        for dim in tensor.type.tensor_type.shape.dim:
            dims.append(dim.dim_value if dim.dim_value else (dim.dim_param or '?'))
        output_shapes.append(dims)
        print(f'  output {tensor.name}: {dims}')

    if len(output_shapes) == 1 and output_shapes[0][:2] == [1, 25200]:
        print('⚠️ 当前 ONNX 只有一个扁平化输出，像是已包含 YOLOv5 decode 后处理。')
        print('   这类模型在 RKNN 编译阶段容易崩溃，RV1106 更建议导出 3 个检测头原始输出再到板端做后处理。')


def main():
    if not os.path.exists(ONNX_PATH):
        print(f'❌ 未找到 ONNX 模型: {ONNX_PATH}')
        return 1

    if not DO_QUANTIZATION:
        print(f'⚠️ {TARGET_PLATFORM} 不支持 do_quantization=False，已强制启用量化。')

    if not os.path.exists(DATASET_PATH):
        print(f'❌ 开启量化时未找到校准集列表: {DATASET_PATH}')
        return 1

    print('========== RKNN Convert Start ==========')
    print(f'模型路径: {ONNX_PATH}')
    print(f'目标平台: {TARGET_PLATFORM}')
    print(f'量化开关: True')
    print(f'输出文件: {OUTPUT_PATH}')
    inspect_onnx()

    # verbose=True 打印详细日志，方便定位 RKNN 编译阶段的问题
    rknn = RKNN(verbose=True)

    try:
        print('\n[1/4] 配置 RKNN...')
        rknn.config(
            mean_values=[[0, 0, 0]],
            std_values=[[255, 255, 255]],
            target_platform=TARGET_PLATFORM,
            optimization_level=0,
            quant_img_RGB2BGR=False,
        )

        print('[2/4] 加载 ONNX...')
        ret = rknn.load_onnx(model=ONNX_PATH)
        if ret != 0:
            print('❌ 加载 ONNX 失败')
            return ret
        print('✅ ONNX 加载成功')

        print('[3/4] 构建 RKNN...')
        ret = rknn.build(
            do_quantization=True,
            dataset=DATASET_PATH,
        )
        if ret != 0:
            print('❌ RKNN 构建失败')
            print('   已按 RV1106 要求使用 INT8 量化。')
            print('   若日志里出现 Squeezed bytes overflow/core dump，通常不是图片列表问题，而是 ONNX 检测头结构不适合 RKNN。')
            return ret
        print('✅ RKNN 构建成功')

        print('[4/4] 导出 RKNN...')
        ret = rknn.export_rknn(OUTPUT_PATH)
        if ret != 0:
            print('❌ 导出 RKNN 失败')
            return ret
        print(f'✅ 转换完成: {OUTPUT_PATH}')
        return 0
    finally:
        rknn.release()


if __name__ == '__main__':
    raise SystemExit(main())
