# Wave Collector / 波形连续采集器

Windows x64 多通道波形采集与分割工具。独立于 [zhuru_wave](https://github.com/DebuNeko233/zhuru_wave)，采用 **Electron + Vue 3 + Element Plus** 界面和 **MSVC C++20** 原生采集进程。

> 当前为 **0.1.0 技术预览版**：模拟数据链路、原生分段记录、CSV 导出已通过本地 C++ 测试。**HT6004BX 旧 libwave.dll 采集路径是实验性的**，尚未经过设备连续性、时基、丢采样、通道使能和电压标定测试，不能作为已验证的工业连续采集方案。厂商 DLL 是否允许公开再分发仍需核对许可。

## 功能

界面遵循少说明、突出操作的布局原则：删除冗余副标题、宣传性介绍和四步使用指南，保留必要的设备安全提示。

- CH1～CH4 自由选择，通道名称可修改，录制元信息中保存名称。
- **设备给出采样率档位**：界面调用原生 `capabilities` 查询可选档位，通道组合或设备源变化时刷新；首选距离 1 MSa/s 最近的有效档位。采集后显示设备回读速率。
- 模拟设备；HT6004BX 旧 DLL 实验性适配。真实硬件不能保证跨次轮询数据无缝连续，需由原厂 SDK 的连续传输能力证明。
- 连续录制，按 **时长与文件大小** 两个阈值中先达到者分段；独立 C++ 写盘线程、有界队列和状态统计。
- 分通道 Canvas Min/Max 实时预览（**不是**全量波形数据搬运）。
- `V / mV` 显示单位与 CSV 导出单位可选；二进制格式目前保存 `float32` 伏特值；旧 DLL 的电压转换准确性需实测。
- **记录分割器**：选输入 `.zwraw` 文件或会话文件夹、起始帧/长度、通道和每份 CSV 点数；输出 [zhuru_wave](https://github.com/DebuNeko233/zhuru_wave) 的 `zhuru-wave-v1` CSV。双通道数据来自同一采集帧，不拼接独立记录。

## 环境

- Windows 11 x64（目标开发机器 Windows 11 26H2 / Core Ultra 9 275HX / 32 GB RAM）。
- Node.js 22、pnpm 10；Visual Studio 2022/2026 C++ 桌面开发工具、Windows SDK、CMake（可使用 VS 附带版本），均须在 `PATH` 中能找到。
- 真实硬件需正确的 HT6004BX 驱动及与硬件匹配的 x64 SDK/旧 DLL；详细见 `vendor/hantek/README.md`。

```powershell
corepack enable
pnpm install
pnpm dev           # 编译原生后端并启动 Electron 开发界面
pnpm test          # C++ 模拟采集 -> 分段记录 -> CSV 回归
pnpm build         # MSVC 原生编译、TS 类型检查、渲染进程构建
pnpm build:unpack  # 绿色版目录 dist/win-unpacked
pnpm build:win     # Windows 安装包
```

`pnpm build:native` 单独编译后端。Windows 下 CMake 默认使用机器上的 Visual Studio 生成器（如需指定可设置 `CMAKE_GENERATOR`）；对 MSVC 使用 x64。`vendor/hantek/dll/x64/` 的二进制由构建脚本复制到 `resources/bin/` 并随 Windows 软件打包。已安排从原 `zhuru_wave` 仓库同步 DLL，但原厂二进制的再分发许可仍须由仓库维护者确认，详见 `vendor/hantek/README.md`。

## 采样率档位

**不是硬编码“100 MHz / 1 MHz 等固定选项”**。界面会针对当前设备源查询可选档位。模拟源提供 1 MSa/s 及更低若干档；旧硬件源通过旧 DLL `LookupSampleRateHz` 函数获取时基表，并在设备被 `SearchDevice` 找到时显示。出于安全考虑，旧封装的通道开关行为未经验证，查询按**四通道表**保守处理。高采样率档位只是设备采样能力，**不意味着 USB 2.0 可以把同等速率连续送到磁盘**。

## 连续记录容量估算

原生存盘当前采用交错 `float32`，因此 1 MSa/s × 4 通道 ≈ 16 MB/s，约 57.6 GB/小时（十进制，未计文件头）。32 GB RAM 足够预览和缓冲，但长时间记录首先取决于 USB 设备连续吞吐、SDK 数据连续性和可用磁盘容量。任何缓冲溢出均应作为失败处理，而不是静默丢弃。

## 文件格式与兼容性

- 原生分段文件 `.zwraw`：64 字节头，后面按有效通道顺序交错排列的 little-endian `float32` 伏特数据，元数据另存 `metadata.json`。
- 导出 CSV 格式：第一行 `meta_key,meta_value,sample_index,X1,Y1...`；`format=zhuru-wave-v1`，`amplitude_unit=V/mV`，采样行索引从 0 连续。当前 `zhuru_wave` 李萨如导入器要求至少两个通道且不超过 250000 点及 25 MB，因此分割器默认每份 100000 点（上限 200000）。
- **注意：** 历史检测算法可能按 ADC 原始码值设置阈值，伏特值与码值不是同一个量，切勿不经转换直接沿用原阈值。

查看 [数据格式](docs/file-format.md) 和 [采集边界](docs/hardware-limitations.md)。

## 本地 C++ 回归验证

`pnpm test` 自动构建原生后端并执行模拟通道录制、分段、元信息与同步 CSV 回读测试。Linux 可用于 **C++ 模拟链路** 测试，不代表已经完成 Windows/Electron/实机编译与联调。

## 不包含的内容

本版本不做三角波异常识别，不做注入机设备联锁；这些仍由 `zhuru_wave` 独立承担。厂商连续采集接口、驱动、通道实际使能、MSVC ABI、零丢点时序和现场电压标定均需完整 SDK + 实机验证。