# Unicorn fingerprint validation

## Current blocker: Gatekeeper new-enroll rejected with -30, 2026-11-17

Current final deployment is **generation 31**, stable kernel plus corrected
pending-state protection: `/nix/store/4swzwrkq632kmdm76gkf7cmh91r6m2y9-nixos-system-unicorn-26.11.20261010.6510408`.
It was rebooted; current/booted closures, loaded GNU kernel Build ID, Image SHA
and all installed program hashes match the build. The failed legacy diagnostic
implementation is absent. Three final Claim/List/Release cycles passed
(11878/7514/6110 ms), with no enrolled fingers. Read-only CLI list/status and
Gatekeeper API 4.1/TA 4.1012 negotiation pass. Services have no restarts. Both
setup/enroll reject the quarantined credential before requesting a password.
The 57 native/target runtime source files match the tested closures. The six
required helper checks, runtime/fprintd/kernel and final system builds pass.
**This repairs failure-state protection, not the underlying TA -30 rejection.**

Generation 28 was deployed over LAN and rebooted. Current and booted closures
match `/nix/store/8vm3cg29l05lr330qv13637p46p3d7br-nixos-system-unicorn-26.11.20261010.6510408`.
The main Image hash did not change because the staging correction is in a
module. The loaded QSEECOM GNU Build ID matches the corrected module
(`94ee500370d8b86aa5cedfd7945c4dd01443dfbd`; module SHA-256
`3167c83314a19612a4917ef9fc6588a6075c5a97735933db966a4581ca1dba66`).
Full gfuni6 initialization/calibration and three fprintd Claim/List/Release
cycles passed after reboot, with empty inventory. Installed program hashes
and all 56 runtime source files match the native/target tested closures.

The user's subsequent local `setup dwuggh` passed PAM and now reports
`command=0x21001 request-bytes=46 response-offset=48 TA status=-30 retry-ms=0 TEE code=0`.
There is no committed Linux credential handle or signed HAT. A root-only pending
marker now quarantines this unresolved setup. Repeating the
password or treating initialization as enrollment success would not resolve it.
No raw credential-store data, HAT, password, image or template was collected.

Static analysis of the identical signed `keymaster_a`/`keymaster_b` code images
(SHA-256 `453cd884fcc26c562597a0a93a79a9824da36a6d902297cee21b9821d9688ef0`)
locates the new-enroll, decoder, record lookup, storage and HMAC branches. The
actual TA accepts arbitrary desired-password bytes and generates a new SID/key.
Its record lookup masks storage initialization errors and exhausted capacity
as NULL; new-enroll converts that to -30. Existing-record HMAC verification can
itself return -30. These are candidate runtime failures, not evidence of storage
corruption or a full store. The exact failing branch remains unresolved. Query
and negotiation share the same open-storage guard, so their success alone does
not verify later secure-record reads/integrity.

A legacy opt-in diagnostic experiment passed 33 source-extracted parser and
registration scenarios, then **failed on hardware**: registration returned
success, but the first shared-ring header read raised a synchronous external
abort in `qseecom_gatekeeper_errors_show`. The process died before the planned
read-only GET_VERSION rejection probe. No new credential operation was issued.
Generation 29 was rolled back and rebooted to the stable generation-28 closure;
three real Claim/List/Release cycles then passed (10621/7703/6325 ms), empty
inventory. The failed patch and test were withdrawn from the repository/build
queue and preserved only with ignored investigation evidence. CPU ownership,
firmware log encryption and the exact cause of this diagnostic fault are not
confirmed. Passing boundary-substitute tests did not validate secure-world
memory accessibility, and this experiment must not be counted as usable logging.

The final correction combines the previously stable kernel with the tested
pending-state protection. The six named helper checks passed again after the
unsafe diagnostic implementation was removed. Generation 31 is deployed/rebooted and verified as recorded above; the Gatekeeper -30 blocker itself remains unresolved. No credential
reset/delete, raw RPMB export, invented RootOfTrust, bypass or password guessing
is part of this investigation.

