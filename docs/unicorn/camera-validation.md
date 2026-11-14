# Unicorn camera validation — 2026-11-15

This records implementation, native builds and phone tests. JN1 RAW capture
checks have passed on the second kernel; front/main identification and full
camera acceptance remain incomplete.

- Pinned kernel: Linux 7.3-rc6, source
  `/nix/store/lai3wlf8h5qyjs4h6fhhpw308n6xf2l1-source`.
- Initial 63-device-patch queue replayed with `patch --fuzz=0`.
- Changed sensor, CCI, V4L2 core, CAMSS and generic PHY objects and Unicorn DTB
  compiled on the host during initial implementation. These are not a native
  ARM kernel build or a phone capture test.
- Stock IMX707 extraction reproduced the committed register header byte for
  byte; seven decoder tests passed, including invalid references, bounds,
  counts, widths and non-write operations.
- libcamera 0.7.2 **built natively on unicorn**:
  `/nix/store/0c9zfl5vs3ndgxjxmmjng27aldr9ffq2-libcamera-0.7.2`.
- The native Unicorn configuration check built:
  `/nix/store/jn89llg4l8jzk0s7jh6n02f260xyrld0-android-unicorn`.
- DMA heap rule installed temporarily under `/run/udev/rules.d` on the phone
  for validation. The actual active local user `dwuggh` received an `rw` ACL;
  owner/group remained `root:video`, other access remained absent. UID 1000
  allocated and released a 65,536-byte DMA heap buffer through the allocation
  ioctl. The permanent source rule is numbered **70**, before systemd's
  `73-seat-late.rules`; `services.udev.extraRules` would be numbered 99 and
  would add the tag too late to schedule the uaccess builtin.

ARM builds run natively on unicorn over LAN, in
`/home/dwuggh/camera-bringup-20261115`. The personal flake is used with a
command-line `androidDevices` input override; `/etc/nixos` and its lock file
are not edited. The personal kernel adds its existing configuration overrides,
so its derivation differs from the standalone example kernel.

First native candidate kernel derivation:
`/nix/store/nd2b1zrdqv376ishkk4iflrrz99i99is-linux-unicorn-7.3-rc6.drv`.
The full kernel and matching boot-file builds both exited successfully (0).
Their logs are
`native-kernel-stock-pins-build.log` and `boot-files-stock-pins-build.log` in that directory.
The first boot-file attempt exposed an
unrelated fetchzip failure in the existing audio topology source: the GitHub
archive URL ends in a revision, so archive type detection fails. An explicit
`extension = "tar.gz"` fixes detection without changing the revision/hash;
the boot-file build has been retried. A subsequent source audit corrected
settle handling from an erroneous nanosecond conversion to the OEM's direct
register count 30. The final candidate also matches the OEM 4 mA drive
strength for MCLK2 and MCLK5 (the SoC default was 2 mA). The native build
was restarted with those fixes and completed successfully. The sensor module,
CAMSS, PHY module and Unicorn DTB are built. Final `.config` enables
`VIDEO_UNICORN_SENSORS=m`, `VIDEO_QCOM_CAMSS=m`, `PHY_QCOM_MIPI_CSI2=m`,
`DMABUF_HEAPS=y`, `DMABUF_HEAPS_SYSTEM=y` and `DYNAMIC_DEBUG=y`.

The initial downloads used the official cache because the active
`/etc/nix/nix.conf` still contained only that cache, despite the personal Nix
source already specifying USTC. At the user's request, the effective phone
configuration and resumed build commands now use:

```text
https://mirrors.ustc.edu.cn/nix-channels/store?priority=10
https://cache.nixos.org?priority=40
```

The previous effective configuration and its original store target are saved
in the phone build directory. The Nix daemon was restarted after stopping this
session's build clients; the USTC store connection check succeeded.

The pre-candidate phone kernel was
`/nix/store/l05g4gb2dfbxh5dx476w6p2qvqhz8za0-linux-unicorn-aarch64-unknown-linux-gnu-7.3-rc6/Image`,
boot ID `a52ea492-f2e3-4f9f-9aca-a176f93810f6` at the latest hardware check.
It still reports OV32B40 NACK at `0x300a` and has no sensor links in CAMSS;
the newly built libcamera consequently still lists no cameras on that kernel.
This is baseline evidence, not a test of the new kernel changes.

