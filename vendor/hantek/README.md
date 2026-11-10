# 本地厂商 SDK 文件（请勿提交到公开仓库）

如果已取得 HT6004BX 厂商的合法 SDK，可在 `vendor/hantek/dll/x64/` 放置：

- `HTSoftDll.dll`
- `HTHardDll.dll`
- `libwave.dll`（**旧 zhuru_wave 项目自研封装**，并非原厂 SDK 内容）

根据实际依赖可能还要放置 `HTDisplayDll.dll`、`MeasDll.dll`。`pnpm build:native` 会把本地 DLL 拷贝到 `resources/bin/`。

**限制：** 当前 `legacy` 源使用旧 `libwave.dll` 的重复采集轮询，无法证明采样时间轴连续无缝，不能用于要求无丢点的生产记录；完整 SDK 直接读数的独立适配器仍待现场硬件联调。