The evidence audit now validates 23 analysis inputs, the 20 existing signed
Goodix TA files, and the signed Keymaster command dispatch entries independently.
A further defect was found in provisioning: a returned TA status removed the
pending marker even though the signed TA can fail after writing a record. The
fixed companion retains it on every failed credential call and refuses replay
before asking for another password. Its real provisioning/filesystem regression
replaces only TEE/commit boundaries and covers returned TA errors, transport
uncertainty, failed local commits, successful commits and close errors.

The active acceptance blocker is legitimate credential creation; all real
biometric sample, gallery, persistence, delete and recovery criteria remain
incomplete.

## Current Gatekeeper setup failure and transport correction, 2026-11-17

On deployed generation 27 the user's local `setup dwuggh` passed the existing
Linux PAM password check, then Gatekeeper credential creation returned positive
status 16 (`retry-ms=16`, TEE status zero). No fingerprint was captured. The
provider did not automatically retry. The credential directory has no committed
handle or pending marker after the reported TA rejection.

Re-reading the exact OEM `sendCmd` disassembly exposed an implementation error:
its request-size argument is the unpadded CBOR size plus four bytes, whereas
only the separate response pointer is word-aligned. Generation 27 passed the
aligned size as the request length, adding two bytes to this enrollment input.
The stock QCBOR finish routine returns 16 for unconsumed bytes. That is
consistent with this failure, but corrected hardware behavior is still needed
to establish the cause. The previous test incorrectly assumed that request
size and response offset were equal; its passing result did not establish OEM
transport conformance.

A second confirmed discrepancy is in the kernel's private staging: disjoint
responses were packed immediately after the input, discarding any original
same-SHM alignment gap. The correction preserves exact input lengths and
forward same-SHM response offsets, includes the gap in allocation bounds, and
places patched buffers after the full extent. Goodix exact aliases still work;
partial/reversed same-SHM layouts are rejected. The new native test extracts
the production memref, patch validation, layout and invoke functions. It uses
allocation/session/SCM substitutes and checks literal 46/+48 and 117/+120 calls,
ordinary/private patches, alias address scrubbing, invalid extents and teardown.
The full display/listener regression and corrected native/ARM64 runtime check
pass. This correction was deployed and rebooted as generation 28, as recorded above;
the real user trial now returns -30. Signed HAT issuance and Goodix acceptance
remain unverified.

## Authorization implementation deployed as generation 27, 2026-11-17

The user chose the existing Linux login password for legitimate enrollment
authorization. The new C companion validates that password with the existing
`login` PAM stack, derives a salted scrypt credential, and uses the resident
Keymaster Gatekeeper implementation. It does not change PAM configuration,
store a plaintext password, replace Android credentials or disable TA checks.
The fixed D-Bus, worker IPC and locator formats remain unchanged.

The exact stock Gatekeeper/Keymaster binaries were extracted from the already
available vendor image for static analysis; hashes and decompiled addresses are
recorded in the evidence manifest. The independent audit now covers 22 analysis
inputs and the existing 20 signed TA files. A read-only handset probe connected
to `keymaster64`, observed API 4.1/TA 4.1012 and completed the exact OEM client
negotiation. **No Gatekeeper enroll/verify/delete operation was issued by this
probe, and no signed HAT was obtained.**

The native runtime check passes real protocol/engine tests, including the new
Gatekeeper codec with TEE-boundary replacements, malformed and truncated CBOR,
challenge/SID/type checks, throttling and transport failures without replay.
Real Unix-socket tests cover peer rejection, provider absence, cancellation,
malformed replies and one-request delivery. Credential-state tests cover atomic
no-replacement writes, permissions, links and corruption; PAM-boundary tests
cover failed authentication and expired accounts. Existing engine, DSP,
libfprint and fprintd template-protection tests pass. ARM64 artifacts were built;
credential operations have not been counted as hardware-tested.

Before deployment, `fprintd-list dwuggh` still found no enrolled fingers, and
`/var/lib/unicorn-fingerprint-authorization` did not exist. The next hardware
step requires the user to run the installed `unicorn-fingerprint-authorize setup
dwuggh` locally and enter the current login password there. Then the companion
`enroll` command must obtain a fresh HAT and the Goodix TA must accept it before
finger acquisition. These steps, physical display observation and all real
biometric acceptance remain incomplete. Do not treat the read-only probe or
passing offline tests as proof that the enrollment blocker is resolved.

