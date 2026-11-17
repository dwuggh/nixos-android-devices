# Unicorn native fingerprint support

> The archive now descends from the rebased port. Fingerprint code and kernel additions are preserved but have not been ported or validated on Linux 7.3.4. The hardware results below refer to the earlier 7.3-rc6 deployments.

> Archived on `archive/unicorn-fingerprint`; development is deferred. The active `codex/unicorn-port` branch excludes this implementation. Gatekeeper status -30 is unresolved; enrollment and matching acceptance are incomplete. The matching branch in the sibling `unicorn-config` repository preserves its enabled configuration.

The repository contains a native C fingerprint runtime, a dedicated libfprint
1.94.100 driver, fprintd linked to that driver, and the kernel support for TA/CDSP
memory sharing and bounded panel illumination. No Android HAL or Android
compatibility layer is installed.

**Real enrollment is blocked by TA authorization.** The generation 25 user
trial reached `PRE_ENROLL` successfully, then the TA rejected the no-token
`ENROLL` request with `GF_ERROR_INVALID_CHALLENGE` (1058), before image capture.
The initial CLI `enroll-stage-passed` was fprintd's duplicate check, not a sample.
Initialization and repeated fprintd opens work. Generation 28 includes the exact
request-length and kernel staging corrections and has been deployed and rebooted;
its loaded transport module Build ID was verified. The user's new setup sends
46 bytes with response offset +48 and now returns TA status -30. No handle or
HAT was obtained. Static analysis of the handset's signed Keymaster image traces
this status to several possible user-record/storage/integrity failures; the exact
runtime branch is unresolved. The failed setup is quarantined with a pending marker. Generation 31 uses the
stable kernel with the tested no-replay guard; the legacy diagnostic-ring
experiment was withdrawn after a hardware access fault. Do not repeat setup or
remove its marker while the secure result is unresolved. See the [validation record](fingerprint-validation.md).

The supported scope is manual, bright-screen `fprintd-enroll`, `fprintd-verify`,
`fprintd-list` and `fprintd-delete`. Login, lock-screen authentication, display-off
wake are outside this scope. Enrollment authorization uses the current Linux
login password in a local terminal. The local personal
configuration enables fprintd and disables fingerprint authentication for every
PAM service, including services supplied by shared inputs.

## Components and ownership

| Component | Responsibility |
|---|---|
| `fingerprint-runtime` | Native worker, profile selector, Wayland hint and descriptor broker |
| `fingerprint-ta` | Hash-checked original signed `gfuni` / `gfuni6` candidates |
| `fingerprint-dsp-firmware` | Hash-checked original Hexagon `libproxy_skel.so` and signed `fastrpc_shell_3` |
| `libfprint` in the Unicorn scope | Independent `unicorn` misc-device driver |
| `fprintd` in the Unicorn scope | Upstream fprintd linked to this private libfprint |
| `qsee-supplicant` | Listener/storage service pinned to `36e06680cf7f690fccbdcd07abc2a64c4bb061d8` |

The ordinary switch is `services.fprintd.enable`. The device module supplies a
`mkDefault` for `services.fprintd.package`; explicit package overrides continue
to work. There is no second fingerprint package option or diagnostic switch.
Recovery forces fprintd off and installs none of these runtime services.

`qsee-supplicant.service` is the sole QSEECOM supplicant. The separate
`unicorn-fingerprint-select.service` holds TA loader processes. These two
services receive `CAP_SYS_ADMIN`; fprintd and its workers have an empty
capability bounding set. They attach through the client TEE node, discovered by
`impl_id=5`, and cannot load trusted applications through the privileged node.

Selection tries the last successful candidate first, otherwise `gfuni` then
`gfuni6`, and stops at the first complete initialization success. Detector,
TA/DSP startup and calibration are mandatory for every candidate. Each probe
uses a separate child process and releases its resources before the next probe.
The name-only cache in `/var/lib/unicorn-fingerprint-selector` is root-only and
atomically replaced; it changes probe order, never bypasses validation. No
enrollment or image capture is used to choose a TA.

