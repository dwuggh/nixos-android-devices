# Unicorn PR validation — 2026-11-17

This record describes the active `codex/unicorn-port` PR; archive and backup
branches retain historical or unvalidated code.

This port is rebased onto upstream `8dcb481fc2ce91794fd294630e06f15a52ec7513`,
using its pinned Linux 7.3.4, U-Boot 2026.10 and Nixpkgs inputs. `flake.lock`
is byte-identical to that upstream commit. The separate personal configuration's
shared input and lock revisions are preserved.

## Commit boundaries

1. **Unicorn support and shared functionality:** board/artifact registration,
   independent kernel queue, board drivers/configuration, U-Boot, firmware,
   audio, display, touch, keys, charging, Bluetooth identity and recovery;
   optional USB modes/backends, MTP queues, tethering/network identity policy,
   initrd role following, console dependency, DMA heap uaccess, USB serial
   failed-open cleanup, camera helpers and opt-in whole-userdata GPT handling.
2. **Asphalt UFS coordination:** extend upstream's existing OPP table with ICE
   clock scaling, coordinated CX/MX performance requests, ICE bootstrap defaults
   and the existing shared OPP initialization-order fix. Preserve upstream's
   SM8475 turbo configuration, full PHY sequence and media patches. Unicorn's
   kernel queue and resulting source remain unchanged across this commit.

The second commit changes Asphalt's UFS clock/power behavior only; it also adds
its compiled-DTB/schema/object check and corresponding documentation. The other
seven existing board directories and the Sheng charger implementation are
unchanged. Shared runtime changes, including network policy, DMA heap permissions
and USB serial unwind, belong to commit one and can affect existing devices.
The earlier eight-device isolation comparison described the superseded split;
it does not establish isolation of this first commit.

Existing devices retain the upstream USB backend by default, including external
role ownership and connected-NBD protection. The extended backend is selected
only by an explicit extended-mode option; it also rejects mutations during
connected NBD netboot. External role ownership plus extended modes is rejected
until that combination is implemented and validated. Shared network policy and
DMA heap permissions are intentional changes for existing devices in commit one.

## Rebase fixes

- Preserve upstream's newer Asphalt UFS turbo/PHY/media integration and all
  three newly upstream device registrations (Marble, T880 and Dodge). Extend
  Asphalt's existing OPPs in commit two rather than replacing its newer queue.
- Remove duplicate kernel and USB serial defaults now owned by mobile/base.
- Port camera PHY lookup helpers to 7.3.4's existing provider mutex ownership;
  do not reapply already-upstream locking or SA8775P VFE-lite clock hunks.
- Enable Unicorn's legacy iptables/NFT compatibility dependencies required by
  upstream's reverse-path firewall policy.
- Keep stock USB service restart behavior and avoid invoking the extended-only
  cleanup command on the stock daemon.
- Preserve upstream recovery's durable original-GPT backup before any split.
  Whole-userdata preparation rejects Android-size arguments, and rollback checks
  the entire original userdata boundary and unchanged unrelated entries.
- Keep original fingerprint source and inputs on `archive/unicorn-fingerprint`.
  No Unicorn fingerprint driver, package or service is enabled by this PR.

## Validation status

The Linux 7.3.4 ARM64 kernel built successfully using the project helper, native
x86_64 LLVM and the workstation store. The full queue replayed with zero fuzz;
the source-extracted display test passed 241 scenarios with fake hardware.
The earlier device-only split evaluated independently; that historical check
does not describe the current commit boundary. The built artifacts and 25-check
results below describe the Unicorn/shared implementation at `877c8bc` before
this history correction; the added Asphalt UFS check is recorded separately.
All builds/checks below use
`tools/unicorn-build.py --local-only`, with `--device` for repository outputs.
The seven camera decoder tests run directly with Python and synthetic inputs.