The six required helper checks and final personal system build passed. The
system was deployed over LAN and switched to generation 27:
`/nix/store/xz3mda4iaq5gnfhxdyzydrv0lv57b3sa-nixos-system-unicorn-26.11.20261010.6510408`.
The actual current link and three installed program hashes match the build.
Booted generation 25 and the same kernel/initrd remain; no reboot was performed
for this update, and generations 24–26 remain available. All 56 runtime source
files match the tested native and ARM64 closures. Post-deployment Gatekeeper
read-only negotiation passed again. Three persistent-client fprintd
Claim/List/Release cycles passed (Claim 6442/7181/6420 ms), with empty inventory.
The root provider can read the nondumpable installed worker's executable path,
as required by its peer check. System, selector, fprintd and user hint are active;
selector/fprintd have zero restarts, hint has three recorded restarts. This does
not verify the physical frame. Details and hashes are in
[fingerprint-validation.json](fingerprint-validation.json).

## Historical enrollment blocker: TA challenge authorization, generations 25–26


The user ran `sudo fprintd-enroll dwuggh` on generation 25. At 00:55:15 handset
journal time, pre-enroll (`1000,2`) succeeded; enrollment start (`1000,3`) returned
`GF_ERROR_INVALID_CHALLENGE` (1058). Cancel and post-enroll cleanup succeeded.
No sensor capture, image read, algorithm enrollment or save command was issued.
The first reported `enroll-stage-passed` came from upstream fprintd's completed
duplicate check with an empty gallery. It was initially misinterpreted as an
acquired sample; that interpretation is corrected here.

Both OEM HALs were rechecked: the 108-byte enrollment request, group at +28 and
packed HAT at +33 match. Their null-HAT AP branch leaves the authorization bytes
zero, as the worker does. The installed TA nevertheless rejects this request.
The unresolved dependency is legitimate challenge-bound enrollment authorization;
there is no signed-HAT provider in this runtime/IPC. The successful Keymaster
initialization exchange does not provide an enrollment HAT. Changing the public
challenge or inserting an unsigned token would not supply signed authorization.
No such workaround was applied.

The diagnostic update classifies 1058 with the existing 1059/1142 permission
errors and logs only challenge presence, token absence and the TA result.
Boundary tests use both real profile encoders and the real engine to verify all
three errors are terminal, preserve the error through cleanup, produce no
progress/capture/save/delete and do not replay the enrollment start. Real
libfprint API tests verify permission-denied classification with no progress
callback. fprintd retains its D-Bus vocabulary, so its CLI may still say
`enroll-unknown-error`; the journal now names the missing authorization path.

Real enrollment, matching, gallery-scope and persistence/deletion trials remain
incomplete. Repeated finger presses cannot resolve this pre-capture rejection.

The diagnostic update was built and activated as generation 26 without a reboot:
`/run/current-system` is
`/nix/store/fln1xajkri48b51w070blg92g0lfc3s2-nixos-system-unicorn-26.11.20261010.6510408`;
`/run/booted-system` remains generation 25. Both use the same kernel/initrd.
The real fprintd preflight was repeated once without finger acquisition: the
challenge-presence boolean was 1, token was absent and TA returned 1058. The new
permission error appeared in the journal; capture/read/save command counts
remained zero and `fprintd-list dwuggh` still reported no enrolled fingers.

The full fingerprint offline/real-driver check, service VM check, personal
configuration check and system build passed for this diagnostic update. All 43
included runtime source files match both the tested native and ARM64 source
closures. System state is running, selector/fprintd are active with zero restarts;
the hint is active with three recorded restarts and no further increment between
the two post-activation observations. Its physical frame remains unverified.

## Verified device-open repair: generation 25, 2026-11-17

The fprintd device-open error is fixed and verified after deployment/reboot.
Both `/run/booted-system` and `/run/current-system` match
`/nix/store/vri2l9hqifk2g0c6mnmx669wz32n9h7z-nixos-system-unicorn-26.11.20261010.6510408`.
The selected EFI kernel and initrd hashes match this closure; the DT hash is
unchanged. Generation 24 and earlier rollback entries remain. The temporary
runtime drop-in used to prove the fix is absent after reboot.

