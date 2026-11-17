# Unicorn OEM protocol evidence

These layouts come from the two hash-pinned AArch64 HALs and their companion
FastRPC stubs. They establish what the native implementation sends; they do not
prove that a handset has accepted an operation. Audit the original inputs with
`python3 tools/unicorn-fingerprint-evidence.py`. The manifest is
`pkgs/unicorn/fingerprint/evidence.json`; originals and local decompilations
remain under ignored `.work/unicorn/fingerprint`.

`libgf_hal.so` is SHA-256
`d86b2d924a6e8528e10bff215145bc9ddb6d75b8d9dec784faa82ec653f692c8`;
`libgf_hal6.so` is
`6d30affa726f88772c7d1a78b4a9fc17761d1ac2a860cb1f385f71d0a26eb853`.
ELF addresses below are original addresses, without the decompiler image base.
The 308-entry error tables in both files are identical. Their numbers are not
interchangeable with a `gfenu` driver's enum.

## Transport

Both HALs send a 64-byte header and receive the response **at the same address**.
`CaEntry::sendCommand` is at `0x3f60c` / `0x39530`. A descriptor patches the
payload physical address at header offset 0 and a persistent carveout at 32.
The original descriptor width is four bytes. The GFUNI6 address fields are eight
bytes with zero upper halves; the GFUNI header has adjacent u32 fields instead.

| Header field | GFUNI offset | GFUNI6 offset | Bytes |
|---|---:|---:|---:|
| Payload address | 0 | 0 | 4 patched |
| Payload length | 4 | 8 | 4 |
| Sequence | 8 | 12 | 4 |
| Milliseconds since local midnight | 16 | 16 | 8 |
| Log level, zero | 24 | 24 | 4 |
| Header status | 28 | 28 | 4 |
| Carveout address | 32 | 32 | 4 patched |
| Carveout length | 36 | 40 | 4 |

The carveout is 4 MiB, allocated once per native worker. It is exported for
FastRPC NOMAP access and is never CPU-mapped in userspace. Its kernel context
retains a reference through TA session teardown. Ordinary payloads are copied
into coherent kernel-only staging and copied back; exact header aliasing is
preserved and patched physical addresses are removed from replies.

The local kernel extension uses invoke function `0x55460001`, a valid client
session and no parameters. Its ioctl return value is **fd + 1**, avoiding output
copy faults after fd installation and representing descriptor zero correctly.
A private-address patch is a value pair: `(offset,4,1)` and `(0,0,0)`. It can name
only that context's private buffer. Ordinary patches retain the existing
`(offset,width,0)` plus MEMREF_INOUT form. Bounds, descriptor overlap, address
width and aggregate staging length are checked separately.

Payload byte 0 requests reset; module u32 is at 4, command u32 at 8, command
i32 status at 12. Remaining common bytes through 27 are initialized to zero.
A reset request aborts the operation; mutating commands are never blindly
reissued. ioctl errno, `ret/ret_origin`, header status and payload status are
separate error layers.

## Keymaster and initialization

The external Keymaster request is `(517,2)`: eight bytes zero-padded to 64.
Its separate 256-byte response begins at shared offset 64. The HAL checks its
leading status and copies the entire opaque response into `(1000,10)` at
payload offset 28. Words 1/2 are not interpreted as a token offset/length pair.
The response is wiped after installation and never logged or serialized.
Earlier records establish that `keymaster64` was already resident. The loader
attaches first and reports an explicit dependency failure if it cannot acquire
that application; this package does not replace Keymaster firmware or keys.

Startup order follows `HalContext::init`: power/reset, TA/auth-key setup,
carveout allocation, wake, detector init/info, start the CDSP initialization
thread, sensor init and algorithm init on the caller thread, join DSP readiness,
screen-on state, cancel stale work, sleep. `HalDsp::init` starts a thread at
`0x3fdbc` / `0x39c74`; its thread loop is `0x413bc` / `0x3b174`. The caller must
not block in DSP initialization before advancing TA initialization. Detector error 1112 permits at
most three reset/retry attempts, as in the OEM detector. A successful detector
response must identify optical type 3 and a supported chip code; filenames are
not sensor identification.