Each worker registers a loader lease. The selector waits for its **process exit**
through a pidfd before releasing and reloading the selected TA. This prevents a
new worker from reusing TA state containing a previous worker's freed carveout
address. Killing a worker, canceling an operation or restarting the listener
therefore has a defined teardown path. Secure ioctls themselves have no kernel
cancel operation; a blocked ioctl is never retried as though it had no effects.

## Private inputs and storage

The listener confines all TA filesystem paths, including Android absolute paths,
to `/var/lib/unicorn-fingerprint` (root, mode 0700). New templates and sealed TA
objects never write the original persist partition. fprintd's files under
`/var/lib/fprint` contain versioned locators, not fingerprint images or templates.
A version-2 locator binds its profile, machine-specific application ID, the
complete sensor serial (up to 18 bytes) and its length, group and template ID.
Version-1 locators are rejected because they truncated the serial to 16 bytes;
re-enumerate device templates instead of relabelling old locators. Returned matches must belong to the current request's gallery;
a different enrolled template is a non-match, never an accepted match.

Device-specific input may be provisioned outside Nix, under the root-only
`/var/lib/unicorn-fingerprint/device` directory:

- `oled_wp`: the exact original `ro.boot.oled_wp` value, without an added newline.
- `gf_secure_obj_1.so_0`: optional 64-byte OEM OTP cache, following the HAL's
  absent-file branch when unavailable.

The runtime does not synthesize calibration, rewrite factory persist or insert
these files into the Nix store. Other sealed calibration objects requested by a
TA must be supplied through the confined listener state, after their actual
paths and provenance have been established. Sensor/algo calibration readiness
must include bits `0x13`; missing or rejected calibration is an explicit error.
The original HAL's optional SD-card debug/cache output is not used as template
storage.

The current worker requires a genuine signed HAT for enrollment and fails if
the companion provider is absent. `GF_ERROR_INVALID_CHALLENGE`,
`GF_ERROR_UNTRUSTED_ENROLL` and `GF_ERROR_ENROLL_AUTH_TOKEN_TIME_OUT` remain
terminal authorization errors. A successful Keymaster connection alone does
not establish that a HAT was issued or accepted.

The checks available **now** are initialization and enumeration, without capture:

```sh
fprintd-list dwuggh
sudo unicorn-fingerprint-runtime status
```

`No fingers enrolled` is the current expected inventory. `selector-qualified`
means initialization passed, not that enrollment or matching passed. Physical
display observation and all biometric acceptance remain incomplete.

The authorization workflow below is implemented, but **currently blocked by
TA -30 and the unresolved pending setup**. Do not run setup again or remove its
marker. Once that secure result is resolved, the intended local-terminal flow
uses the current Linux login password:

```sh
sudo unicorn-fingerprint-authorize setup dwuggh
sudo unicorn-fingerprint-authorize enroll dwuggh right-index-finger
```

Setup creates a Linux-owned Gatekeeper credential without capturing a finger;
it never overwrites an existing handle. Enrollment supplies a one-time HAT to
the normal fprintd workflow. Keep the display on and follow its finger prompts.
The handle and random salt are root-only; the password is not stored. Keep the
same password for this credential: migration after a login-password change is
not implemented. Stop and report an authorization error instead of removing
state or repeatedly attempting setup. `fprintd-verify dwuggh` and
`fprintd-list dwuggh` retain their existing usage.

## Bright-screen acquisition

CDSP must boot before native algorithm initialization. The selector allows ten
seconds for remoteproc/udev readiness and reports `event=platform_unavailable`
with exit status 78 if CDSP or its FastRPC character device is absent. systemd
does not restart on that status or on failed profile qualification;
other failures have a bounded restart policy. The board DT enables
`remoteproc@32300000` with the original `cdsp.mdt`; the base firmware package
contains all nonempty signed segments, including the initrd copy. The image's
relocatable 32 MiB span is checked against the actual compiled reservation.
The CDSP node also supplies the NSP-to-DDR interconnect path used by the Q6v5
startup bandwidth vote, as in the OEM cape device tree. Generation 21 confirmed that CDSP now boots and creates both FastRPC nodes.
The selector and worker both receive the packaged DSP search path. The private
FastRPC build also uses that path for the signed process shell, without changing
the generic FastRPC package used by other devices.