Remaining phone acceptance: candidate boot, front/main ID reads, five boot
probes, twenty capture cycles, 300 consecutive frames each, exposure/Bayer/
timestamp/CSI checks, actual-scene software ISP preview, power release, and
ultra/tele regression with failed-probe isolation. After the user explicitly requested installing the new kernel as default,
NixOS generation 15 was installed with `switch-to-configuration boot`. The
generation 14 entry remains available for rollback. Generation 15 has booted successfully (boot ID
`ac28b5fa-5a36-4fa8-a4ca-5672b0c1ce00`). This is not a full capture
acceptance result.

The first native build uses camera patch SHA-256
`892b80a0e153f0c81a408711677b27e33b670a926c1acf7e243f4f9cf4041950`.
A later cover-letter-only correction replaces "30ns settle" with "settle
register count 30". The current patch SHA-256 is
`482ababa6687966b4956f5f078d6d8dec494654d1a846c61c2b3b4a82c1c0c7d`;
the kernel diff body is byte-identical (SHA-256
`68a346a9b2476eb42bb1baf019d16a62525b16574af8860c2a619e67d03156ad`).

Native build outputs:

- Kernel: `/nix/store/3v54h45j2m86nz8i5dp4bp7hbdhpl7d8-linux-unicorn-7.3-rc6`.
- Modules: `/nix/store/7j0jz2znkhbhxqlyw39sh7dy6p7gq1vf-linux-unicorn-7.3-rc6-modules`.
- Development/config: `/nix/store/41wxd2bz1bb9w81jydzkmfnjvgvwfddw-linux-unicorn-7.3-rc6-dev`.
- System: `/nix/store/iwgmavz0qs6yfmwgpfrxjiq3zp3wsyn9-nixos-system-unicorn-26.11.20261010.6510408`.
- Boot files: `/nix/store/lpmvc72vdiy3mrd5nihn53rq7vaxgdz2-unicorn-boot-files`.
- Image SHA-256: `ef6d463b2445d683501fb0b26ef19c414ae1d4550a5639601d7ff0a8e7128c60`.
- DTB SHA-256: `4396c62dda8043e4ed1c589c9f64103001c5bf8556a6718a549c465244fe7274`.
- Default entry: `nixos-11865bf003a6e093e64d4d7f3f69c98e685f6d478ed840225575d2594d71c58f.conf`.
- Saved previous loader/profile/install log: phone `/root/unicorn-camera-deploy-20261115`.

First candidate hardware results:

- Front `6-0010`: reported MCLK 19.2 MHz, AVDD 2.8 V, IOVDD 1.8 V,
  DVDD 1.104 V and released reset; `0x300b` identification still fails with
  `-ENXIO`. CCI identifies message 1/2, write/address phase, length 2.
- Main `5-0010`: mandatory `0x0016` identification also fails with `-ENXIO`
  at message 1/2, write/address phase, length 2. No ID bypass is used.
- Both JN1 modules identify as `0x38e1`. Both enumerate in libcamera despite
  front/main failures, demonstrating per-sensor graph registration.
- Telephoto captured three 4080×3072 `SGRBG10_CSI2P` frames, 15,679,488
  bytes each, monotonic sequence/timestamps and approximately 30 fps.
- Starting the next camera session fails: `titan_top_gdsc status stuck at
  'off'` in `gdsc_enable`, followed by CAMSS pipeline power-up `-EINVAL`.
  Ultra-wide and later telephoto attempts cannot start. Twenty restart cycles,
  continuous 300 frames, exposure and software ISP acceptance have not passed.
- The independent display self-refresh worker also emitted `driver disabled
  vblank in self-refresh`. Camera tests did not use DRM/KMS preview or cycle
  display power. This warning is recorded separately from camera errors.