Customized sensor init is 616 / 640 bytes. Product 14, display vendor 8 and DSP
flags are at extension offset 600 / 624. Configuration is returned at +32,
length 416 / 440; sensor kind, calibration flags and count follow it. Only the
single-sensor flow fitted to Unicorn is accepted; malformed counts/IDs fail.
The optional 64-byte OTP input starts at extension−107, with its length at
extension−40. Sensor ID length/data are at extension−116 / extension−136.

Customized algorithm init is 560 bytes in both profiles. DSP flags are at +40;
`ro.boot.oled_wp` starts at +44 with its length at +556. Returned calibration
flags at +36 are merged with sensor readiness; bits 0, 1 and 4 must be set
before acquisition. Neither missing calibration nor a rejected OLED identity
is silently replaced with zeros and called successful.

## Commands and result fields

Lengths are the actual Delmar/customized call arguments. Base-class capture or
algorithm layouts are not used.

| Operation | Module, command | GFUNI bytes | GFUNI6 bytes | Fields after common header |
|---|---|---:|---:|---|
| Install Keymaster reply | 1000,10 | 284 | 284 | Opaque 256 bytes at +28 |
| Wake / sleep | 1002,9 / 8 | 32 / 28 | 32 / 28 | Zero-initialized wake argument |
| Detector init / info | 1002,0 / 1 | 28 / 44 | 28 / 44 | Info at +28, 16 bytes |
| Sensor init | 1002,2 | 616 | 640 | Profile-specific extension above |
| Algorithm init | 1001,0 | 560 | 560 | DSP flags/OLED identity above |
| Set group | 1000,0 | 288 | 288 | Group +28; bounded path +32[256] |
| Pre-enroll | 1000,2 | 40 | 40 | Challenge returned at +32 |
| Enroll | 1000,3 | 108 | 108 | Group +28; packed HAT +33[69] |
| Post-enroll | 1000,4 | 28 | 28 | No fabricated HAT |
| Start authentication | 1000,1 | 48 | 48 | Group +28; challenge +32 |
| Enumerate | 1000,7 | 112 | 112 | Count +28; groups +32[10], IDs +72[10] |
| Remove | 1000,6 | 120 | 120 | Group +28; ID +32 |
| Save | 1000,9 | 36 | 36 | Group +28; ID +32 |
| Cancel | 1000,8 | 28 | 28 | Serialized with the current operation |
| Screen state | 1000,11 | 36 | 36 | Screen-on +28; auth state +32 |
| Capture | 1002,3 | 80 | 88 | Retry +32; mode +36; sensor IDs +44 |
| Read image | 1002,101 | 88 | 88 | IDs +32; area flag +40; coordinates +44 |
| IRQ event | 1002,5 | 32 | 32 | Event enum returned at +28 |
| Calibration load error | 1002,103 | 28 | 28 | Read cached TA failure code when calibration is not ready |
| Update sensor IDs | 1001,22 | 48 | 48 | Matched/study IDs at +32/+40 |
| Enroll image | 1001,1 | 176 | 184 | IDs +48; raw flag +56; progress u16 +40 |
| Authentication image | 1001,2 | 240 | 240 | Retry +28; IDs +144; match group/ID +44/+48 |
| Finish enrollment early | 1001,24 | 40 | 40 | Group/ID +28/+32; remaining u16 +36 |
| End enrollment sample | 1001,29 | 40 | 40 | Group/ID +28/+32; temperature/remaining at +36/+38 |
| Post-authentication | 1000,12 | 52 | 52 | Retry +28; study ID +32; group/ID +36/+40; phase +48 |

Relevant GFUNI6 overrides include sensor init/capture/read at
`0x9c188/0x9c5bc/0x9c81c`, algorithm init/enroll/auth at
`0x98f08/0x990bc/0x994fc`, enumeration notification at `0x3f148`, enrollment
completion at `0x9a7dc`, and split post-authentication at `0x9b878`. The vtables
select these implementations; the smaller base-class packets are insufficient.

Enrollment reports decreasing remaining samples, processes the requested
additional duplicate/fake check, and honors the TA's early-finish indication
after at least two presses. A final save must be visible to a fresh enumeration
before success is returned. The optional high-light fallback uses the OEM
simple-enrollment branch. Cancellation/post-enroll runs on authorization and
acquisition failures too.

