# Y700 2023 内核补丁

[English](PATCHES.md) · [Y700 2023 指南](../../../../docs/asphalt/README.zh.md)

队列对应锁定的 Linux 7.3-rc6，补丁顺序见 [`series.nix`](series.nix)，
配置见 [`kernel.config`](kernel.config)。队列变更后须重新检查补丁应用、配置
及构建，实机验证另行进行。其他设备见[共享内核说明](../../README.zh.md)。

| 补丁 | 适配领域 |
| --- | --- |
| [SM8475 公共队列](../README.zh.md) | TLMM、SoC 基础定义、UBWC、PLL 交接、UFS 与 GCC CX 修复 |
| 0001 | Asphalt 控制器约束、CPUFreq 选择和 eUSB2 repeater |
| 0002 | 显示、面板与背光 |
| 0003 | 触摸屏 |
| [共享 PCIe 修复](../../patches/pcie-qcom-iommu-cells.patch) | 正确解析 PCIe BDF-to-SID 映射的 IOMMU 字段 |
| 0005 | 板级 UFS 时钟；850 MHz 的 GPLL3 父时钟修正由公共队列提供 |
| 0006 | Y700 2023 设备树 |
| 0007 | 视频编解码 |
| 0008 | USB 与音频连接 |
| 0009 | CS35L45 DSP 固件名称 |
| 0010 | QMP 硬件端口选择 |
| 0011 | 音频服务配置 |
| 0012 | SM8450 CAMSS |
| 0013 | 相机共享 RCG 时钟 |
| 0014 | WL2864C 稳压器 |
| 0015 | HI1339、S5K4H7 和 SC202CS 传感器 |
| 0016 | TB320FC 相机连接和微距 PHY lane 掩码 |
| 0017 | 相机 GDSC 寄存器保持 |
| 0018 | DisplayPort 连接、主 FSA4480 AUX 开关和 QMP DP 方向 |
| 0019 | DP debugfs connector 初始化 |
| `iris-hfi-gen2-{decode-order,capture-pool}.patch` | HFI gen2 解码顺序控制和更大的解码捕获缓冲池 |
| [共享 GMU 补丁组](../../patches/gmu-sync-state/README.md) | 为 GMU 绑定驱动，使 GCC/GPUCC `sync_state()` 完成 |
| 0026 | 禁用未使用的 QCE 节点（上游驱动标记为 BROKEN），使 interconnect `sync_state()` 完成 |
| `ufs-no-phy-retention.patch`（共享） | 系统挂起时关闭 SM8450/SM8475 UFS 控制器、设备和 PHY |
| `ufs-shared-rcg.patch`（共享） | 在父时钟关闭前切换 SM8475 UFS 根时钟至安全源，运行中更新时临时强制开启 |
| `ufs-qcom-hibern8-exit-error.patch`（共享） | 向上传递切频后 Hibern8 退出失败 |
| `gcc-cx-{binding,runtime-pm,domain}.patch`（共享） | 成组描述 GCC CX 依赖并启用 runtime PM |

在 x86_64 或 AArch64 Linux 主机上运行 `nix build .#devices.asphalt.kernel`。在 x86_64
主机上，derivation 由 x86_64 运行并生成 AArch64 内核。Kbuild 使用
`ARCH=arm64`、AArch64 target prefix，以及来自 `buildPackages` 的 Clang/LLD；
不需要 AArch64 主机版 LLVM 工具。Linux 内核补丁遵循上游 GPL-2.0 条款及被修改文件中的 SPDX 声明。

## 共享 PCIe 修复

Asphalt 与 Sheng 的 7.3 补丁集引用同一份上游 PCIe 解析补丁。
它替换 Asphalt 原本的本地 0004，保留完整原始提交说明与 trailer；代码层面的
唯一差异是恢复上游的警告文本。SDM845 使用另一组 PCIe 操作，不调用此 SID
解析函数，因此 Polaris 不选择该补丁。

## 相机来源

