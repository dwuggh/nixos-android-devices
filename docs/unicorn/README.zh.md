# 小米 12S Pro (unicorn)

[English](README.md) · [全部设备](../README.zh.md)

Unicorn 使用 U-Boot → systemd-boot → Linux 启动链，并提供独立 RAM recovery。
内核跟随仓库锁定的 Linux 7.3.4。Unicorn 使用独立的 SM8475 补丁队列，
保留 Asphalt 的上游队列。下述真机结果来自 7.3-rc6，重定基线后仍需重新验收。

## 配置与构建

在 `lib.mkSystem` 中选择 `device = "unicorn"`，或导入 `nixosModules.unicorn`。
个人 flake 的写法见[配置指南](../configuration.zh.md)。仓库控制台示例启用
`172.16.47.1` USB NCM、ACM 登录、root 空密码 SSH 和 NetworkManager。
个人部署前应设置自己的凭据及 USB 身份。
OTG、MTP、共享网络及硬件验收见 [USB2 记录](usb.zh.md)。
HIDRAW、原生触摸、镜像 U 盘和 UVC 见 [USB 扩展模式](usb-gadgets.zh.md)。

```sh
python3 tools/unicorn-build.py --device build kernel
python3 tools/unicorn-build.py --device build u-boot
python3 tools/unicorn-build.py --device build recovery
python3 tools/unicorn-build.py --device build system
python3 tools/unicorn-build.py --device build boot-files
python3 tools/unicorn-build.py --device build bootstrap-info
```

补丁及配置见[内核说明](../../pkgs/linux-kernel/sm8475/unicorn/PATCHES.zh.md)。
[固件输入](../../pkgs/unicorn/README.md)核验哈希并标为 unfree；设备身份及
逐台校准数据保留在手机运行时目录。


当前电脑／手机环境统一使用局域网构建脚本：

```sh
python3 tools/unicorn-build.py build kernel                 # 个人配置
python3 tools/unicorn-build.py build boot-files
python3 tools/unicorn-build.py --device build kernel        # 仓库设备示例
python3 tools/unicorn-build.py --device check unicorn-display-lifecycle
```

个人配置在旁边的 `unicorn-config` 仓库维护。脚本包含未提交、未忽略的新文件，
在电脑求值，内核与设备组件由电脑原生 x86_64 工具交叉编译为 ARM64。
普通 ARM64 软件包由电脑 QEMU 和手机局域网原生构建协作，各一个构建槽位；默认 `--cores 0`，
任务使用所在机器的全部核心（当前电脑 16 线程、手机 8 核）。个人系统及 recovery
内核要求电脑独有的 `unicorn-workstation` 特性；仓库示例先在电脑构建基础内核，
再混合构建其余产物。产物、GC root 和清单均在电脑的
`~/.local/state/unicorn-build`。构建不会激活系统或重启。

一次性电脑配置（本机已完成）：

```sh
sudo python3 tools/setup-unicorn-builders.py --user "$USER"
```

此操作将当前用户加入 Nix trusted-users 并添加专属构建特性。脚本显式使用
局域网 IP、已有 SSH 密钥及已验证的主机指纹，不依赖 root 的 SSH 别名配置。

## 存储与安装

本适配占用原 userdata 全部空间：先建立 1 GiB FAT32 ESP `linux-boot`，
其余为 ext4 根分区 `linux`。准备分区会清除全部 Android 用户数据。
A、super、固件及校准等其他分区边界保留，Android EFI 启动动作关闭。
U-Boot 写入 `boot_b`，独立项目 recovery 写入 `recovery_b`。

从匹配的 RAM recovery、活动槽 B 执行检查，再准备分区：

```sh
recovery prepare-install --check
recovery prepare-install --confirm-erase-userdata
```

已有 Linux 布局时会拒绝重新准备。替换启动镜像前应安装并独立验证
`recovery_b`。随后按[首次安装](../initial-install.zh.md)使用匹配的 `system`
和 `bootstrap-info` 安装闭包、EFI 文件及 U-Boot。后续系统代更新使用 ESP，
不会更新 U-Boot；修改引导程序补丁后需要单独构建并安装 U-Boot。

## 硬件支持