Authentication retains its per-press context, bounds retries, honors image reuse
for independent fast authentication, and completes the post-auth phases. Byte
169 is a **provisional** result: status zero with that byte set cannot become an
accepted libfprint match. A final result must identify a member of the requested
gallery. Code 1008 is a non-match; acquisition retries and transport/DSP errors
remain different outcomes. DSP failure cannot be hidden by a simultaneous TA
non-match response.

## Enrollment authorization and the first CLI stage

Both original HALs use a 108-byte `1000,3` packet. Their C++ `enroll` functions
(`gfuni` at `0x427e4`, `gfuni6` at `0x3c49c`) leave the token area zero when the
input HAT pointer is null. With a HAT, 69 packed bytes are copied at +33. The
pre-enroll functions (`0x42508`, `0x3c1c0`) return the TA challenge at +32 of their
40-byte packet. Generations 25–26 matched the null-pointer branch and had no
signing provider. The current source requires a genuine Gatekeeper HAT; it
does not fall back to the rejected no-token request.

Generation 25's actual user enrollment reached pre-enroll successfully, then
`1000,3` returned `GF_ERROR_INVALID_CHALLENGE` (1058). No `1002,3` capture,
`1002,101` image read, algorithm enrollment or save followed. This establishes
that the installed TA rejects the current no-token request; the existence of a
null-pointer branch in the AP library does not establish TA authorization.
A legitimate provider's enrollment path still needs hardware qualification.
Keymaster command 517/2 installs shared TA auth-key
material; its success is not a signed enrollment HAT. An untrusted or timed-out
HAT (1059/1142) is likewise terminal. These three codes become permission errors,
with no capture, progress, replay, save or delete in the worker regression tests.
Diagnostics record only challenge presence, token source and status, never
challenge/HAT bytes.

### Gatekeeper provider, 2026-11-17

The exact stock vendor binaries and decompilation entry points are pinned in
`pkgs/unicorn/fingerprint/evidence.json`. The stock property
`vendor.gatekeeper.disable_spu=true` selects the TEE path. The C client attaches
to resident `keymaster64`; it does not load or replace a secure app. The observed
query reports Keymaster API 4.1, TA 4.1012. The read-only hardware probe sent only
`0x200` and `0x207` and succeeded; it did not create a credential or obtain a HAT.

The `libkeymasterdeviceutils.so` constructor at `0x51e0` uses a 0xa000-byte
buffer. Query `0x200` has a four-byte request and an immediately following
response containing signed status and four version words. The `0x207` request
is six little-endian words `(0x207,4,5,4,5,0)`; its response starts at +24.
The first four request words are independently audited at file offset `0x2e00`.

The Gatekeeper HIDL implementation's `enroll` at `0x7c6c` sends `0x21001`
and a CBOR map `{3:uid, 8:desired_password_bytes}` for a new credential. Verify
`0x21002` sends `{3:uid, 4:challenge, 6:handle, 9:password_bytes}`. Both responses
start at the four-byte-aligned request end: signed status, CBOR length, payload.
The actual request length is **unpadded** (`sendCmd` at `0x583c`: x2 is encoded
CBOR size + 4; x3 is base + the aligned length). Generation 27 accidentally sent
the alignment bytes as input. Its first real user setup returned positive
status 16. The stock `libqcbor.so`'s `QCBORDecode_Finish` export at `0x5048`
jumps to `0x4c00`, which returns 16 for a valid decoder with unconsumed bytes.
Generation 28 was deployed and rebooted. Its real new-enroll request is 46 bytes
with response at +48; the user's trial now returns -30. This confirms a changed
TA result after correcting transport, but no credential or signed HAT exists.
Do not assume that the earlier positive 16 proves a password failure or secure
throttling.
Negative status is a rejection; positive status is a throttling delay in ms.
No operation is retried automatically. A transport failure invalidates the
session. **`0x21003` is delete-user** (`0x831c`), not a harmless query; the
provider never sends it.

