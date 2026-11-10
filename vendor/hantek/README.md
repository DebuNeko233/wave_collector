# HT6004BX 原厂 SDK（x64）

本目录使用原厂 `HTHardDll` / `HTSoftDll` SDK 接口，**不使用自研 `libwave.dll`**。

| 目录 | 内容 |
|---|---|
| `include/` | 厂商 `DefMacro.h`、`HTHardDll.h`（为当前目录修改了相对 include 路径） |
| `lib/x64/` | `HTHardDll.lib`、`HTSoftDll.lib`，供 MSVC 直接链接 |
| `dll/x64/` | `HTHardDll.dll`、`HTSoftDll.dll` 与其他原厂运行库 |

源版本：HT6004BX SDK，镜像仓库 [`CircuitAnalysis/Hantek`](https://github.com/CircuitAnalysis/Hantek/tree/fbf09d36e125a3b57da96cd07c99aafc232ad90c/6XXX%20USB%20Scope/HT6004BX_SDK)，固定源码提交 `fbf09d36e125a3b57da96cd07c99aafc232ad90c`。原厂 SDK 下载地址为 `https://www.hantek.com/Product/Hantek6000BX/HT6004BX_SDK.zip`，请优先用设备随附或原厂确认的版本核对镜像内容。`.lib` 为 Windows DLL 导入库，不是算法静态实现。`pnpm build:native` 构建时链接 `.lib`，Windows 打包带入匹配的 `.dll`。

新添加的 `.github/workflows/sync-sdk.yml` 负责从固定版本镜像同步 SDK（包括匹配的 x64 `.lib` / `.dll`），并删除旧自研 `libwave.dll`。该操作不依赖旧 `zhuru_wave` 代码。

**许可**：SDK 二进制和头文件的再分发许可尚未经核实；该仓库已有文件并不构成获得再分发授权。投入生产或向外发布安装包前，应与厂家确认授权。硬件仍需要正确驱动，CI 模拟测试不代表实机连续采集已通过。