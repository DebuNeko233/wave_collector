# Hantek x64 DLL

`vendor/hantek/dll/x64/` 保存从本账号现有 [zhuru_wave](https://github.com/DebuNeko233/zhuru_wave) 归档工程同步的五个 DLL：

- `HTSoftDll.dll`、`HTHardDll.dll`：原厂硬件接口依赖；
- `HTDisplayDll.dll`、`MeasDll.dll`：原厂显示 / 测量组件；
- `libwave.dll`：原 `zhuru_wave` 的 C++ 封装，并非原厂 DLL。

`pnpm build:native` 会将 DLL 复制到 `resources/bin/`，使开发版与 Windows 打包版包含运行所需依赖。驱动仍需要单独安装。

**授权说明：** 这些二进制不因存放于本仓库而自动获得开源许可。原厂 DLL 的版权归其权利人所有，使用/再次分发仍需遵守厂商许可。本仓库尚未附有可以核实的原厂再分发条款；如无相应授权，应撤下公开 DLL 并改为用户本地安装。

**采集限制：** 当前 `legacy` 模式通过旧 `libwave.dll` 轮询获取采集块，不能证明块间时间轴无缝、无重复或无丢采样。要用于生产连续记录，需要用原厂 SDK 与实际设备完成验证。