The handset's signed `keymaster_a` and `keymaster_b` code partitions are identical
(SHA-256 `453cd884fcc26c562597a0a93a79a9824da36a6d902297cee21b9821d9688ef0`).
Their ELF-relative addresses, below, subtract Ghidra's default `0x100000` base.
The command table at `0x505b0` maps `0x21001` to `0x46a0`, decode to `0x21ae8`.
New-enroll generates its own SID/key and hashes arbitrary desired-password bytes;
there is no discovered 32-versus-64-byte password requirement to fix by trial.
User-record lookup at `0x14294` returns NULL if storage initialization fails or
no record is available (46 records). New-enroll maps NULL to -30. Storage init
at `0x13b70` reads the store through `0x19f84`, derives the hardware HMAC key,
and verifies every occupied record; a MAC mismatch returns -30. Other storage
errors are masked by the same lookup result. These are possible branches, not
proof that the handset has corrupt records or exhausted capacity. GET_VERSION,
SET_VERSION and NEW_ENROLL all share the `0x100` open-storage guard, so their
successful negotiation does not establish successful record reading/integrity.

Do not use command `0x202` as a harmless status probe: the signed TA implements
it as an export of private secure-storage sectors. Do not issue DELETE_USER,
DELETE_ALL, change verified-boot/RootOfTrust state, or reset Android credentials
to diagnose this failure.

A diagnostic experiment registered the legacy QSEE 32-KiB log ring (SCM owner
50/service 1/command 6) with a root-only parser that exposed only whitelisted
function/line/status metadata. Its 33 source-extracted parser/lifecycle scenarios
passed, but the first real ring-header read caused a synchronous external abort
in `qseecom_gatekeeper_errors_show`. Registration success did not establish CPU
read access on this handset. The process died before the planned read-only
GET_VERSION size-error probe; no new credential command was issued. The system
was rolled back and rebooted into the preceding stable kernel. The patch and its
fixture were withdrawn from the source/build queue; failure evidence is kept in
the ignored investigation directory. This is a failed hardware experiment, not
a working diagnostic path. Encryption or memory ownership as the underlying
reason has not been established. Do not re-enable this legacy ring method.

The new HAT parser at `0x9af0`, used from TA 4.540, reads CBOR keys
`11,4,12,13,14,15,16` as version, challenge, secure user ID, authenticator ID,
type, timestamp and 32-byte MAC. Key 11 is the HAT version, not a status.
Their integer storage is copied into packed offsets `0,1,9,17,25,29,37`.
The type and timestamp integers already represent the network-order bytes;
adding another byte swap would corrupt them. The C decoder verifies the exact
field set, widths, challenge, handle SID, type and version and rejects malformed
or trailing bytes. Only the TA signs; offline test MACs are transport fixtures.

The local companion tool validates the user's current Linux login password
through the existing `login` PAM stack, without changing PAM configuration or
enabling fingerprint login. It derives 32 credential bytes with scrypt
`N=32768,r=8,p=1`, a random 16-byte salt and a 64-MiB limit. The Gatekeeper UID
is `0x55460000 | linux_uid` (Linux UID at most 65535), separate from Android's
ordinary user IDs. Only salt and the opaque handle are stored, atomically and
without replacement, under root-only `/var/lib/unicorn-fingerprint-authorization`.
Any failed credential-creation call leaves a durable pending marker, including
a returned TA error: the signed TA has failure paths after a secure-record write.
The tool refuses another setup before asking for a password. A TA status alone
does not prove that storage stayed unchanged. The generation 28 failure was
quarantined with a root-only pending marker; its lost local derivation salt and
absent handle must not be repaired by replay or a credential reset.

For each enrollment the tool starts a one-shot, root-only Unix socket, launches
the existing `fprintd-enroll`, and verifies its fresh pre-enroll challenge with
Gatekeeper. The root worker and provider check peer credentials; the provider
also checks the installed runtime executable. The returned 69-byte HAT stays
outside worker IPC and D-Bus. Absence, cancellation, throttling and malformed
responses fail before Goodix enrollment/capture. Credential buffers are locked,
core dumps disabled and transient passwords/HATs wiped. Daily fingerprint
verification needs no password or provider. Password changes do not silently
reset Gatekeeper: trusted credential migration is not implemented yet.