The root cause was a missing device allowlist entry, not TA calibration:
`libadsprpc.so` uses its default ADSP domain for coherent shared allocations even
when the proxy URI selects CDSP. The module now allows `/dev/fastrpc-adsp rw`.
The capability bounding set stays empty and all other service restrictions
remain. The pinned allocator source and required policy are documented in
[fingerprint-protocol.md](fingerprint-protocol.md#fprintd-allocator-device-access-2026-11-17).

Before deployment, three persistent-client D-Bus Claim/ListEnrolledFingers/Release
cycles passed with the targeted runtime rule. After reboot, another three Claim
and Release cycles passed through the installed fprintd service; initial claim
including service activation/selector qualification took 11593 ms, subsequent
claims 5679 and 4833 ms. Each list call returned the expected
`net.reactivated.Fprint.Error.NoEnrolledPrints`, consistent with the empty initial
inventory. These checks do not enroll, capture, delete or clear templates. No
allocator descriptor failure or domain-0 EPERM recurred in these opens.

The updated device configuration check, production service VM regression and
personal configuration check passed; the final personal system built successfully.
The unchanged protocol/runtime, template-protection, CDSP and display checks
retain their earlier passing results. The OEM evidence audit again checked 18
analysis inputs, 20 signed TA files and the 308-entry HAL error table. Current
paths, hashes and trial timings are in [fingerprint-validation.json](fingerprint-validation.json).

**Remaining:** physical frame/highlight observation, enrollment authorization,
two real enrollments, positive/negative/gallery-scope trials, template persistence,
explicit deletion and capture-time cancellation/recovery. The user requested to
perform finger tests themselves. No real biometric trial is reported as passed.

## Earlier integration failure: generation 24, 2026-11-17

Generation 24 was deployed over LAN and rebooted. Both booted/current links match
`/nix/store/f94w0wmxw9md62wiw34kl6d2m54abd3n-nixos-system-unicorn-26.11.20261010.6510408`.
Generations 21–23 and earlier rollback entries remain. The six required checks
and the final personal system build passed; their latest outputs are recorded in
[fingerprint-validation.json](fingerprint-validation.json).

The bounded QSEECOM listener fix was exercised on hardware: one initialization
serviced 558 listener exchanges in 5272 ms, without the previous ELOOP.
Provisioning now totals 107 original calibration files/backups, including the
complete `gf_cali_0` through `gf_cali_3` family. Every source was read from the
original persist partition mounted `ro,noload`, compared after copying, and
installed without replacing an existing file. The original partition is
unmounted. Existing generated OTP/flocbase files were preserved. The optional
`lbautoCalibrationParams.so_0_0` is also absent in factory persist and was not
synthesized. Calibration now reaches sensor `0x2`, merged `0x13`.

The full `gfuni6` selector probe succeeds. Generation 24 also verifies that the
successful-candidate cache prioritizes `gfuni6` after reboot. A separate root
worker opened, enumerated zero device templates and closed successfully;
`fprintd-list dwuggh` initially listed no enrolled fingers. These are initialization
and inventory results, not biometric acceptance.

**Blocker in generation 24 (resolved in generation 25):** the first actual `fprintd-enroll -f right-index-finger dwuggh`
attempt failed during device claim after about 8.2 seconds, before capture:
`Remote I/O error; TA GF_SUCCESS (0)`. The fprintd worker's FastRPC logs show domain
0 device access denied (EPERM), process-shell DMA allocation failure with both
allocator descriptors -1, and CDSP remote-open error 0x68. TA sensor/algo startup
still reaches flags `0x13`. The precise required service permission/allocator
path remains to be fixed; successful selector initialization does not establish
that the more restricted fprintd worker can initialize DSP. No enrollment-token
check, physical frame, enrollment, matching or deletion has been reached.

The user will run the finger tests themselves. No further remote capture is
started concurrently with their tests. On `failed to claim device` or
`Remote I/O error`, stop the biometric trial and collect the fprintd journal;
repeated finger presses cannot fix this startup error. All real biometric and
physical-display acceptance remains incomplete.

## Earlier repair diagnostics: generation 22, 2026-11-17

The user authorized deployment/reboot and will perform finger/display trials
after initialization is ready. Generation 22 was deployed over LAN and rebooted;
booted/current system both matched
`/nix/store/ml2fn78ppfjvd1x31zib7xqrhyj71dad-nixos-system-unicorn-26.11.20261010.6510408`.
Generation 21 and earlier rollback entries remain. The system reports running,
and installed PAM configurations still exclude fingerprint authentication.

The corrected method 8 → method 3/command 11 high-frequency sequence, synchronous
method 7 completion, asynchronous DSP initialization and callback decoder all
ran on the phone. `gfuni6` completed sensor/algo and DSP initialization; the
previous DSP timeout did not recur. `gfuni` fails sensor-init transport with
EINVAL and is rejected before fallback. Loader probe/fd warnings still occur,
but do not prevent successful proxy initialization in these runs.

Initial `gfuni6` calibration flags were sensor `0x0`, merged `0x10`, below the
OEM-required `0x13`. Path-only tracing found missing factory calibration files.
The original persist partition was mounted `ro,noload`; 101 requested calibration
files and their available backups were copied into the confined listener tree,
mode 0600, without overwriting any existing file. Each copy was compared with
its source. The original partition was then unmounted. Private bytes were not
copied into this repository or the Nix store. The listener state before deployment
and before provisioning is backed up under the root-only handset directory
`/root/unicorn-fingerprint-acceptance/20261117`.

With factory data present, sensor flags became `0x2`; algo initialization then
hit the kernel's 256-request listener cap (ELOOP), leaving the TA resident until
reboot. A second trace after reboot confirmed changing file names and successful
header/data reads, including 66 preads of 33 distinct paths, before the cap.
No buffer contents were traced. The new kernel patch permits 4096 requests,
limits the entire listener transaction to 30 seconds and stops after eight
consecutive service failures; individual supplicant waits remain unchanged.
The actual patched kernel function passes long-transaction, deadline, count,
failure, wraparound and malformed-response tests. Its hardware outcome is pending.

No capture, enrollment, verification or template deletion has been performed.
The initial isolated TA store contained only `device/oled_wp`; `/var/lib/fprint`
was absent. Calibration initialization subsequently created OTP/flocbase files.
These are recorded separately from fingerprint templates.

The repair also tests first-success candidate ordering/cache fallback and real
fprintd D-Bus behavior: full storage and orphan duplicates preserve templates,
first enrollment does not clear storage, existing fingers require explicit
deletion, and per-finger deletion affects only the requested finger. Runtime
concurrency tests use the real engine, DSP and callback decoder with replacements
only at TEE/FastRPC/device boundaries. Offline results do not establish biometric
acceptance. Final build and handset records below will be updated after the
remaining deployment and trials.

## Deployment and reboot: generation 21

At the user's explicit request, the audited system was copied over LAN,
installed as generation 21, and rebooted. `/run/booted-system` and
`/run/current-system` now both match
`/nix/store/lpya1977vd6pw84ylm6nf8rjss5md73g-nixos-system-unicorn-26.11.20261010.6510408`.
The default EFI entry's kernel, initrd and DT hashes match the built payloads;
generation 20 remains available for rollback.

CDSP started successfully at 23:17:35, and both FastRPC nodes are present.
The user hint is active with zero restarts. Starting fprintd triggers the real
selector: both detectors still accept, but full initialization now reaches the
CDSP proxy and times out with errno 110 for both profiles. The selector exits
78 with zero restarts; fprintd is blocked by that dependency. Fingerprint
recognition is therefore still unavailable. Logs also contain FastRPC reverse
module/file-descriptor errors; their relationship to the initialization timeout
requires further diagnosis. No biometric capture or enrollment was performed.

The system reports degraded because `unicorn-bluetooth-identity.service` also
failed; this is separate from fingerprint initialization.

## Earlier audit evidence

Generation 20 is now booted on the handset. Read-only checks on 2026-11-16
confirmed `/run/booted-system` and `/run/current-system` both reference
`4ghcirz8ck9w7m81swb1kxplph6fja5s-nixos-system-unicorn-26.11.20261010.6510408`.
At 22:19 the kernel loaded signed `cdsp.mdt`, then timed out starting CDSP with
`-110`; all CDSP ready/handover/SMP2P interrupt counts remained zero. CDSP was
`offline` and its FastRPC nodes were absent. The subsequent startup test passed
both Keymaster/detector exchanges but failed full initialization opening domain
3 with ENOENT/EREMOTEIO. No enrollment or verification was reached.

A separate user-service regression was reproduced with the same sandbox
properties and a read-only socket credential probe: the root broker appears as
uid 0 outside the namespace and uid 65534 inside it. The real hint requires uid
0. The selector and hint were both repeatedly restarting in generation 20.

The current audit fixes the missing CDSP NSP-to-DDR bandwidth path, the hint
namespace mismatch, startup retry policy, self-refresh acquisition, reset/error
precedence, callback dispatch and DSP thread synchronization, full serial
identity, queued touch transitions, reply validation and hint handshake budget.
The bandwidth path is a concrete OEM/mainline configuration omission; it is not
yet proven to be the only cause of the CDSP timeout. At audit completion these corrections had not yet been deployed. The deployment
and reboot above supersede that state. No capture/enrollment was initiated. Hardware acceptance remains outstanding.

The final personal system built successfully as
`/nix/store/lpya1977vd6pw84ylm6nf8rjss5md73g-nixos-system-unicorn-26.11.20261010.6510408`.
It includes the cross-compiled ARM64 kernel and runtime. Final rendered user and
system service units were checked; 29 PAM configurations and all 25 installed
PAM files exclude fingerprint authentication. The deployment above uses this build.

Offline results cannot establish a working sensor, acceptable false-match rate,
TA enrollment authorization or device template persistence. In particular,
synthetic TA responses in state-machine tests are explicitly test fixtures.

## Offline checks

The earlier correction enabled CDSP and packaged its original signed main
firmware and process shell. The current `unicorn-cdsp` test additionally checks
the compiled interconnect phandles, endpoint IDs and tags used during boot.
The `unicorn-fingerprint-services` VM runs the real production broker and user
helper with the module's actual service properties; it checks preserved root
credentials across SCM_RIGHTS and the selector's permanent-failure exit policy.
It does not emulate a fingerprint sensor or claim a displayed optical frame.

The [2026-11-16 build record](fingerprint-validation.json) records successful
derivations, output paths and source hashes. Both native and ARM64 runtime
sources were compared with the final working C files and headers.

| Check | Coverage |
|---|---|
| `unicorn-fingerprint-evidence.py` | Hashes of original analysis inputs, 20 signed TA files, command constants, identical 308-entry error tables in both HALs |
| Native transport | Separate Keymaster reply, exact header alias, two payload mappings, private DMA ABI including fd zero, overlap/overflow/truncation, errno versus TEE status, no interrupted-command replay |
| Codec | Both profile lengths/header offsets, packed HAT at +33, bounded group paths, ten-entry enumeration, duplicate rejection and gallery membership |
| Native lifecycle | impl_id discovery, partial allocation/session failures, memory wipe/DONTDUMP and fallback context close |
| IRQ and touch | Merged notifications, cancellation priority, deadlines, device loss, native coordinate scaling, multiple-finger rejection, SYN_DROPPED recovery and close |
| Native DSP | RPC scalars, NOMAP mapping, callback correlation, clock transition deduplication, errors and cancellation |
| Real runtime state machine with boundary fakes | Enrollment commit/failed persistence, missing authorization/display, matching versus acquisition retry, provisional result rejection, independent image reuse, early enrollment finish, complete serial identity, queued finger-up, TA reset and DSP error precedence |
| Real libfprint public API with test-only worker | Async open, enroll progress, verify/identify, list/delete, locator serialization, foreign-gallery/malformed-reply rejection, main-context progress, one retry report, cancellation and subsequent worker recovery |
| Real display descriptor broker with test credential namespace | Roles/UIDs, duplicate helper registration, descriptor transfer with preserved credentials, nonce exchange and disconnect |
| Pinned real listener/storage code | Path confinement, reopen persistence, failed replacement preserving the old object, temporary cleanup and upstream listener/loader tests |
| Actual patched display callbacks | 246 lifecycle scenarios plus real lease ioctl, self-refresh acquisition, off-display refusal, modeset deadlock/error unwind, exclusive fd, deadline, timeout, close and removal; hardware/scheduling are faked |
| Real systemd VM | Production user/broker sandbox, root credentials before/after fd transfer, absent CDSP fails before TEE loading with no restart |
| Nix evaluation | ARM64 target/build platforms, package override, dependency ordering, root-only TEE loading, unprivileged worker/broker, Recovery exclusion and generated PAM files |

Run the helper commands listed in [fingerprint.md](fingerprint.md). Build logs,
GC roots and result manifests are workstation-local under
`~/.local/state/unicorn-build`. The personal configuration check inspects the
actual generated PAM texts, not merely a boolean option. A successful ARM64
kernel and fprintd/libfprint/runtime build is a build result, not biometric
acceptance.

## Remaining handset acceptance

Use a bright, active local Niri/Wayland session. A Wayland session launched from
a local tty is also usable when the authenticated user hint can complete its
real frame handshake. Keep password/PIN authentication available; the test
configuration explicitly disables fingerprint PAM.

1. Deploy the matching kernel, runtime, TA/DSP firmware and fprintd/libfprint
   closure as one reviewed change. Building them does not perform this step.
2. Provision only verified device-private calibration inputs outside the Nix
   store. Keep original persist read-only. If additional TA listener paths are
   requested, establish those paths and copy only their verified private inputs
   into the listener's isolated state. Do not import a prior experiment package.
3. Check `qsee-supplicant`, `unicorn-fingerprint-select` and the user
   `unicorn-fingerprint-hint` journal. Confirm first-success actual-response profile
   selection, CDSP initialization, calibration readiness, correct physical
   region and four-vblank display readiness. Stop on incomplete initialization or
   missing signed enrollment authorization; do not substitute fake responses.
4. Record the initial device templates and local index. Use two vacant finger
   slots, for example `fprintd-enroll -f right-index-finger "$USER"` and
   `fprintd-enroll -f left-index-finger "$USER"`, and list them again. Stop if
   capacity is insufficient; never clear or replace existing templates. Record
   reported progress and acquisition retries. A TA authorization error is an
   outstanding dependency, never a successful enrollment.
5. Run `fprintd-verify -f right-index-finger "$USER"` for 20 enrolled-finger
   trials and 20 genuinely unenrolled-finger trials. Record each physical
   attempt separately; an automatic CLI retry does not count as an additional
   successful trial.

Use this record format, with one row per physical attempt:

| Trial | Finger class | Result | Elapsed ms | Retry/status | Profile/build |
|---:|---|---|---:|---|---|
| 1–20 | Enrolled | success / non-match / acquisition failure | | | |
| 1–20 | Unenrolled | non-match / acquisition failure / **false match** | | | |

An unenrolled finger producing an accepted match fails acceptance immediately.
Also verify an enrolled finger that is outside the current request's template
set: it must not authenticate that request. Keep acquisition failure, no-match
and transport/device failure as separate counts.

## Persistence, cancellation and recovery

After successful real enrollment:

- Restart fprintd and the fingerprint services, then verify the enrolled finger.
  Confirm that the isolated TA state survives and that a fresh worker starts.
- After the authorized phone reboot, verify again. This is the cold-start
  Keymaster/calibration/template-persistence check; no reboot is performed by a
  build or by the offline suite.
- Cancel while waiting for a finger, during display readiness and during
  acquisition. Kill a native worker during capture. The kernel must end local
  HBM within its lease; no stale IRQ, power reference or overlay may remain.
  Confirm that the selector waits for process exit and reloads the TA before a
  subsequent worker uses a new carveout.
- Restart the supplicant during an operation. The current operation must fail
  explicitly, release the display and recover through a fresh loader/client
  lifetime. A failed secure call must not be replayed as a completed command.
- Exercise brightness changes and the existing display lifecycle with capture
  active. Check deferred brightness restoration, idle/mode serialization, VT
  ioctl behavior and explicit physical display recovery. If DSI off fails, the
  panel must be reset dark; this is distinct from compositor IPC state.
- Delete each test finger with `fprintd-delete "$USER" -f right-index-finger`
  (using its actual finger name), confirm list removal, and verify that the
  deleted template cannot authenticate. Repeat list/verification after service
  restart to ensure deletion persisted.

Do not delete unrelated users' templates to simplify testing. Retain only
status/timing/build records; do not collect raw images, HATs, calibration bytes
or sealed template contents in logs.

Biometric and physical display acceptance rows remain **not performed** until
physical finger trials are completed. Deployment and initialization diagnostics
are recorded separately above.
