# Connect over USB and update a system

[Configure a system](configuration.md) · [Installation paths](README.md) · [中文](usb-signaller.zh.md)

## Enable the device connection

Installed systems default to USB NCM with U-Boot/UEFI.
Enable SSH and configure your public key:

```nix
{
  androidDevices.usbSerial = "my-phone-01";
  androidDevices.usb.ssh.enable = true;
  users.users.root.openssh.authorizedKeys.keys = [
    "ssh-ed25519 REPLACE_WITH_YOUR_PUBLIC_KEY"
  ];
}
```

The module supplies USB NCM, DHCP on the management link and SSH bound to the
USB IPv4 address. SSH uses keys by default; no demo password or user is added.
The device chooses the following default address. Override it with
`androidDevices.usb.signaller.address` if it overlaps a LAN/VPN or another connected unit.
Set unique USB serials as they determine the gadget MAC addresses.

| Device | Device IPv4 address |
| --- | --- |
| Asphalt | `172.16.43.1/24` |
| Polaris | `172.16.42.1/24` |
| Sheng | `172.16.45.1/24` |
| Clover | `172.16.44.1/24` |

U-Boot exposes an ACM console. RAM recovery independently provides
[ACM/NCM and OpenSSH](recovery.md). Installed systems use NCM, with an optional
ACM login console enabled explicitly by the examples. The normal boot initrd
does not enable USB management by default; usb-signaller starts in stage 2. Recovery configures its gadget directly in the initrd and
does not run usb-signaller. Optional early-boot ACM/NCM uses `initrdUsb` and
tears down its gadget before switch-root. The USB transport does not select
a boot method, prepare storage or install U-Boot.
`androidDevices.usb.ssh.enable` only configures USB access; for SSH on Wi-Fi or other
interfaces, use `services.openssh` directly and set its listen/authentication policy.

## Connect from the computer

Connect a data-capable USB cable. Configure the new Ethernet interface on the
computer with DHCP. For a NetworkManager computer:

```sh
nmcli device status
# Replace USB_INTERFACE with the new USB Ethernet interface.
nmcli device connect USB_INTERFACE
ip -4 address show dev USB_INTERFACE
ssh root@172.16.42.1
```

The computer should receive an address in the same subnet (normally `.2`). The
link supplies no default route, DNS or Internet forwarding, so the computer's
normal connection remains in use. The device does not need a desktop login;
NetworkManager may manage Wi-Fi separately from networkd's USB interface.

If SSH fails, inspect from a local console or another working connection:

```sh
ip -4 address show usb0
systemctl status usb-signaller sshd.socket
systemctl list-units --all 'sshd@*.service'
journalctl -b -u usb-signaller -u sshd.socket -u 'sshd@*.service' -u sshd.service -u systemd-networkd
```

SSH uses socket activation by default: systemd listens on port 22 and starts
an `sshd@*.service` instance for each connection. A listening port alone does
not establish that SSH can complete a login, and `journalctl -u sshd` alone
misses the instance logs. If `systemctl` itself times out, collect the manager's
state from the device's local console before rebooting:

```sh
sudo timeout 5s systemctl show --property=Version
printf 'systemctl exit=%s\n' "$?"
ps -p 1 -o pid,stat,wchan:32,comm
sudo cat /proc/1/stack
sudo ss -lntp 'sport = :22'
sudo journalctl -k -b -n 120 --no-pager
```

Exit status `124` means the query exceeded five seconds. An earlier kernel
Oops in `gs_close` may have left related operations blocked, but attributing
the manager timeout to it requires process stacks or a comparison after the
kernel fix. Rebooting removes the old process-stack evidence.