The Linux transport must preserve that gap too. The staging-layout patch keeps
forward disjoint offsets within one SHM object, exact Goodix aliases, and exact
input lengths. Its bounded extent includes the response gap; ordinary patched
buffers start after the page-aligned extent. Partial/reversed same-SHM layouts
are rejected, and an excessive gap or aggregate size fails before allocation.
Production kernel `memref`, patch-validation, layout and invoke functions are
extracted into a native regression fixture, with replacements only for memory,
session and SCM boundaries. Tests assert 46-byte input/+48 response and 117-byte
input/+120 response, alias address scrubbing, ordinary/private patches, error
cleanup and bounds. Runtime codec tests now assert those independent literal
lengths; the earlier test incorrectly expected an aligned input length.

The pinned fprintd `enroll_identify_cb` emits `enroll-stage-passed` after its
preliminary duplicate check, before calling the driver's enrollment method. An
empty Unicorn gallery completes that check without acquisition. Consequently the
first CLI stage alone is not proof of a scanned fingerprint. fprintd's existing
D-Bus status vocabulary maps permission errors to `enroll-unknown-error`; its
journal contains the explicit TA authorization error. The interface vocabulary
is preserved.

## Native CDSP bridge

The pinned `libhvx_proxy_stub.so` establishes the URI:

```
file:///libproxy_skel.so?proxy_skel_handle_invoke&_modver=1.0&_dom=cdsp
```

| RPC method | Scalars / input |
|---:|---|
| 2 | Init, no buffers |
| 3 | One 4-byte command buffer |
| 4 | One 8-byte buffer: command 10, chip type |
| 5 | One 4-byte command 6 plus one DMA handle; NOMAP attribute `0x10` |
| 6 | Deinit, no buffers |
| 7 | Disable clocks; synchronous return, no notification 7 |
| 8 | 16-byte clocks request `(0,1000,0,69)`, then method 3 command 11 |

HAL DSP work 1/2/3/6 maps to proxy commands 2/3/4/13. Completion is correlated by
command and bounded by a cancelable deadline. The AP proxy's high-frequency
function (`0x194c`) invokes method 8 followed by command 11 and returns the
command's synchronous status. Normal frequency (`0x1a74`) returns method 7's
status. Neither caller waits for a clock notification. Linux fails the session
if either high-frequency RPC fails rather than hiding method 8's error as the
OEM wrapper can. Notification 11 is consumed without arming a completion waiter.

The native `libarm_proxy_skel.so` accepts at least eight bytes for notification
method 0 (`0x870`) and at least four header bytes for log method 1 (`0xa28`).
Only the fixed fields are parsed. Buffer counts/directions and variable log
lengths remain checked; log contents are discarded. The first expected
completion wins over duplicates. A timeout or cancellation invalidates the
session, so a late callback cannot satisfy a retry. The protocol has no request
sequence in callbacks; a completed command's duplicate arriving during a later
same-number command cannot be independently identified by this ABI.

`uf_dsp_start`, `uf_dsp_ready`, and `uf_dsp_close` manage startup, readiness,
failure and closing internally. Close wakes notification waiters and joins the
initialization thread before releasing the remote handle, library and DMA.
Kernel RPCs cannot be safely canceled by freeing their arguments: a 30-second
selector child watchdog and the worker parent's existing deadline bound them
at process level. Every candidate runs in a fresh child, reaped before its TA
is unloaded. Linux uses eventfds/pthread synchronization instead of Android
Thread/semaphore objects. Both Android AP-side libraries remain analysis inputs.

The FastRPC 1.0.7 listener decodes reverse-call argument buffers, dispatches
`arm_proxy_skel_invoke`, and writes the runtime eventfd through `uf_dsp_notify`.
`apps_std` scalar `0x13050100` is method 19, `fopen_with_env`; errno 2 can be a
library search miss. Scalars `0x09010000` and `0x04010200` are `fseek` and `fread`;
EBADF is a failed descriptor operation, not proof of a working callback. Older
logs subsequently open `libproxy_skel.so` and `arm_proxy`, so those particular
messages did not prevent library loading. Whether they impede initialization
must be checked against the new per-RPC and expected-notification diagnostics.
The domain-support probe errors fall back to the legacy listener API.

The private DMA registration uses `FASTRPC_ATTR_NOMAP` (0x10). FastRPC consumes
and re-registers the handle when marshaling method 5; remote-handle/domain
teardown releases mappings. TA session/context teardown precedes closing the
last local DMA fd. Candidate process isolation prevents stale library handle
registrations from surviving into another candidate.

