# usb-signaller package

For configuration, connection and updates, see [USB networking](../../docs/usb-signaller.md).

## Local packaging changes

Upstream source: <https://codeberg.org/DylanVanAssche/usb-signaller/src/tag/v0.4.2>.
The package retains upstream source and adds a documented local patch for:

- stable USB serial/MAC/interface names supplied by the NixOS module;
- explicit controller/role-switch selection and ownership of only
  `usb-signaller-*` gadgets;
- configured mode advertisement, serialized transitions and failure reporting;
- firmware-led Type-C roles on opted-in boards and bounded daemon restarts.

Executable paths are substituted with Nix store paths. Two upstream unit tests
use existing `/` and `.` instead of assuming `/sys` and `/home` exist inside the
build sandbox. Network configuration uses native networkd instead of upstream's
NetworkManager/unudhcpd helper scripts. The upstream unrestricted D-Bus policy is
replaced by query access for all users and mode changes for root.

`lifecycle.patch` removes the old UDC mutation path; `manager.rs` owns startup,
requests, hotplug, rollback and shutdown. A single worker and bounded queue keep
D-Bus responsive. Exact owned names are scoped to the selected controller.
Mode discovery reads gadget links and owned network policy, never ip_forward.
Partial failures restore the previous mode only for the same partner/firmware
in device role. A process lock and ExecStopPost cleanup cover duplicate starts
and daemon crashes. The six upstream hardware tests remain manual.

Configfs reconfiguration still disconnects USB. Keep standard generations and
independent RAM recovery. Hardware/VBUS acceptance is tracked in
[the Unicorn USB record](../../docs/unicorn/usb.zh.md).