No USB Ethernet interface usually indicates a cable, port or gadget/controller
problem. An address without SSH access points to credentials, DHCP/routing or
SSH configuration. RAM recovery remains the fallback when the installed system
cannot start. Asphalt and Polaris have hardware-validated NCM/SSH updates. Sheng has passed
installed B-slot U-Boot/EFI Linux, NCM/SSH and Wi-Fi with its 7.3 kernel. Asphalt's
connectors have different support, so consult its [hardware notes](asphalt/hardware.md).

## Optional ACM console

Set `androidDevices.usb.acm.enable = true;` to expose an ACM serial login
console alongside NCM in developer mode. It defaults to `false`; the public
USB examples enable it explicitly. The kernel must include `USB_CONFIGFS_ACM`.
Connect using the device's serial-number link under `/dev/serial/by-id/` on the
host. When the gadget port appears, `serial-getty@ttyGS0` starts a background
waiter. It reads the selected controller's state without opening or resetting
the TTY, and runs getty once USB enumeration reaches `configured`. This means
USB is configured, not necessarily that a host application has opened the
serial port. Waiting does not delay boot or local login. Disconnect hangs up
the serial session; the restarted service waits for enumeration again.

Your configured getty/login policy applies; the module does not create a user,
password, or root autologin. Public desktop examples automatically log in demo
only on local tty1; the ACM console shows the ordinary login prompt. The
minimal lab example retains its explicit root-autologin policy.

ACM does not replace NCM or SSH, and boot does not wait for a host to open it.
It disappears in charging-only/host mode and returns with developer mode.
U-Boot's ACM is a separate console that ends at Linux handoff; the installed
system re-enumerates its own USB gadget. RAM recovery uses its independent
`androidDevices.initrdUsb.acm.enable` setting.

