# HT6004BX SDK 与连续采集边界

采集后端 **不依赖 libwave.dll**。Windows/MSVC 使用官方 SDK 中的 `HTHardDll.lib`、`HTSoftDll.lib` 和对应 DLL，直接调用 `dsoHTSearchDevice`、`dsoHTDeviceConnect`、`dsoInitHard`、`dsoHTSetCHAndTrigger`、`dsoHTSetSampleRate`、`dsoHTStartCollectData`、`dsoHTGetState`、`dsoHTGetData`、`dsoGetSampleRate` 等函数。

## 采样档位

`dsoSFGetSampleRate(timebase, 0, channelMask, 0)` 依据原厂 SDK 返回时基/通道掩码对应的标称采样率，软件仅展示由它返回的有效档位（整数 Hz），开始录制后使用 `dsoGetSampleRate` 记录设备实际报告值。默认目标为接近 1 MSa/s 的档位，**不是**承诺 1 MSa/s 可持续经 USB 全量传输。

## 连续性

目前适配的是 SDK `YT_NORMAL` 模式：调用 `dsoHTStartCollectData`，等待 `dsoHTGetState` 的完成标志，再用 `dsoHTGetData` 获取一批 4096 点数据并进行下一次采集。这是重复触发的采集窗口，不提供跨窗口连续性保证。

原厂还提供 `dsoHTGetScanData`、`dsoHTGetRollData` 接口，但 SCAN/ROLL 的具体档位、数据游标和硬件支持范围必须通过实际设备验证，不能擅自用于 1 MSa/s 的无间断录制。因此元信息明确标记 `UNVERIFIED_vendor_normal_trigger_windows_may_have_gaps`，不能用软件生成的递增帧号当作硬件连续性证明。

## 电压

ADC 数据取值 0～255，按厂商示例的 `（原始码值-128）×每格电压/32` 转换，默认 500 mV/div、DC 耦合、垂直位置 128。软件显示/导出支持 V 与 mV，但这不代表经过计量校准。请实测探头倍率、档位、电路零偏和增益。

## 生产使用前

请在目标 Windows 11 26H2 电脑上，结合正确版本的原厂驱动、SDK 和 HT6004BX，验证所有可用时基/通道组合、设备断开/重连、长时间 USB 吞吐、采样段重复与间隙、磁盘写入、丢数据报警和电压标定。SDK 镜像与运行库版本不同时不得混用，详见 `vendor/hantek/README.md`。

## 手册档位 / 无设备状态

原厂 HT6004BX SDK 的时基表（Table 1/2）采用 2 ns/div～1000 s/div，常规时基档位按每格 250 个采样点计算，2 ns～500 ns 依据启用通道数限制单/双/四通道最高采样率。新版本界面和 C++ 统一使用这张固定档位表，拔掉 USB 仍可选择预设采样率。选项不代表设备已连接，真正开始采集才会搜索、配置设备并回读采样率。1 MSa/s 不是说明书的离散档位；相邻档位为 1.25 MSa/s 和 500 kSa/s。

源文件：`CircuitAnalysis/Hantek` 镜像中的 `6XXX USB Scope/HT6004BX_SDK/Manual/SDK_HTHardDLL-EN.pdf`，Table 1 和 Table 2；产品手册：<https://www.hantek.com/Product/Hantek6000BX/HT6004BX_Manual.pdf>。