- 0012/0013 提取 Aaron Bockelie 的 SM8450 CAMSS 和 shared-RCG 内核补丁：
  [`d08afbea`](https://github.com/aaronsb/sm-x800-linux/commit/d08afbea7ba594092a104089aa5e124a91b06c7c)、
  [`04f99a72`](https://github.com/aaronsb/sm-x800-linux/commit/04f99a725bc63343683b54dea1aa8f698c4c3b13)。
  保留原作者、日期、提交说明及来源，仅提取打包提交中的相关内核差异。
- 0014 根据[厂商寄存器规格](https://www.ovt.com/wp-content/uploads/2022/09/OV-WL2864C-v2.pdf)
  实现 WL2864C 稳压器。
- 0015 提供 HI1339、S5K4H7、SC202CS 的全尺寸 RAW10 模式，使用 TB320FC
  的 24 MHz 配置。表中保留源文件哈希，补丁说明列出寄存器语义的 GPL 参考来源。
- 0016 配置实机供电、GPIO 和 CSI 连接，并保留单通道微距相机的 PHY 掩码；
  属于 0012 之后的本地适配。
- 0017 提取同作者 `04f99a72` 中的相机 GDSC 寄存器保留配置，与 TB320FC
  原厂设备树中全部九个 `qcom,retain-regs` 节点一致。

S5K4H7 控制寄存器语义也参考了 [MediaTek GPL 驱动（2019）](https://github.com/Samsung-MT6769-Devs/android_kernel_samsung_mt6768/blob/4e0fdc5e6f4c605813c569392f1c255ac4ef66ea/drivers/misc/mediatek/imgsensor/src/common/v1_1/s5k4h7_mipi_raw/s5k4h7mipiraw_Sensor.c)。

## 共享显示交接、UFS 与 GCC 电源管理

[SM8475 公共队列](../series.nix) 直接引用 Esteban Urrutia 的
[Lucid OLE PLL 修复](https://lore.kernel.org/all/20260713-sm8450-qol-dispcc-v3-3-56fd05822270@proton.me/)，
保留原始内容和归属，使引导程序已启用的 PLL 保持运行，供 MDP RCG 安全交接。
U-Boot 同时保留 `/reserved-memory/splash_region`；要求见
[引导固件说明](../../../u-boot/sm8475/README.md)。

Asphalt 使用共用 75/850 MHz OPP，联动 AXI/UniPro/ICE 及 CX/MX 性能请求。
原本已支持 850 MHz；本变更替换频率表及独立 ICE 时钟管理，保留控制器时序。
实现与验证边界见 [SM8475 说明](../README.zh.md)。

## Iris VA-API 前置支持

`iris-hfi-gen2-{decode-order,capture-pool}.patch` 原样导入 libva-v4l2 v1.0.0 中 Xilin Wu 的
[解码顺序控制](https://github.com/radxa-pkg/libva-v4l2/blob/0e4ce1e05da1d0a92b342efc2090b9e4987035e1/patches/0001-media-iris-support-decode-order-output-on-HFI-gen2.patch)和
[捕获缓冲池上限](https://github.com/radxa-pkg/libva-v4l2/blob/0e4ce1e05da1d0a92b342efc2090b9e4987035e1/patches/0002-media-iris-allow-larger-capture-buffer-pools-on-HFI-.patch)补丁，保留原作者、日期、
提交说明和签名。这些是 libva-v4l2 提供的配套补丁，不代表已被 Linux 上游合入。

同步 VA-API 客户端可能在提交后续输入之前等待当前画面完成。显示延迟控制
允许客户端按需启用 HFI gen2 解码顺序输出；未设置该控制的客户端保留固件默认
行为。第二项补丁允许 HFI gen2 解码器申请最多 64 个捕获缓冲区，不改变最小
分配数或编码器队列。具备这些接口本身不代表所有视频 profile 都已通过解码验证。

## 电池通知串行化

共用的[通知启用补丁](../../patches/qcom-battmgr-serialize-notification-request.patch)
让通知 worker 像属性读取一样使用既有请求互斥锁。两条路径共用 completion，
ADSP 启动时请求重叠可能唤醒错误的等待者。补丁保持 PDR 回调不阻塞，
不改变充电策略；超时后才到达的回复仍是另一项协议限制。
