# Wave Collector

用于 [zhuru_wave](https://github.com/DebuNeko233/zhuru_wave) 的 Windows 波形记录器。前端 Electron / Vue / Element Plus，后端 MSVC C++，**直接链接 HT6004BX 厂商 x64 SDK**，不使用 `libwave.dll`。

## 主要功能

- CH1～CH4 自由选择、通道自定义名称。
- 按 SDK 支持档位选择采样率（接近 1 MSa/s 作为默认目标）；采集时显示实际设备速率。
- 实时波形预览（Min/Max 降采样），原生线程写盘、按时间或体积自动分段。
- V / mV 显示与导出；按起始帧、长度、通道筛选记录片段，导出 `zhuru-wave-v1` CSV。
- 无设备时使用模拟源完成采集、分段及导出测试。

## Windows 11 x64 构建

需要 Node.js 22、pnpm 10、CMake 与带 x64 MSVC/Windows SDK 的 Visual Studio C++ 构建工具。仓库中的厂商头文件与导入库位于 `vendor/hantek/`。

```powershell
git clone https://github.com/DebuNeko233/wave_collector.git
cd wave_collector
corepack enable
pnpm install
pnpm dev
pnpm test
pnpm build:unpack
pnpm build:win
```

`pnpm dev` 自动构建 C++ 后端。`pnpm build:native` 只编译原生模块；`pnpm build:unpack` 输出免安装版 `dist/win-unpacked`。

## 记录格式

原生 `.zwraw` 按已选通道交错保存小端 `float32` 伏特值（64 字节文件头），同时保存 `metadata.json`，数据分割器按窗口转换为 `zhuru-wave-v1` CSV。详见 [文件格式](docs/file-format.md)。

## 实机验证

HT6004BX 原厂 `YT_NORMAL` / `dsoHTGetData` 读取的是**重复触发窗口**；这**不证明**跨窗口无间断采样，尤其不保证持续 1 MSa/s。录制元信息标记 `UNVERIFIED_vendor_normal_trigger_windows_may_have_gaps`。若需要真实连续流式采样，需实机验证支持的 SCAN/ROLL 模式和硬件传输能力，不能以分段数据的递增索引冒充硬件连续样本。采样电压也尚未现场校准。

厂商 `.lib` 和 DLL 的来源、版本及再分发许可说明见 [SDK 文档](vendor/hantek/README.md)；公开分发厂商二进制前须确认授权。