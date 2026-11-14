# Unicorn firmware inputs

`devices.unicorn.firmware` supplies the six GPU files, complete split ADSP and
SLPI images, the original matching OEM WLAN AMSS/M3/BDF/regulatory quartet,
WCN6855 Bluetooth firmware and Linux's signed regulatory database.
`hardware.unicorn.firmwarePackage` can replace the package. The device module
installs it in the running system and includes GPU/PAS files in the initrd.

`inputs/oem-firmware.tar.xz` is a self-contained, deterministic archive of the
61 selected OEM files and its manifest. `manifest.json` records each file's
length and SHA256, source location, and partition byte extents where available.
Every selected DSP and WLAN file was rehashed against the installed `modem_a`
partition on 2026-11-13. GPU files match the recorded installed vendor inputs.
The archive SHA256 is pinned in `firmware.nix`. These OEM bytes have not been
reviewed for redistribution; the package is marked unfree. No runtime addresses
or speaker calibration are included.

`assemble-firmware.py` rejects missing, extra, duplicate, modified or truncated
archive inputs. It checks ELF metadata, all nonempty split segments and packed
hash metadata for GPU/ADSP/SLPI. These checks establish input completeness;
secure-world authentication and successful DSP startup require device testing.

WLAN uses unchanged `amss20.bin` bytes installed as `amss.bin`, paired with the
same OEM `m3.bin`. The ath11k API2 `board-2.bin` preserves `bd_l2sgf.elf` in its
BOARD record and `regdb_xiaomi.bin` in its REGDB record. Its full PCI/QMI key is
listed in the manifest. Selection follows Unicorn L2S/project8, chip bit0x10,
and board255 in [MiCode's driver source](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/fa4513e61931e11a59e3f177ae590d4e090d7229).
Board255 can also mean a missing optional QMI TLV; confirm the reported key in
the running kernel. Linux's `regulatory.db` is separate from the OEM WLAN REGDB.

Bluetooth files and notices come from `linux-firmware.src` in the project's
locked Nixpkgs; signed `regulatory.db`/`regulatory.db.p7s` come from that same
Nixpkgs' `wireless-regdb`. Bluetooth uses `wcnhpbtfw21.tlv` and
`wcnhpnv21g.bin`, which that source's `WHENCE` identifies as WCN685x UART
firmware. The build checks the patch TLV length and its product/ROM fields
against the handset's measured `0x13`/`0x0201`, and records the selection in
`installed-files.json`. Its `hpbtfw21.tlv` and `hpnv21g.bin` have different bytes and are
listed for QCA2066; they are not aliases for the installed WCN685x files.
The kernel's WCN6855 setup also tries the old names after a transfer error.
An error for `hpbtfw21.tlv` can therefore follow an earlier timeout while
transferring the available `wcnhpbtfw21.tlv`. Check the first transfer result
before treating the fallback filename as a missing package input.

OEM radio firmware may leave addresses unset, so
controller discovery alone does not establish RF operation. Supply identities
at runtime if needed, outside firmware packages and the Nix store.

To validate and assemble the OEM portion without building a kernel:

```sh
python3 pkgs/unicorn/assemble-firmware.py \
  pkgs/unicorn/inputs/oem-firmware.tar.xz pkgs/unicorn/manifest.json /tmp/unicorn-firmware
nix build .#devices.unicorn.firmware
```
