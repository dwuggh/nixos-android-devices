# Unicorn USB2

Unicorn uses firmware-negotiated Type-C roles, a USB2 DWC3 controller and an
external PTN3222-compatible repeater. The installed default remains NCM + optional
ACM. MTP and internet sharing are opt-in. See [the Chinese implementation and
acceptance record](usb.zh.md) for hardware provenance, validation and pending
physical tests, and [USB mode configuration](../usb-signaller.md) for commands.

[Additional USB modes](usb-gadgets.md) document HIDRAW, native multitouch,
optional image-backed mass storage and UVC. Their validation records identify which software and physical tests passed.

The 2026-11-16 generation 18 checkpoint in the older validation records is historical. The phone later
booted generation 32 with fingerprint support disabled and basic desktop,
network and service checks passed. This branch is now rebased onto Linux 7.3.4;
that revision has not been deployed or tested on physical USB hardware.
Host data
role is not evidence of VBUS power, DP support, or analog audio support.
