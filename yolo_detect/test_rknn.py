import os
from pathlib import Path

import cv2
import numpy as np
from rknn.api import RKNN


PROJECT_DIR = Path(__file__).resolve().parent
RKNN_PATH = PROJECT_DIR / 'beverage_v1_int8.rknn'
CALIB_DIR = PROJECT_DIR / 'calib'
TARGET_PLATFORM = 'rv1106'
DEVICE_ID = os.getenv('RKNN_DEVICE_ID')


def pick_test_image():
    image_paths = sorted(
        [
            path for path in CALIB_DIR.iterdir()
            if path.suffix.lower() in {'.jpg', '.jpeg', '.png', '.bmp'}
        ]
    )
    if not image_paths:
        raise FileNotFoundError(f'未在 {CALIB_DIR} 找到测试图片')
    return image_paths[0]


def preprocess_image(image_path):
    img = cv2.imread(str(image_path))
    if img is None:
        raise ValueError(f'读取图片失败: {image_path}')

    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img_resized = cv2.resize(img_rgb, (640, 640))
    return img, img_resized


def explain_runtime_error(exc):
    text = str(exc)
    print('\n❌ 初始化 RKNN 运行时失败')
    print(f'错误信息: {text}')

    if 'required target_platform = rv1103' in text or 'required target_platform = rv1106' in text:
        print('\n原因说明:')
        print('1. ADB 已经连上设备，但当前连接的板子平台与模型导出平台不一致。')
        print(f'2. 当前脚本和模型目标平台是: {TARGET_PLATFORM}')
        print('3. RKNN Runtime 检测到实际设备平台不是这个目标平台，所以拒绝加载运行。')
        print('\n处理建议:')
        print('1. 确认你连的是 RV1106 板子，而不是其它 Rockchip 设备')
        print('2. 如果你手上的板子实际是 RV1103，就需要重新按 RV1103 导出 `.rknn`')
        print('3. 一块板子只应匹配同平台导出的 `.rknn` 文件')
        return

    print('\n原因说明:')
    print('1. 当前脚本在测试 `.rknn` 文件，而 `.rknn` 不能直接在 PC 模拟器上推理。')
    print('2. 脚本指定了 target=rv1106，所以 RKNN 会尝试通过 USB/ADB 连接真实板子。')
    print('3. 只要板子没连上、ADB 没识别、或 Runtime 版本不匹配，init_runtime 就会失败。')
    if DEVICE_ID:
        print(f'4. 当前指定的设备 ID 为: {DEVICE_ID}')
    else:
        print('4. 当前未指定设备 ID，RKNN 会自动查找第一块板子。')

    print('\n你现在有两种正确用法:')
    print('1. 连上 RV1106 开发板后再运行这个脚本')
    print('2. 如果要在 PC 本地做模拟验证，不能用 load_rknn，需要重新走 load_onnx + build + init_runtime()')


def main():
    if not RKNN_PATH.exists():
        print(f'❌ 未找到 RKNN 模型: {RKNN_PATH}')
        return 1

    try:
        image_path = pick_test_image()
    except Exception as exc:
        print(f'❌ 选择测试图片失败: {exc}')
        return 1

    print('========== RKNN Test Start ==========')
    print(f'模型路径: {RKNN_PATH}')
    print(f'目标平台: {TARGET_PLATFORM}')
    print(f'设备 ID: {DEVICE_ID if DEVICE_ID else "未指定，自动查找"}')
    print(f'测试图片: {image_path}')

    rknn = RKNN(verbose=False)

    try:
        ret = rknn.load_rknn(str(RKNN_PATH))
        if ret != 0:
            print('❌ 加载 RKNN 模型失败')
            return ret
        print('✅ RKNN 模型加载成功')

        print('\n[1/3] 初始化运行时...')
        init_kwargs = {'target': TARGET_PLATFORM}
        if DEVICE_ID:
            init_kwargs['device_id'] = DEVICE_ID
        ret = rknn.init_runtime(**init_kwargs)
        if ret != 0:
            print('❌ init_runtime 返回失败')
            print('   提示: `.rknn` 文件不能直接在 PC 模拟器上推理，必须连接 RV1106 板子。')
            return ret
        print('✅ RV1106 运行时初始化成功')

        print('[2/3] 预处理测试图片...')
        img, img_resized = preprocess_image(image_path)
        print(f'原始尺寸: {img.shape}, 输入尺寸: {img_resized.shape}, dtype={img_resized.dtype}')

        print('[3/3] 执行推理...')
        outputs = rknn.inference(inputs=[img_resized])
        if outputs is None:
            print('❌ 推理没有返回结果')
            return 1

        print(f'\n输出层数: {len(outputs)}')
        for i, out in enumerate(outputs):
            out_np = np.asarray(out)
            print(
                f'  输出 {i}: shape={out_np.shape}, dtype={out_np.dtype}, '
                f'min={out_np.min():.4f}, max={out_np.max():.4f}, mean={out_np.mean():.4f}'
            )

        expected_shape = (1, 25200, 10)
        first_output = np.asarray(outputs[0])
        if tuple(first_output.shape) == expected_shape:
            print(f'\n✅ 输出形状符合当前模型预期: {expected_shape}')
        else:
            print(f'\n⚠️ 输出形状与当前预期不一致: 期望 {expected_shape}, 实际 {first_output.shape}')

        if np.any(first_output != 0):
            print('✅ 输出存在非零值，模型已实际跑通')
        else:
            print('⚠️ 输出全零，建议继续检查量化质量和板端前处理')

        print('\n🎉 RKNN 模型测试完成')
        return 0

    except Exception as exc:
        explain_runtime_error(exc)
        return 1
    finally:
        rknn.release()


if __name__ == '__main__':
    raise SystemExit(main())