The locked kernel lacks the SM8475 camera GDSC retention and RCG parking
fixes from [Sushmit Singh Rathore's v2 series](https://lore.landau.one/lkml/20261020142157.1749-1-sushmit718@gmail.com/T/).
`camcc-retain-flops.patch` and `camcc-park-clocks.patch` preserve the upstream
changes and attribution. The current full 66-patch queue, including the
current shared display fixes, replayed with zero fuzz. The second native build
completed on unicorn (both kernel and boot-file exit status 0):

- Derivation: `/nix/store/ziccc1j1ylnw2vwl7f3b35gayhhgg1n9-linux-unicorn-7.3-rc6.drv`.
- Logs: phone build directory `native-kernel-camcc-build.log` and
  `boot-files-camcc-build.log`.

No conclusion about a physical front/main module fault follows from these
software states. Their NACKs remain unresolved until the clock/power path
is verified; connector voltage/clock/reset waveforms have not been measured.


Second native candidate, installed as default NixOS generation 16 with
`switch-to-configuration boot`, then booted successfully:

- Boot ID: `0c18b8d0-10ef-43df-9173-f41cfa9ce9a0`.
- Kernel: `/nix/store/pzl24nv9kyy95m1q6y1k3arqlg8mbgdn-linux-unicorn-7.3-rc6`.
- Modules: `/nix/store/pf5jcil7lmnfy19n8z8x2yfigjfvqf5d-linux-unicorn-7.3-rc6-modules`.
- Development/config: `/nix/store/r8qzja6sgcxvp7sklsskmz94b3misbdk-linux-unicorn-7.3-rc6-dev`.
- System: `/nix/store/p6xmxjamgg9g2ih1v9zqnkxn30knza9l-nixos-system-unicorn-26.11.20261010.6510408`.
- Boot files: `/nix/store/y062jlpxi0838hp4fdzcbsv1b0hgks13-unicorn-boot-files`.
- Image SHA-256: `4750da09c6174f7f6841d0dfa55206c156f7404aa4ce135f918c2ffbad863b16`.
- DTB SHA-256 remains `4396c62dda8043e4ed1c589c9f64103001c5bf8556a6718a549c465244fe7274`.
- Default entry: `nixos-1cafd49153e4e26baea3c80351018222e5d3a6e46517f3a40e8cfdc51c0300d1.conf`.
- Saved manifest, previous loader/profile and install log: phone
  `/root/unicorn-camera-camcc-deploy-20261115`. Generations 14 and 15 remain
  available for rollback.

Second candidate hardware results:

- Front/main still fail mandatory identification in the write/address phase.
  Reprobing each with its CCI parent's runtime PM forced to `on` reports
  parent `active` and the same `ENXIO`; neither sensor binds. The original
  parent PM setting was restored after each test. Logs/traces are in phone
  `camcc-parent-probe/` under the build directory.
- Both JN1 modules still enumerate independently. Each completed 20 separate
  three-frame RAW capture sessions, followed by 300 continuous RAW frames
  and a saved one-frame RAW sample. All sessions returned success, with the
  expected frame counts, consecutive sequence numbers, strictly increasing
  timestamps and nonempty buffers. Mode: 4080×3072 `SGRBG10_CSI2P`,
  stride 5104, 15,679,488 bytes/frame, approximately 30 fps.
- Evidence directories: phone build directory `camcc-tele-validation/` and
  `camcc-ultra-validation/`, including per-session logs, full kernel logs,
  sensor runtime PM snapshots and JSON summaries. These checks establish
  repeated capture and failed-probe isolation, not physical CSI ECC/CRC
  counter acceptance, Bayer correctness, software ISP image quality or five
  successful front/main boot probes.

After both JN1 capture suites completed, new LAN SSH connections began
accepting TCP but timing out before the server banner. ICMP responses
continued, and the user confirmed that waking/unlocking the phone showed a
normal desktop. A manual reboot was requested because no remote command
could be started. Exposure, software ISP and post-capture power checks are
pending connection recovery; this is not a claim that those checks passed.


After the user's manual reboot, LAN SSH recovered. The same generation 16
kernel booted with ID `e613484b-2a7e-4c21-8709-4e3668073c16`.
Both JN1 IDs were read again; front/main still failed mandatory identification.
Battery status was Charging at 55%.

As the active desktop user, a GStreamer test processed five ultra-wide frames
through libcamera's software ISP to a fake sink and reached EOS successfully.
Five-second Wayland previews for ultra-wide and telephoto both entered PLAYING,
used the software ISP (4080×3072 GRBG10 input), and completed graceful EOS
shutdown on the test timeout. Sensor runtime PM returned to suspended after
preview. This proves the processing/window pipeline runs; actual scene,
colour/Bayer correctness and exposure response still need visual assessment.
The current preview negotiates a cropped 3840×2400 processed stream and scales
it to a 960×720 window; it is not a full-field-of-view or geometry acceptance
check. An independent display self-refresh warning also appears earlier in
the boot log; these previews do not use DRM/KMS or display power cycling.

The phone has a user-owned test script:

```sh
cd ~/camera-bringup-20261115
./camera-test.sh ultra       # Wayland preview, up to 60 seconds
./camera-test.sh tele        # Run after the previous preview stops
./camera-test.sh ultra raw   # 30 RAW frames, text output, no saved image
```

Run as `dwuggh` from the phone's desktop terminal. Ctrl+C stops preview;
logs are saved in `~/camera-bringup-20261115/manual-tests/`. The script uses
stable camera IDs rather than probe-order indices and explicitly includes
the candidate libcamera and installed GStreamer plugin paths. It is a
phone-specific diagnostic helper, not an application API or a packaged
camera application. Ask the user to assess visible scene motion, cover/uncover
response, colour, freezes and repeat-open behaviour. Autofocus is not
implemented, so sharpness is not an acceptance criterion at this stage.


## Front-only follow-up after owner clarification

The owner reports that the front camera is good and the main camera may be
faulty. The date/system version of the last working front-camera use is not
known. The front camera is therefore the priority for software diagnosis;
main/front failures are not treated as evidence of the same physical fault.
The following tests ran on the same generation 16 kernel, using temporary
modules compiled natively with its matching development output and Clang
23.1.3. The default kernel and production patch were not replaced by these
diagnostic modules. No chip-ID check was bypassed.

| Controlled test | Observed result |
| --- | --- |
| Hold front powered for 3 seconds before the mandatory OEM word ID read | Still `ENXIO`; MCLK5 enabled at reported 19.2 MHz, reset output latch high, GPIO44/64 output latches high, supplies enabled in the regulator framework |
| Disable inherited pull-downs on front AVDD/DVDD enable GPIO64/GPIO44 | Still `ENXIO` |
| Match OEM CCI GPIO114/GPIO115 drive strength, 4 mA instead of 2 mA | Still `ENXIO` |
| Apply both pinconf differences together | Still `ENXIO`; debugfs confirms requested configuration, then restoration |
| Read full three-byte ID at `0x300a` at documented addresses `0x10` and `0x36` | Both return `ENXIO` |
| CCI standard/fast-plus timing configurations with existing 19.2 MHz CCI source | Front still unbound; telephoto ID and three-frame RAW capture succeed in both configurations |
| Set CCI source to 37.5 MHz and test 100 kHz, 400 kHz, 1 MHz timing configurations | Front still unbound in all three; telephoto ID and three-frame RAW capture succeed in each; source restored to 19.2 MHz on detach |
| Use 24 MHz MCLK for ID reads only, then restore OEM 19.2 MHz | Still `ENXIO`, including full-ID and read-only tests at both addresses; no mode table is written at 24 MHz |
| After separate power cycles, make a pure read the first CCI transaction, once at `0x10`, once at `0x36` | Both first reads return `ENXIO`; no preceding register-address failure in each test |

The bounded alternative-address/24 MHz ID tests follow the sensor-specific
[Rockchip OV32B40 driver](https://github.com/rockchip-linux/kernel/blob/develop-6.1/drivers/media/i2c/ov32b40.c),
which checks full ID `0x563242` and lists both addresses. Its mode tables and
board power configuration are not imported into the Unicorn driver. Unicorn's
production mode remains the owner's stock 19.2 MHz mode.

The CCI source-rate discrepancy and pinconf differences are observable
configuration differences, but correcting them in isolation did not resolve
identification. The nominal CCI bus rates above denote timing-table selections;
SCL waveforms were not measured. GPIO debugfs reports output-register latches,
not measured connector voltage. Framework voltage/frequency reports do not
establish that those signals physically reach the sensor.

CCI1 was rebound for the timing comparisons, briefly removing/re-registering
telephoto and front I2C devices. Each test restored the original
`i2c-qcom-cci` driver. Telephoto captured three frames after each restoration;
final libcamera enumeration still includes both JN1 modules, with their runtime
PM states suspended. The front-only diagnostic drivers and pinconf helper were
unloaded, the original pin biases/strengths and 19.2 MHz clock sources restored,
and this session's temporary persistent sensor-debug option removed. Loading
out-of-tree diagnostics sets the kernel's module taint flag until reboot;
it does not change the boot image.

Phone evidence is under `~/camera-bringup-20261115/`: `front-hold-probe/`,
`front-pinconf-probe/`, `front-address-probe/`, `front-cci-rate-probe/`,
`front-cci-clock-probe/`, `front-xclk-{19200000,24000000}/` and
`front-readfirst-{10,36}/`. Final restoration state and a source/log archive
are saved as `front-diagnostic-final-state.json` and
`front-diagnostics-20261115.tar.gz`.

No tested software change has resolved front identification. The next useful
evidence is a working stock-runtime configuration/trace to compare, or direct
measurement of sensor power, reset, MCLK and I2C at the connector. The current
results do not establish that the front module is defective. Main-camera
capture acceptance remains blocked independently.
