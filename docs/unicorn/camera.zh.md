# Unicorn 相机

[English](camera.md) · [设备指南](README.zh.md)

当前补丁队列包含四颗传感器的原生 V4L2 RAW10 实现。**源码接入不等于已完成实机出图验收**。
内核、设备树与 libcamera 已在 unicorn 原生构建通过，最新内核已安装并启动为默认的第 16 代系统。
首次实测超广角、长焦可枚举，长焦采到 3 帧约 30 fps RAW10；前摄、主摄仍在 ID 写地址阶段 NACK。
第二版已加入 GDSC 状态保持与 RCG 时钟停驻修复；超广角、长焦各通过 20 次采集启停和连续 300 帧 RAW10 检查。
前摄、主摄即使保持 CCI 父电源域开启也仍然 NACK；Bayer、曝光、软件 ISP 等完整验收尚未完成。

| 模组 | Linux I²C 地址与接收端 | 模式 |
| --- | --- | --- |
| S5KJN1 超广角 | CCI0/master0 `0x3d` → CSIPHY0 | 4080 × 3072 RAW10 |
| S5KJN1 长焦 | CCI1/master0 `0x2d` → CSIPHY1 | 4080 × 3072 RAW10 |
| OV32B40 前摄 | CCI1/master0 `0x10` → CSIPHY3 | 3264 × 2448 RAW10 |
| IMX707 主摄 | CCI0/master1 `0x10` → CSIPHY2，三组 C-PHY trio | 4096 × 3072 RAW10 |

`camera-bringup.patch` 在现有 `camera-sensors.patch` 之后应用，包含：

- 前摄对齐原厂 `0x300b` 的 16 位 ID 读取及上电延时；保留芯片识别，MCLK5／MCLK2 对齐原厂 4 mA 驱动强度，增加电源阶段和 CCI 写地址／读数据阶段日志。
- CAMSS 每绑定一颗传感器即建立媒体连接和注册子设备节点；失败回滚与解绑只清理对应传感器，避免一颗失败阻塞其他相机。
- 通用 PHY 的 C-PHY 配置、CAMSS 总线类型传递，以及 SM8475 实际使用的 CSID 680 接收模式选择。
- Unicorn 独立的 PHY compatible，采用原厂 `v2.1.3-xiaomi-l2s` 三组 trio 初始化、settle 计数 30 和 26 项有序 CDR 表；D-PHY 继续使用原有计算方式。
- IMX707 原厂版本 0 初始化表、模式 1、运行时电源管理、曝光、模拟增益和垂直消隐。接收端匹配 VC0/DT0x2b，过滤原厂 DT0x30 PDAF 辅助流。

四颗都使用 19.2 MHz MCLK，停止采集后断电，重新采集时再次识别 ID 并完整初始化。
libcamera 增益适配支持 IMX707 的 `1024 / (1024 - code)`；DMA heap 保留 `video` 组／`0660` 权限，并增加活动桌面会话的 `uaccess`。
自动对焦、OIS、HDR、全分辨率和画质调校留待后续。

## 构建与诊断

ARM 构建使用 unicorn，经 LAN 连接。在手机上的独立候选源码目录运行：

```sh
nix build .#devices.unicorn.kernel
nix build .#devices.unicorn.system
```

先使用可回退的测试启动项。用户已明确授权构建完成后设为默认启动；旧系统启动项保留用于回退。启动候选内核后：

```sh
cam --list
cam --camera 1 --info --list-properties
cam --camera 1 --capture=300
media-ctl -p -d /dev/media0
sudo journalctl -k -b --no-pager
```

编号以实际枚举为准。`/dev/video*` 仅代表 CAMSS 采集引擎，不能证明传感器工作。
前摄日志可通过 dynamic debug 打开；驱动报告的电压与 GPIO 状态仅为软件证据，不能替代模块端的电气测量。
不能跳过 ID 检查或把 NACK 直接归因于硬件损坏。

验收要求每颗 5 次启动探测、20 次采集启停、至少 300 帧连续采集，确认尺寸、时间戳、Bayer 排列、曝光响应、电源释放及 CSI 错误；软件 ISP 须显示实际场景，并回归超广角／长焦和单颗探测失败时的隔离行为。

原厂文件哈希、寄存器提取工具、时钟单位和实现细节见[英文记录](camera.md)。
实际构建和实机测试状态见[验证记录](camera-validation.md)。