The shared kernel queue protects `gs_close()` when a gadget TTY open fails
before initializing its port pointer. The failure previously caused a NULL
pointer Oops during `tty_open` → `tty_release` → `gs_close` →
`_raw_spin_lock_irq`; waiting for USB enumeration does not protect this error
path. This fix does not identify the original open failure or cover every port
lifetime race. See the [upstream proposal](https://lkml.rescloud.iu.edu/2610.1/13659.html).
`just check-one usb-serial-lifecycle` tests failed-open cleanup and ordinary
open/close behavior against the patched kernel callbacks on the build host.
After installing a rebuilt system generation and rebooting, validate startup
with and without USB, ACM login, new SSH connections, and at least 20 cable
disconnect/reconnect cycles. Confirm there are no new Oops reports and that
the manager query above returns within five seconds. Hardware validation is
separate from the host checks.

For SSH without socket activation, set `services.openssh.startWhenNeeded = false;`.
When changing USB or the only SSH listener, keep another established management
channel until a **new** connection succeeds. A successful activation command
alone does not establish that a replacement listener is reachable.

## Standard updates

Once the computer has an NCM address and SSH access, this is a normal NixOS
remote target. Assuming your flake exports `nixosConfigurations.phone`:

```sh
nixos-rebuild boot --flake .#phone --target-host root@172.16.42.1
ssh root@172.16.42.1 systemctl reboot
```

`switch` also works for immediate activation. Prefer `boot` followed by reboot
when updating the USB daemon, networking, SSH or their configuration: restarting
the transport during `switch` can disconnect the client. Keep Wi-Fi SSH available
when changing the management transport. Build with an appropriate native,
cross or remote builder for the target architecture.

The standard installer updates the store, system profile, EFI kernel/initrd/DTB
and generation entries on FAT `/boot`. Ordinary updates do not flash U-Boot or
recovery. Transfer completion and activation/boot success are separate checks.
An initrd emergency still uses local/U-Boot console or independent recovery;
this backend deliberately does not add networking to the normal initrd.

## Modes and peripherals

The default modes are `developer_mode`, `charging_only`, and `host_mode`.
`mtp_ffs_mode` and `tethering_mode` appear only when their backends are enabled.
USB mode changes are root-only by default; ordinary users may query status.

```nix
androidDevices.usb.signaller = {
  # Unicorn enables this by default. Other devices retain direct role switching.
  followRole = true;
  mtp = {
    enable = true;
    user = "dwuggh"; # An existing non-root account.
    directories = [ "/home/dwuggh/USB-Share" ];
  };
  tethering.enable = true;
};
```

`set_mode` accepts a request immediately; poll `get_status` until `busy=false`,
then inspect `desired_mode`, `actual_mode` and `last_error`. A full queue returns
an immediate error. `bound` and `enumerated` are separate. Failed setup restores
the old gadget only if the same connection/firmware still permits device mode.
Unicorn follows UCSI at startup and on hotplug; explicit data-role requests go
through Type-C and never write the power role. Host role does not prove VBUS.
See [Unicorn hardware and acceptance](unicorn/usb.md).

To let selected users change modes without `sudo` or a polkit prompt, name a
control group and add those users to it:

```nix
{
  androidDevices.usb.signaller.controlGroup = "usb-mode";
  users.users.alice.extraGroups = [ "usb-mode" ];
}
```

The module creates the group and adds a D-Bus rule allowing its members to call
only `set_mode`; ownership of the daemon's bus name and other methods stay
root-only. usb-signaller performs no caller check of its own, so the D-Bus
policy is the whole authorization. **Risk:** any member can switch to
`charging_only` or `host_mode` and immediately disconnect USB networking, the
ACM console and any SSH session over USB, with no prompt. Add only users who may
already administer the device, and keep another management channel (Wi-Fi SSH
or a local console) to return to `developer_mode`. The default `null` adds no
group or rule. Group membership takes effect at the user's next login.

```sh
busctl call com.meego.usb_moded /com/meego/usb_moded com.meego.usb_moded get_modes
busctl call com.meego.usb_moded /com/meego/usb_moded com.meego.usb_moded mode_request
busctl call com.meego.usb_moded /com/meego/usb_moded com.meego.usb_moded get_status
# These disconnect USB networking; issue them locally or through Wi-Fi SSH.
# Members of controlGroup may omit sudo.
sudo busctl call com.meego.usb_moded /com/meego/usb_moded com.meego.usb_moded set_mode s host_mode
sudo busctl call com.meego.usb_moded /com/meego/usb_moded com.meego.usb_moded set_mode s developer_mode
```

| Attachment | Required behavior and remaining boundary |
| --- | --- |
| Computer | Device role + NCM provides DHCP and configured SSH. |
| Mouse/keyboard | A working host/VBUS path lets standard USB HID and the desktop input stack handle it. usb-signaller does not supply power or input drivers. |
| USB disk/card reader | Host/VBUS and USB Storage/UAS/SCSI expose block devices. Mounting and filesystem permissions are normal OS/desktop policy; this minimal Niri example has no automount UI. |
| Display/dock | USB-C DP needs real DP wiring, CC/PD, orientation/mux/PHY and DRM support, followed by compositor output policy. A data-role write alone cannot enable DP. DisplayLink is a separate USB graphics path. |

With `followRole`, startup, hotplug and firmware changes follow the negotiated
role through one serialized manager. There is no desktop selection dialog or
multi-connector arbitration. MTP mounts FunctionFS only for its mode and runs
as the configured user. It requires `POSIX_MQUEUE=y`; a configuration assertion
rejects kernels without that capability. Sharing uses networkd DHCP, a USB-only DNS listener and
restricted IPv4 NAT to the current main-table default uplink; private destination
networks and other LAN interfaces are blocked. Exiting removes its policy.
Honk and complex policy routing are not adapted. Driver configuration alone is
not a physical peripheral or VBUS test.


Package patches and upstream details are documented [with the package](../pkgs/usb-signaller/README.md).

## HIDRAW, touchscreen, mass storage and UVC

These optional modes use the same mode manager and error reporting. See
[additional USB modes](unicorn/usb-gadgets.md) for configuration and CLI examples.