| Validation | Result |
| --- | --- |
| ARM64 Linux 7.3.4 kernel and modules | Built with native x86_64 LLVM |
| U-Boot 2026.10 Android image | Built |
| Recovery image | Built; 59,633,664 bytes, within the configured partition limit |
| Public API, platforms, module boundary, package scope, native userspace and release configuration | Passed |
| Unicorn configuration and fingerprint exclusion | Passed |
| Personal configuration check and system derivation evaluation | Passed; pinned shared input and personal lock unchanged |
| Host image/fastboot tools and Bluetooth identity | Passed |
| Recovery daemon/GPT/CLI/screen | 94 tests passed; ext4 loop-device persistence test ignored |
| Extended USB manager | 16 tests passed; 6 environment-dependent tests ignored |
| MTP message queue, tethering and gadget helper checks | Passed |
| USB serial failure/retry/refcount/drain/suspend source tests | Passed |
| USB ARM64 objects, selected bindings/schema and PTN3222 failure/recovery model | Passed |
| Display lifecycle source tests | 241 scenarios passed with fake hardware |
| Charging | Wire-format/error/PDR source model and 21 Python tests passed |
| Hardware keys and ACM login regression | Passed |
| Recovery USB and recovery artifact contracts | Passed |
| USB VM integration | Passed on emulated UDC; modes, transfers, failure recovery and final gadget cleanup |
| Camera table decoder | 7 tests passed without vendor binaries |

The VM uses an emulated UDC on native x86_64 Linux. It tests software integration,
not handset hardware. Ignored tests are not counted as passed. Cross compilation,
model tests and schema checks do not establish physical USB/display behavior.
The VM verified native touch/evdev enumeration, storage writeback/read-only
behavior and loop-attachment rejection, MTP/touch/NCM transitions, and final
cleanup. Its dummy_hcd lacks isochronous endpoints: UVC failure cleanup and NCM
rollback passed, but video streaming was not accepted by this test. The native
x86_64 libcamera build and its debayer-geometry test passed.
Existing upstream DTC warnings about alternate GENI unit addresses remain;
the selected USB schema validation log is empty.

All 25 targeted device checks passed, alongside the personal configuration check
and standalone decoder tests. This is the targeted set above, not every check
in the repository. A new personal system closure was evaluated but not built.

Built artifact identities:

- Kernel: `/nix/store/9l270b4jxxc9azpi59lpaphvcpzjsh22-linux-unicorn-aarch64-unknown-linux-gnu-7.3.4`.
  `Image` SHA256: `e56c941d811215dfa6fa5e7712ce64055d9fc3309fa296e46e9ec17d1399570b`.
- U-Boot: `/nix/store/5nj3dammn6845bajwi9vfq72bg8adkad-unicorn-u-boot.img`.
  SHA256: `43d0d691aa38d048f3da031c227096fdf230004440f432ac69963fca7105adf5`.

- Recovery: `/nix/store/k3rc62grk5ma1x7391s83jbaarav1f6f-unicorn-recovery-boot.img`.
  SHA256: `64f1580e53a1d237aa84c8f8b99311d4c7a5af66e69e032c80af518b5b510ea4`.

Initial failures caused by duplicate USB defaults, outdated test assumptions and
the kernel's relocated source/build directory were fixed and rerun. The large
firmware source cache entry was fetched from the official cache with its original
signature and checked compressed hash; no input pins, trusted keys or global
Nix settings changed.

## Asphalt follow-up validation after history correction

The added `asphalt-ufs` project-helper check passes on Linux 7.3.4. It compiles
ARM64 UFS core/host, GCC, GDSC and ICE objects, validates the selected GCC/UFS/ICE
bindings and compiled board DTB, and checks the effective nine-clock table,
CX/MX parents, ICE bootstrap, performance levels and retained turbo compatible.
The new binding delta preserves upstream's existing SM8475 compatible entries.
Asphalt retains its kernel configuration and prior ordered patches, with exactly
the coordinated UFS patch and OPP initialization fix added. Unicorn and all
seven other existing devices retain their configuration and ordered patch hashes
from `877c8bc`. This is a software check, not a full Asphalt kernel build or a
physical storage/suspend validation. Existing GENI unit-address warnings remain.

## Hardware and distribution limits

The phone remains on the previously validated generation 32, using Linux
7.3-rc6. Its working desktop, basic services and fingerprint deferral were checked
before this rebase. **The 7.3.4 revision has not been deployed or physically
validated.** Earlier camera, charging, USB and kernel audit records describe
their stated revisions; they do not establish hardware acceptance of this rebase.
No physical test has been performed on the other eight devices.

Unicorn's hash-checked OEM archive is currently vendored under
`pkgs/unicorn/inputs/`. Its manifest records `redistribution_reviewed = false`;
redistribution review and migration to the repository's firmware release remain
maintainer review items. Private calibration, credentials and identity inputs
are not included. Camera capture, analogue headset routing and the other limits
in the device guide remain unverified or incomplete as documented.
