# HT6004BX 采集边界

原项目 `zhuru_wave` 中有 HT6004BX SDK 的历史资料描述、`libwave.dll` 接口和代码。然而自研 `libwave` 的版本/ABI 与仓库归档代码有差异。当前适配层针对既有 **DLL 导出的接口**，不以 `native_cpp/libwave` 中旧源码作为可直接替换的二进制。

当前 `legacy` 模式使用：

- `SearchDevice` 搜索设备；`LookupSampleRateHz(timebase,4)` 读取保守四通道采样档位；
- `InitDevice` 配置时基；`GetSampleRate` 回读采样率；
- `GetAllChannelData` 每次抓取最多 10000 点，循环送往 C++ 缓冲与存盘线程。

**重要：** 每次 `GetAllChannelData` 的数据可能是一次新的触发/采集窗口。旧接口未提供可用于证明连续采样的硬件样本序号、时间戳和 DMA/streaming 队列。即使软件每块帧索引递增，仍不能证明跨块真实采集时间连续。当前报告 `UNVERIFIED_legacy_poll_may_have_gaps_or_repeats`，不得据此进行无丢点生产追溯。要达到连续记录要求，需要核查原厂 SDK 是否支持 USB streaming/bulk transfer 和连续缓冲。若不支持，应调整为触发分段记录而非虚构连续信号。

此外：旧版 `libwave` 的伏特换算可能是简化校准，未实测通道增益、探头倍率和量程。`V` / `mV` 为当前软件值域转换，不代表计量认证电压。采样率表显示设备档位，不代表 USB 2.0 持续传输能力。请在 Windows 11 26H2 目标主机、正确驱动与采集设备上做长期稳定性/丢块/USB 断连/磁盘压力测试。