The existing ST FTS input device supplies native touch coordinates. Collection
handles slot bounds, multiple fingers in the capture region, dropped evdev
frames and finger removal without grabbing the touchscreen.

The user hint uses layer-shell, viewporter and idle-inhibit in an active local
Wayland session. It draws the OEM physical region `(586,2346,267,267)` on the
1440×3200 panel and acknowledges a committed frame. Touches inside that region
are absorbed by the overlay. fprintd reaches the helper through an authenticated
file-descriptor broker. The user service keeps host uid semantics: mount
sandboxing that implicitly creates a single-uid user namespace would break
`SO_PEERCRED` checks of the root broker and worker. fprintd does not open
`/run/user` or gain DAC/TEE loading capabilities. An absent, ambiguous, rotated or unresponsive display session is
an error; acquisition cannot proceed with an imaginary ready signal.

After that acknowledgement, `/dev/unicorn-fingerprint-illumination` applies the
matching L2S 38/0c/0a white-1000-nit command table and waits four actual vblanks.
If the static hint has already entered panel self-refresh, acquisition exits
self-refresh through KMS and prevents new idle admission while acquiring the
lease. A powered-off display remains an error.
The fd owns an exclusive lease, bounded to 1500 ms. Close, timeout, panel disable
and device removal end it. Brightness changes are cached until the lease ends;
mode changes and idle entry are serialized with the panel state. If the off
command cannot reach the panel, reset/power references are dropped to prevent
stuck illumination; subsequent recovery uses the existing explicit display
recovery path. VT ioctl support remains present.

The TA and CDSP share one persistent, non-CPU-mappable 4 MiB DMA buffer. Ordinary
request/response data still uses kernel-only coherent staging, with exact
request/reply aliasing and separate transport, TEE and TA error checks. The
native AP callback skeleton replaces the Android library; DSP strings and image
material are not logged. Algorithm retries, image reuse, provisional results,
post-authentication phases, duplicate checks and enrollment completion follow
the Delmar overrides, rather than the unrelated `gfenu` layout.

## Build and verification

All Unicorn builds use the repository helper, without activation:

```sh
python3 tools/unicorn-fingerprint-evidence.py
python3 tools/unicorn-build.py --device --local-only check unicorn-fingerprint
python3 tools/unicorn-build.py --device --local-only check unicorn-fingerprint-config
python3 tools/unicorn-build.py --device --local-only check unicorn-fingerprint-services
python3 tools/unicorn-build.py --device --local-only check unicorn-cdsp
python3 tools/unicorn-build.py --device --local-only check unicorn-display-lifecycle
python3 tools/unicorn-build.py --local-only check configuration
python3 tools/unicorn-build.py --device --local-only build kernel
python3 tools/unicorn-build.py --device --local-only build fprintd
```

The offline suite includes the real libfprint public API against a test-only
worker, the real runtime state machine against boundary fakes, and the pinned
listener's storage implementation. The display suite extracts actual patched
kernel callbacks. These tests deliberately distinguish synthetic responses from
handset evidence. See [protocol evidence](fingerprint-protocol.md) and the
[hardware acceptance procedure](fingerprint-validation.md) for the remaining
qualification work. Building does not activate, flash, reboot or power-cycle the
phone.

`unicorn-fingerprint-runtime status` reads board/CDSP state, the protected runtime
profile and systemd states without opening TEE, sensor, DSP or illumination
sessions. It reports selector qualification separately from biometric acceptance
and returns 3 when startup prerequisites are not confirmed. User hint status
refers to the caller's user bus; unknown is not proof of failure or success.

The Unicorn fprintd package protects existing templates from implicit garbage
collection, first-enrollment clearing and replacement. Explicitly delete the
requested finger before replacing it. Capacity and duplicate errors preserve
all existing templates, including those absent from the local fprintd index.