| 功能 | 当前支持与限制 |
| --- | --- |
| 启动与存储 | 已验证 UFS 启动、EFI 系统代更新、独立 recovery 和 Linux 独占布局；有限文件读写及挂起恢复检查通过 |
| 显示与 GPU | 1440×3200 DSC、A730 渲染及 24/30/40/60/90/120 Hz 六档固定模式已验证，默认 60 Hz |
| 触摸与侧键 | FingerTipS 多点触摸；控制台音量键、电源短按熄亮屏、长按正常关机 |
| Wi-Fi | 2.4/5 GHz 连接、传输及重连已验证；NetworkManager 提供稳定本地地址 |
| 蓝牙 | 配置独立核实的本机原厂地址后，配对、耳机播放及 RFCOMM 传输已验证；仍可能偶发固件下载超时，HFP 与 Wi-Fi/音频并用待验收 |
| 音频 | 提供私有运行时数据后，内置扬声器、麦克风录音及回放已验证；USB-C 数字和模拟配件未验证，模拟路由尚不完整 |
| 相机 | S5KJN1 超广角/长焦、OV32B40 前摄原生 RAW10 驱动，实机采集未验证；IMX707 主摄未支持，见[相机说明](camera.zh.md) |
| 指纹 | [原生 C 运行时、专用 libfprint/fprintd 和限时照明](fingerprint.md)；离线支持已实现，实机录入、比对待验收；本地个人配置关闭指纹 PAM |
| 传感器及其他 | 包含 DSP、电池、NFC、振动及红外驱动；专用 SSC 数据/服务及完整外设验收仍未完成 |

已保留 SoundWire 恢复、音频路由及关屏提交修复。模拟耳机的 cross connection
和阻抗检测报错仍是已知限制。表中结果只对应各自验证过的路径，新的组合仍需实测。

## 私有音频输入

模块默认安装 AudioReach topology。准备好本机私有运行时数据后，启用
扬声器与麦克风 UCM 路径：

```nix
hardware.unicorn.audioRuntime = {
  enable = true;
  inputDirectory = "/var/lib/unicorn-audio/private";
};
```

目录须由 root 所有、权限 `0700`，位于根文件系统，祖先路径不能是符号链接。
放入 `cs35l41-dsp1-spk-prot.wmfw`、`T-cs35l41-dsp1-spk-prot.bin`、
`B-cs35l41-dsp1-spk-prot.bin`、`crus_calr.bin`（8 字节）、`t-music.be32`
（300 字节）、`b-music.be32`（64 字节），以及 `manifest.json`。
清单的 `format` 为 `"unicorn-speaker-runtime-v1"`，`files` 对象将上述六个
文件名分别映射到 `{ "bytes": ..., "sha256": "..." }`。输入须为 root 所有的
私有普通文件。准备服务核验后在 `/run/unicorn-audio` 生成驱动所需文件名，
私有数据不进入 Nix store。内核保留功放就绪、校准及保护检查。

## 蓝牙身份

将 `hardware.unicorn.bluetoothIdentityFile` 设为设备侧绝对路径字符串，
例如 `"/var/lib/mobile-identity/bluetooth-address"`。文件须由 root 所有、
权限 `0600`，内容为已独立核实的本机原厂公共蓝牙地址，使用通常显示顺序的
冒号分隔格式。服务校验并回读地址后启动 BlueZ。默认值为 `null`，recovery
不执行身份配置。不能从 Wi-Fi 推算蓝牙地址，也不能复制其他设备的地址。

## 可选显示与控制台设置

`hardware.unicorn.display.seamless.enable` 默认关闭，默认白名单为
`[ 60 120 ]`。60↔120 Hz 活动切换已在三档亮度下通过重复实机检查。
其他活动切换及 10/1 Hz 面板空闲刷新仍为实验功能；通过
`seamless.refreshRates` 和 `idleRefreshRate` 显式配置，recovery 不应用。
不宣称支持通用 VRR。

`hardware.unicorn.hardwareKeys.enable` 默认关闭，控制台示例显式启用，
短按切换文本控制台熄屏，长按关机；`powerOffHoldSeconds` 默认 3 秒，
可设为 1–6 秒。`screen.enable`、`powerOff.enable` 和 `volume.enable`
分别控制短按熄屏、长按关机及音量键。桌面会话应自行处理侧键。

有线充电：[认证、功率采样和实机验收](charging.zh.md)。