The packaged DSP binary is SHA-256
`050dab21f7ba7d59008df52a67fd3aa5cadc4374373c0227614299fcdc5f52ef`.
Its DSP libc/libgcc imports are runtime dependencies of the CDSP image; the
original vendor RFSA directory does not supply separate copies. Real domain
startup, calibration readiness, authorization and biometric response traces
remain handset acceptance items.

## Selection and template preservation

`mfp-daemon` openHal (`0x8058`, cache probe `0x8354–0x83cc`, fallback
`0x84d8–0x8644`) accepts the first full HAL open success. Linux tries the last
successful name first, then the other candidate; without a valid cache the order
is gfuni, gfuni6. Every candidate must pass detector, TA/DSP initialization and
calibration bits 0/1/4. No capture or template mutation selects a candidate.

Only a candidate name is cached, using fsync and atomic replacement in the
separate root-only `/var/lib/unicorn-fingerprint-selector`. Invalid caches are
ignored, and a failed cache write does not reject a qualified candidate. The
runtime profile is published only after success; all failures exit 78 without
restart. Workers keep the selected TA until their process has exited.

For the `unicorn` driver fprintd does not garbage-collect unknown templates when
full, delete an orphan duplicate, clear storage before first enrollment, or
implicitly replace an already enrolled finger. The explicit per-finger deletion
API remains. Empty-gallery identification completes with no match, allowing
fprintd's first-enrollment check without authorizing any template. Existing
signed-token/no-token enrollment branches remain unchanged: TA authorization
failure is a blocking dependency, never bypassed.

## Calibration and listener transaction bounds

The gfuni6 base enrollment-readiness check at `0x9a054` requires sensor bits
0/1, and the customized check at `0x4e968` additionally requires bit 4 (OEM
identity match). Sensor-init (`0x9c188`) places the 440-byte configuration at
32, sensor kind at 472 and calibration flags at 476. Algo-init (`0x98f08`)
merges the response flags at 36. Linux requires the same `0x13` mask.
On failure it queries the OEM cached error command (`gfuni` at `0xe5c24`,
`gfuni6` at `0x9cdb0`; constant pair at `0x1f2c8`) and preserves transport/reset
failure precedence. The query does not initiate calibration or change readiness.

On this phone the isolated listener lacked the factory files requested beneath
`/mnt/vendor/persist/goodix`. Path-only tracing established those requests;
verified originals were copied from a read-only, no-journal-replay persist mount
into the root-only listener state. Existing files were preserved. No template
directory or arbitrary Android user data was imported.

With these inputs the signed TA uses more than 256 listener exchanges in one
algorithm-init command, for metadata, headers and data of distinct calibration
files. The Linux-only 256-round guard returned ELOOP during successful reads and
left the TA unable to unload. `qseecom-listener-budget.patch` replaces that guard
with 4096 total requests, a 30-second transaction deadline, and eight consecutive
service failures. A single service wait retains its existing timeout, so a wait
already in progress can extend beyond the transaction deadline by that timeout.
The selector watchdog also bounds the owning process. The original SCM calls
and secure-world failure responses are unchanged; no uncertain mutation is
retried. Reaching a limit invalidates the session, and a stuck secure app may
still require reboot. Counts and elapsed times are logged without buffer data.

## fprintd allocator device access (2026-11-17)

The native worker loads `libadsprpc.so` and opens the proxy URI on CDSP. Its
shared RPC allocations still use `DEFAULT_DOMAIN_ID` (ADSP): the pinned
`coherent-rpcmem.patch` deliberately uses `open_device_node(DEFAULT_DOMAIN_ID)`
instead of unsynchronized cached system-heap buffers. Therefore the worker's
device policy must allow `/dev/fastrpc-adsp rw` as well as the CDSP nodes.

With only CDSP allowed, the real fprintd worker reports EPERM opening domain 0,
then process-shell allocation fails with both allocator fds -1 and remote open
returns 0x68. The privileged selector can initialize successfully in the same
boot, so its readiness alone cannot cover this service integration failure.
Adding exactly the ADSP device to fprintd's allowlist fixes repeated real D-Bus
Claim/Release cycles without adding capabilities or disabling other sandbox
restrictions. The Nix configuration check asserts this allocator dependency.
