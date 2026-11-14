# Unicorn 内核功能审计（2026-11-16）

审计基线是实机已启动的第 18 代系统，Linux 7.3-rc6：
`/nix/store/zgpals2iq8mljxngfl2fy3wngr60glrg-nixos-system-unicorn-26.11.20261010.6510408`。
从手机 `/proc/config.gz` 读取实际配置，并核对该版本 Linux Kconfig、当前安装的
Waydroid 1.6.3、BlueZ 5.87 源码和 LXC 7.0.0 的检查结果。

## 本次补齐

| 配置 | 第 18 代 | 修改后 | 对应功能及证据 |
| --- | --- | --- | --- |
| `POSIX_MQUEUE` | 未启用 | `y` | uMTP-Responder 已实际报 `mq_open error 38`，对应内核缺少 POSIX 消息队列系统调用 |
| `POSIX_MQUEUE_SYSCTL` | 未启用 | 自动解析为 `y` | 队列限制的 sysctl；由 Linux Kconfig 在消息队列和 SYSCTL 启用时默认选择，检查最终配置 |
| `UHID` | 未启用 | `m` | 当前 BlueZ 的 `profiles/input/device.c` 默认 `UHID_ENABLED`，蓝牙键鼠和 HID-over-GATT 使用用户态 HID 通路 |
| `OVERLAY_FS` | 未启用 | 个人内核 `y` | 当前 Waydroid 默认 `mount_overlays=True`；`tools/helpers/images.py` 在缺少 OverlayFS 时会退回关闭 overlay，丢失该功能 |

设备通用能力在本仓库的 Unicorn 内核配置中维护；Waydroid 所需 OverlayFS
在个人配置 `unicorn-config/kernel-features.nix` 中维护，不改变其他设备默认配置。
MTP 模块增加消息队列配置断言，缺失时拒绝生成启用 MTP 的系统。

uMTP-Responder 还固定申请 20 条消息；新 IPC 命名空间的 Linux 默认上限为 10，
普通用户无 CAP_SYS_RESOURCE 时会报 EINVAL。软件包补丁将控制队列容量调整到 10，
保持原有 128 字节消息大小、权限和 PrivateIPC 隔离，不扩大宿主机全局限制或赋予特权。
探测尚未运行的 responder 时，队列不存在（ENOENT）不再作为错误打印；其他错误仍报告。
原始代码的 EINVAL 已复现，补丁后的实际 mq_open/send/receive/close/unlink 往返测试通过。

构建校验包括实际 Kconfig、ARM64 的 `ipc/mqueue.o` 和 `drivers/hid/uhid.o`，以及
原有 USB 驱动、DTB/schema 和角色回滚检查。内核编译通过不等于新内核已启动，
消息队列运行时、蓝牙外设和 Waydroid 仍需在新内核上验收。
MTP 停止超时是另一项已观察到的问题，本次配置修复不宣称解决它。

## 已具备的关键能力

- systemd/桌面：SYSVIPC、IPC/用户/PID/网络命名空间、seccomp/filter、cgroup、
  内存/进程数/IO 控制、inotify、epoll、memfd、FUSE、tmpfs ACL/xattr 均已启用。
- Waydroid/LXC：Binder、BinderFS、用户命名空间、loop、ext4、veth、bridge 已启用。
  当前实际安装的是 nftables 版 Waydroid，网络脚本明确 `LXC_USE_NFT=true`；
  nftables/NAT/conntrack 能力存在。容器会话在审计时为 STOPPED，未替用户初始化或启动 Android。
- Honk：BPF syscall/JIT、cgroup BPF、TCX、BTF、IPv4/IPv6 策略路由和 NFQUEUE 已启用。
- 手机设备：图形、输入、Wi-Fi、蓝牙、音频、TEE、USB 主机/设备等现有配置保留；
  VT/VT_CONSOLE 仍为内建。

## 当前关闭、按具体用途决定的能力

| 配置 | 当前状态与影响 | 本次处理 |
| --- | --- | --- |
| `INET_DIAG`、`INET_TCP_DIAG`、`INET_UDP_DIAG`、`UNIX_DIAG`、`PACKET_DIAG`、`NETLINK_DIAG` | 缺少完整 socket 诊断通路，影响 `ss` 等诊断工具的部分信息；不等于网络不能通信 | 记录为可选运维增强 |
| `FAIR_GROUP_SCHED`、`CFS_BANDWIDTH` | 未启用；组间公平 CPU 分配及 CPU 带宽配额能力受限 | 在需要 CPUWeight/CPUQuota、容器 CPU 限制时补齐；未在本次改调度行为 |
| `CHECKPOINT_RESTORE` | 未启用；不能据此宣称支持 CRIU 容器检查点/恢复 | 当前无此需求，保持关闭 |
| `MACVLAN`、`VLAN_8021Q` | 未启用；LXC 额外网卡模式/VLAN 不可用，当前 veth+bridge 模式已有支持 | 保持关闭 |
| 旧式 iptables/Xtables | 未启用；LXC 检查器会报告若干旧式 MASQUERADE/CHECKSUM/comment 缺项 | 当前 Waydroid 使用 nftables，无需为消除提示而启用另一套后端；其他 LXC 配置须单独核对 |
| `HIDRAW` | 未启用；需要 `/dev/hidraw` 的外设应用缺少接口 | 无已配置依赖，按外设需求启用 |
| `USB_VIDEO_CLASS` | 未启用；标准 UVC USB 摄像头驱动缺失 | 属于额外外设支持，保持关闭 |
| `ZRAM`/`ZSWAP`、`SND_SEQ`、`BINFMT_MISC`、KVM | 未启用；分别对应压缩交换、ALSA MIDI sequencer、额外可执行格式、硬件虚拟化 | 当前未配置相应服务，不作为必需缺项开启 |
| `BPF_LSM` | 未启用；虽启动参数 `lsm=landlock,yama,bpf` 包含 bpf，实际加载列表为 capability,landlock,yama | 不影响已启用的网络 BPF；没有已配置 BPF LSM 程序，不为参数字符串额外开启 |

LXC 的 `Cgroup cpu account: missing` 不能直接解释为没有 CPU 统计：实机使用
cgroup v2，`cpu.stat` 实际存在并有 usage/user/system 数据。应结合实际接口和所需
控制功能判断，不能仅凭检查器的一行结果批量开启选项。

## 启动参数和运行时设置

上述补齐均为编译选项，需要新内核；增加启动命令行或执行 sysctl 无法给旧内核补出
`mq_open` 实现。当前已有 `psi=1`，用户命名空间额度非零。审计时全局
`net.ipv4.ip_forward=1`，本次未改动；USB 模式识别和受限共享策略不得依赖把它归零。
没有发现当前启用功能还必须新增的启动命令行参数。

显示 self-refresh WARNING、前/主摄像头 ID NACK、MTP 停止超时仍是独立的驱动或
生命周期问题，没有证据表明再打开一组 Kconfig 开关就能解决。

## 构建结果

新系统候选已构建完成，尚未安装或重启：
`/nix/store/b27q56wxfhp1n0lmbxvkvrira8cam68s-nixos-system-unicorn-26.11.20261010.6510408`。
完整内核的最终配置与实际 Kconfig 检查输出逐字节相同，消息队列、UHID、OverlayFS
和 VT 设置均符合预期；候选中的 responder 已确认是 AArch64，保留 PrivateIPC。
模式配置的缺失消息队列负例、内核对象/DTB/schema、原生 responder 消息队列往返、
个人配置及完整系统构建均通过。具体产物、哈希和各检查清单见
[构建验证记录](kernel-config-audit-validation.json)。
