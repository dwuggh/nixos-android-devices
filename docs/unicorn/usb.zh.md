# Unicorn USB2 实现和验收

默认模式仍为 NCM，个人配置同时启用 ACM。MTP 仅导出
`/home/dwuggh/USB-Share`，由 `dwuggh` 运行；网络共享通过 networkd、专用 DNS
和 nftables 实现。源码在本仓库，个人启用选项在相邻 `unicorn-config/phone.nix`。
没有修改 Honk 的配置或共享流量代理策略。

新增的 HIDRAW、原生多点触摸、可选镜像 U 盘和 UVC 模式见
[USB 扩展模式](usb-gadgets.zh.md)。这些代码及个人配置改动尚未部署。

## 可追溯的硬件通路

来源：

- [OEM 公共设备树](https://github.com/LineageOS/android_kernel_xiaomi_sm8450-devicetrees/blob/2e649a7f3e762903eedad43d8256b91e28b512d8/qcom/xiaomi-sm8475-common.dtsi)，尤其 `&usb0`、`&ucsi`、`&nxp_eusb2_repeater`。
- [公开 Unicorn 原厂展开树](https://github.com/sm8450-mainline/fdt/blob/3e347601d9e91dd2f1f48e51499823225f7bc0bc/unicorn.dts)，尤其 `ssusb@a600000`、`hsphy@88e3000`、`i2c@994000` 和 `ssphy@88e8000`。该展开树有重复标签，适合核对属性，不能直接作为可编译主线 DTS。
- 仓库的 `platform.patch`、`board.patch`、`eusb2.patch`、`usb-role-switch.patch` 是主线格式的实现。连接器图经当前 Linux 7.3-rc6 schema 和实际 DTB 检查。

| 部件/功能 | 原厂证据 | 当前实现与验证状态 |
| --- | --- | --- |
| DWC3 | `0x0a600000`，`dr_mode=otg`，`maximum-speed=high-speed` | 双角色、USB2；端点双向连接 PMIC GLINK connector@0。驱动对象和 DTB 通过，新的实机 OTG 验收待做 |
| eUSB2 PHY | `0x088e3000`，L5B 0.88V、L10C 1.2V | 主线 Synopsys eUSB2，保留对应供电，不从其他板推导供电 |
| PHY 时钟/复位 | XO 参考源、GCC EUSB3_0_CLKREF_EN 门、QUSB2PHY_PRIM_BCR | 两路时钟和复位已配置，schema/对象通过 |
| 外部中继 | I²C `0x00994000`、地址 `0x4f`，S10B 1.8V、L2B 约 3V，PM8350C GPIO7 reset | PTN3222 兼容驱动；compatible 是迁移匹配，并非本轮重新确认芯片丝印。主线 reset active-low 与逻辑 assert/deassert 配套 |
| 中继信号参数 | Unicorn 覆盖 device: `0x62→0x08, 0x00→0x07`；host: `0x63→0x08, 0x22→0x07` | 保持原厂写入顺序；I²C 读写失败回滚寄存器，失败不提交新模式；断电重初始化重放模式。故障注入通过 |
| CC/PD/数据角色 | OEM USB ↔ UCSI connector 图 | UCSI/ADSP 协商，DWC3 跟随；启动和热插拔不请求角色交换。显式请求写 Type-C data_role 并确认 Type-C 和 DWC3 一致 |
| VBUS/充电 | 由原厂 PMIC GLINK/充电固件处理 | 没有新增猜测的 VBUS regulator 或直接供电命令。power_role 单独报告；`vbus_verified=false`，实测电压、电流及带载能力仍待验收 |
| USB HID/Storage/UAS/网卡/数字音频 | 标准 USB2 外设协议 | 实际 Kconfig 和对象检查通过；键鼠、U 盘、网卡、音频、带电 Hub 的实物测试待做 |
| USB3 | 原厂 maximum-speed 为 high-speed；QMP USB/DP PHY disabled | 当前仅提供 USB2；没有足够板级证据启用 USB3 通路 |
| 原生 DP | 公共 SoC 树包含 DP/QMP/FSA 描述，不等于板上完整链路 | 未验证；不添加虚构的 PHY、mux 或供电，不把 USB2 当成视频输出 |
| 模拟 Type-C 音频 | 原厂 `qcom,fsa4480-i2c` @ `0x42` 和相关引用 | 只有器件/引用证据；板级路由、供电、切换协议尚未完整核实，不启用 |

2026-11-15 本轮仅通过 LAN 对运行中的旧系统做只读观测：内核 `7.3.0-rc6`，
Type-C 为 host/sink，DWC3 为 device，UDC 为 not attached，usb-signaller active。
这证明旧系统存在角色不一致，不能作为新实现的通过记录。新实现会报告该错误，
不会用强制写 DWC3 来掩盖协商状态。

## 生命周期和接口

所有启动、模式请求、Type-C 事件、固件代次变化和退出操作由一个有界队列、单线程
状态机串行处理。重复进程被运行目录的文件锁拒绝。D-Bus 查询读取缓存状态，不等
sysfs 或外部服务；`set_mode` 返回的是**请求已接受**，应继续检查 `get_status` 中的
`busy`、`desired_mode`、`actual_mode` 和 `last_error`。队列已满会立即返回错误。

切换顺序为停止后端/console → 解绑 → 卸载 FunctionFS → 删除本控制器拥有的 gadget →
请求必要的数据角色 → 创建/准备后端 → 确认连接未变 → 绑定。创建、端点就绪、服务
启动或绑定失败时清理半成品；同一连接、同一固件代次仍允许 device 时重建前一模式。
断线或固件离线不强行回滚角色，最近错误保持可查询；仅新请求或连接/固件变化重试，
不会无限重启。切换期间发生的新连接事件保留到下一轮处理。

只管理六个精确名称：`usb-signaller-developer`、`usb-signaller-mtp`、
`usb-signaller-tethering`、`usb-signaller-touch`、`usb-signaller-storage` 和
`usb-signaller-uvc`。不删除 initrd/recovery 或其他服务的 gadget，不抢占其 UDC。
正常 initrd 在 switch-root 前解绑。Unicorn 的 initrd 不覆盖已连接 partner 的角色。
守护进程即使异常退出，systemd 的 ExecStopPost 也会执行同一清理入口。

`bound=true` 只表示 gadget 已绑定；`enumerated=true` 必须观察到 UDC configured。
无插线时绑定是有效状态，不等待电脑枚举，不阻塞事件处理。

`host_mode` 仅请求数据角色，不改变供电角色；host/sink 是合法组合，不能宣称能给
无源外设供电。拔线后退出显式 host 策略，恢复默认策略。`charging_only` 解绑 gadget，
保留 Type-C/充电协商，不切换 power_role。小米认证默认不改角色，可选发现也只通过
D-Bus 提交给这个状态机；认证标志不代替充电功率测量。

MTP 使用仓库打包的 uMTP-Responder，仅在 MTP 模式挂载 FunctionFS、运行 responder，
等 `functions/ffs.mtp/ready=1` 后绑定。systemd 隐藏 home，只绑定明确列出的共享目录。
MTP 不发布系统磁盘；其他可选设备模式各自独立，见上述扩展模式文档。

networkd 和 NetworkManager 通过本 gadget 的 MAC 身份区分 usb0，避免误接管主机模式下的 USB 网卡。
共享模式保留 networkd 对 gadget usb0 的所有权；先准备并加载 DHCP drop-in，再绑定 UDC，
确认 NCM 网卡的 MAC 后才启用接口转发和上游集合。临时 DHCP drop-in 发布路由和 DNS，专属
DNS 仅监听 usb0。上游按 main 表的 IPv4 默认路由和 metric 选取，排除 usb0/lo 和
不能安全确定的多路径。仅允许该上游、USB 子网源地址和公网目的地址，拒绝额外
内网转发、IPv6 转发及未建立的反向连接。使用接口级 forwarding，保存并恢复原值，
不写全局 ip_forward。退出和异常终止会清空上游集合、删除 DHCP drop-in、恢复接口
转发值；即使其他服务开启全局转发，空集合也会阻止 USB 转发。切换后电脑需重新
获取 DHCP 租约。复杂策略路由、代理和 Honk 行为不属于本轮适配范围。

## 构建与已经执行的检查

按 AGENTS.md 从工作站发起，不修改 Git staging、共享 input 或无关 lock：

```sh
python3 tools/unicorn-build.py --device --local-only check usb-modes
python3 tools/unicorn-build.py --device --local-only check usb-manager
python3 tools/unicorn-build.py --device --local-only check usb-tethering
python3 tools/unicorn-build.py --device --local-only check unicorn-usb-kernel
python3 tools/unicorn-build.py --device --local-only check usb-signaller
python3 tools/unicorn-build.py --device --local-only check usb-serial-lifecycle
python3 tools/unicorn-build.py --device --local-only check recovery-usb
python3 tools/unicorn-build.py --local-only check configuration
python3 tools/unicorn-build.py --local-only check kernel-config
python3 tools/unicorn-build.py --local-only build usb-tools
```

`unicorn-usb-kernel` 在两个 checks 命名空间均显式使用 x86_64 原生 LLVM 交叉编译，
不使用 QEMU 中的 ARM64 编译器。检查实际 .config、PHY/DWC3/UCSI/xHCI、HID、Storage/
UAS、网卡、USB Audio、NCM/FunctionFS/serial 对象和 DTB。DT schema 工具的 nixpkgs
依赖包提供了 libfdt 模块却没有 pylibfdt 分发元数据，检查包仅移除该 pip 元数据要求，
保留 libfdt 运行时、导入测试和 schema 校验。

构建候选和检查输出的具体路径、哈希及未执行项目记录在 [usb-validation.json](usb-validation.json)。

本轮验证包括 Rust 状态机故障测试、并发请求/有界队列、失败后不无限重试、固件代次
变化重建；Python 共享策略和充电认证回归；从实际 C 补丁提取的中继回调故障注入。
六项依赖物理 UDC/configfs 的上游 Rust 测试保持 ignored，不能计为已通过。

网络包测试使用 Linux 的临时 user/network/mount namespace，不改变工作站网络，
从 `usb-modes` 检查输出取得实际 nft 内容并包装为 `table inet mobile_usb { ... }`：

```sh
unshare -Urnm python3 tests/usb-tethering-netns.py /path/to/evaluated-rules.nft
```

管理接口还可在同样的隔离环境测试，运行 usb-manager 检查构建的 x86_64 二进制：

```sh
unshare -Urnm python3 tests/usb-signaller-dbus.py ~/.local/state/unicorn-build/result-device-usb-manager-0-out/bin/usb-signaller
```

该测试使用独立 D-Bus 和挂起的 systemctl 后端，验证状态查询、模式请求和拒绝请求
仍迅速返回，并验证重复进程被拒绝；隐藏真实 /sys 与 /run，不操作手机或工作站 USB。
本轮各命令约 1–2 毫秒，低于五秒要求；实机时延仍需另测。

网络包测试验证无策略时的路由基线、全局 ip_forward=1 时开发模式仍拒绝转发、NAT 后的真实
源地址、内网隔离、地址伪造、反向新连接以及退出后的关闭状态。

## 实机待验收记录

2026-11-16 已通过 LAN 将当前候选安装为第 18 代默认启动项，第 17 代和更早的
入口保留用于回退。最新快照重建得到相同系统路径；内核队列中 68 个本仓库补丁与
当前文件逐字节一致，另有一个 Nixpkgs RANDSTRUCT 补丁。配置及实际 Kconfig 检查通过。
安装记录保存在手机 `/root/unicorn-usb-deploy-20261116`，详见
[usb-validation.json](usb-validation.json)。尚未重启或激活新系统，当前仍运行第 17 代，
未切换手机数据角色或改变显示电源。保留独立 LAN，重启进入第 18 代后按以下顺序验收：

1. 开发模式与 OTG：无插线启动、连接电脑启动、正反插线；每种模式至少 20 次切换/
   插拔；NCM 新建 SSH、ACM 实际登录；拔线中切换、拒绝角色交换、固件重启后恢复。
2. MTP：仅见 USB-Share；电脑写入、读回、比较 SHA-256，确认 home/私密目录不可见；
   重复插拔与停止 responder，检查端点和挂载均被清理。
3. 网络共享：电脑 DHCP/DNS/默认路由、互联网访问、内网隔离；手机上游切换和掉线；
   退出后 DHCP 不再发布网关/DNS，专用 DNS/策略撤销。
4. 外设供电：鼠标、键盘、U 盘、USB 网卡、数字音频和带电 Hub，分别记枚举、实际
   功能和 VBUS 电压/电流，不能只记 host 状态。
5. 充电：电脑 USB、标准 PD/PPS、原装小米充电器；插拔、认证失败回退、数据并行，
   记录 USB 输入功率，不使用认证标志推算功率。
6. 连续 24 小时、挂起恢复和 DSP 重启：无新 Oops、无限重启或资源泄漏；管理命令在
   五秒内返回。DSP 重启和挂起是单独的实机操作，不随构建自动执行。
