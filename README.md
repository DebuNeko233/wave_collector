# Wave Collector

用于 [zhuru_wave](https://github.com/DebuNeko233/zhuru_wave) 的 Windows 波形记录器。前端 Electron / Vue / Element Plus，后端 MSVC C++，**直接链接 HT6004BX 厂商 x64 SDK**，不使用 `libwave.dll`。

## 主要功能

- CH1～CH4 自由选择、通道自定义名称。
- 按 SDK 支持档位选择采样率（接近 1 MSa/s 作为默认目标）；采集时显示实际设备速率。
- 大面积示波网格，支持多通道叠加 / 分轨 Min/Max 波形预览；原生线程写盘，按时间或体积自动分段。
- V / mV 显示与导出；按起始帧、长度、通道筛选记录片段，导出 `zhuru-wave-v1` CSV。
- 无设备时使用模拟源完成采集、分段及导出测试。

## 界面

以示波视图为中心：左侧窄导航用于切换采集 / 分割器，中部绘制多通道实时波形，右侧控制采集设备、通道与存储。采样状态读数保持在波形下方，事件记录默认收起。分割器独立页面，直接选择输入、范围、通道和输出格式。

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

## 采样率与 Electron 桥接

采样率选项按 HT6004BX SDK 时基表生成，但当前仅开放已实现的 NORMAL 模式（索引 0～23），不必先连接设备；1 MSa/s 附近的相邻档位为 1.25 MSa/s 和 500 kSa/s，默认取最近的 1.25 MSa/s。列表表示**标称硬件档位**，不是 USB 无缝流式吞吐保障。实际记录采样率以 `dsoGetSampleRate` 返回值为准。

Electron 37 + electron-vite 输出预加载文件为 `out/preload/index.mjs`。主进程必须使用相同文件名，否则 `window.acquisition` 未定义。界面在桥接失效时会展示明确错误，采样率列表仍可使用。
