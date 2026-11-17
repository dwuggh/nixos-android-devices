# Unicorn USB2

Unicorn uses firmware-negotiated Type-C roles, a USB2 DWC3 controller and an
external PTN3222-compatible repeater. The installed default remains NCM + optional
ACM. MTP and internet sharing are opt-in. See [the Chinese implementation and
acceptance record](usb.zh.md) for hardware provenance, validation and pending
physical tests, and [USB mode configuration](../usb-signaller.md) for commands.

[Additional USB modes](usb-gadgets.md) document HIDRAW, native multitouch,
optional image-backed mass storage and UVC. These additions have not been deployed.

The current candidate has passed workstation driver/DTB/schema and isolated
network tests. On 2026-11-16 it was installed as default boot generation 18,
with generation 17 retained for rollback. The phone has not rebooted into it.
Host data
role is not evidence of VBUS power, DP support, or analog audio support.
