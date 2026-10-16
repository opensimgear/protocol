# Local cross-firmware integration

## Development status and disclaimer

This project is heavily under development and is not ready for use. Use it entirely at your own risk. To the fullest extent permitted by applicable law, the authors and contributors disclaim all liability for any damage, loss, or other issues arising from its use.

The optional runner builds the actual gateway core/registry/layout worker, generated Cyphal
payloads, libcanard transport and generic CAN client/delivery with a local pinned
firmware-common input implementation.
It requires adjacent `device-gateway/` and `device-template/` checkouts, the pinned common source and a host C compiler. It never
fetches a branch, flashes a board or changes an existing firmware checkout.

```sh
mkdir -p .research
TMPDIR="$PWD/.research" python3 protocol/tests/run_gateway_generic.py \
  --workspace /path/to/opensimgear \
  --common-source /path/to/pinned/firmware-common \
  --sanitize
```

When omitted, `--common-source` uses
`.research/generic-pinned-arm-example/firmware_common-src` below the workspace.
The runner verifies both device `shared-cyphal` snapshots match every canonical
file before linking the gateway copy. Compilation uses C11,
warnings as errors and enabled assertions; the optional sanitizer flag enables
AddressSanitizer and UndefinedBehaviorSanitizer. Temporary build files live in
the workspace `.research/` and are removed after execution. No unpublished
remote commit is required.

The test drives real debounced contacts, eight ADC axes, four hats and a
quadrature encoder with 128 logical buttons. Two actual Cyphal instances exchange
CAN frames through discovery/assignment, automatic enrollment, exact registry serialization/publication and full input
delivery. ACKs come from the gateway's successful USB interrupt completion
function, then pass through the generic client's history/lease checks before
crediting encoder phases. Submission, corrupted completion, stale ACKs and
elapsed time alone cannot release a pending encoder press/release. Direct USB
ownership neutralizes gateway input and invalidates pending delivery; fallback
rearms contacts and requires fresh input. Bus reset rejects stale leases,
temporary absence retains neutral retained controllers, and registry restoration
preserves their identities.

Disable and Re-enable publication renew the changed device's lease, rearming
the real generic acquisition state so encoder turns collected while disabled
cannot appear later. The ten-client lifecycle scenario waits for the ordinary
450–550 ms jittered Announcement and a subsequent fresh input, asserting neutral
gateway reports while the client still has its old lease. It also checks
append-on-Re-enable, unchanged surviving descriptor bytes/report IDs, offline
ghost retention and removal after the discovery timeout.

This verifies portable firmware interoperability. It does not establish actual
CAN arbitration, USB enumeration, interrupt delivery, physical wiring or flash
power-loss behavior; those require their own board and journal checks. Canonical
generated payload and transport tests live under `../cyphal/tests/`.

The runner also executes `panel_events_integration.c`, linking the real common
event collector, generic configuration envelope handler and gateway forwarder.
It captures a debounced press/release without HID ownership, transports baseline
and pages through sealed Cyphal fragments, checks immutable cached reads after
later input and routes fresh pages with direct USB active. It fills the 64-record
ring, validates explicit gap/16-record pagination, resets the stream on runtime
activation and rejects stale-stream reads. Settings are not claimed or mutated
by reads. The Master Caution native/board profiles share logical control 0 and
`led.status.set_mode`; physical validation remains deferred.

The same fixture now transports additive contact/selector/axis metadata, applies
the real runtime calibration transform to its independent axis report, verifies
inverted endpoints and stale neutral/availability, and fragments complete ASCII
display setters through the sealed command envelope. It checks declared geometry,
rejected encoding, readable display state and immutable invocation retries.
Assignments renew through the real CAN transport while the larger fixture runs.
Common native tests separately exercise UTF-8 scalar/cell/byte bounds and maximum
metadata/page sizing. None of these software tests establish physical displays.

The same runner also executes `multi_device_integration.c`: ten independent real
device CAN clients broadcast through a deterministic frame queue to one actual
gateway core and transport. Fault injection selects a sender and Cyphal subject,
then drops or delays a fixed number of emitted frames; reverse queue delivery
exercises reordered traffic. Every other endpoint receives broadcast frames.
The suite checks lost announcements and assignments, delayed discovery, older
input arriving after newer input, delayed and dropped completion ACKs, isolated
input freshness loss, offline/reconnect recovery and stable per-identity descriptor order and report IDs.
Device reboots destroy and reinitialize the client with an incremented boot
session, testing both a changed CAN source and reuse of the original source.
Gateway reboot restores the encoded registry and advances the simulated persisted
epoch. Retained old input/ACK frames must not credit the rebooted device, and old
input must not activate restored gateway controllers before fresh discovery and input.
Recovery is checked over bounded simulated time; CAN transfer-ID suppression
means recovery after loss or source reuse need not happen on the first frame.
The multi-device suite supplies distinct valid button/axis reports directly;
the existing single-device suite continues to exercise acquisition and encoder
delivery. Neither suite executes STM32 startup, HAL, TinyUSB or physical flash.

Additional ten-device scenarios inject a real standard Heartbeat from a rebooted
client at an occupied device or gateway CAN node ID. They check libcanard address
repair, route invalidation, renewed leases, stable controller order and report IDs, rejection of
retained input/ACKs and stale USB completions, and source-bound messaging recovery.
Device collisions preserve unaffected clients' leases. Gateway collision handling
mirrors the board loop's transport reset and lease revocation boundary; clients
learn the new gateway source after their current assignments expire. Old ACKs are
retained until after that rediscovery, since clients cannot infer a remote source
migration before observing it. This tests protocol behavior, not physical CAN
arbitration. Direct USB takeover/release checks neutral fallback, rejected old
delivery, lost first fresh input on release and continued delivery for the other
nine clients. These scenarios use existing contracts without creating a release.

The runner also executes deterministic randomized stress for seeds `0x13579bdf`,
`0x2468ace1` and `0xc0ffee01`. Each seed runs twice and must produce the same
frame/event trace digest. Separate xorshift32 streams drive the event schedule
and wire faults. The harness normalizes libcanard's initial address-mixed PRNG
state after construction/reset so ASLR cannot alter replayed collision choices;
the firmware and vendored transport are unchanged. Each run combines 80 seconds
of simulated activity with roughly
12% frame loss, 28% delayed frames (10–400 ms), reversed delivery order, device
and gateway reboots, occupied CAN node IDs and temporary direct USB ownership.
Of frames surviving wire loss, roughly 15% are duplicated with an independent
0–80 ms extra delay and 10% are corrupted. Corruption rotates through an invalid
extended-ID flag, illegal CAN FD length, invalid single-frame toggle, and invalid
application UID/route fields. Rejected traffic must never become gateway input
or a successful client ACK. All four corruption modes and duplicate/corrupted
traffic from every device and the gateway are required in each seed.

CAN driver backpressure returns `false` before wire admission, retaining frames
in real libcanard queues. Each endpoint gets two transmit credits per 10 ms tick
and interleaved blocked periods of 50–250 ms. A 150 ms blocked startup burst
fills all eleven 16-frame transport queues, verifies failed admission without
advancing client input sequences, and forces deadline expiration. Coverage checks
require saturation, admission failures, driver refusals and expiration on every
endpoint, plus successful immutable retries. Retry bytes and deadlines are
checked; expired frames are consumed without wire delivery, matching the board
driver's queue-discard behavior. Input provenance follows queued frame bytes and
ACK provenance follows retained successful USB snapshots even when emission is
delayed by pressure.

Bus-off scenarios inject the board's reported recovery state on all ten device
controllers and the gateway. CAN transmit admission and receive are disabled
during recovery. The harness mirrors firmware main-loop boundaries: a device
resets its client route on entry, while the gateway resets transport and revokes
all leases on entry and on successful controller restart. Boot sessions and CAN
source IDs survive these resets. Queued real application transfers must be purged;
every controller experiences one or two failed restart attempts at the board's
1-second retry interval before success. Recovery can overlap other faults and
continues naturally after new fault injection stops.

Disconnects block both CAN directions without revoking a gateway route directly.
Five devices keep running while unplugged; five stop ticking and reboot into a
new boot session on reconnect. Every device gets an initial 2.2–3.2-second absence
that must expire gateway inventory and expose a neutral report by 2 seconds,
allowing for already-departed delayed frames. Later disconnects also include
100–400 ms absences that overlap delayed traffic. Wire emission and accepted ACK
counts must remain unchanged while an endpoint is unavailable. Delayed frames
already on the wire remain in flight for other endpoints; frames received while
offline are discarded rather than replayed on reconnect. Enabled controllers
remain stable through absence and rediscovery.

A shuffled initial sweep guarantees a reboot, collision injection and USB
takeover/release, bus-off recovery and disconnect/reconnect for every device;
coverage checks additionally require actual loss/delay and acknowledged messaging
for every device, gateway reboot/source migration/bus-off recovery, failed
restarts, purged transmit queues, suppressed receives and offline expiration.
USB completions are delayed by 10–200 ms,
including snapshots retained across ownership/session changes.

Checks during the fault phase cover stable controller order and report IDs, unique live gateway
routes/sources, bounded transport pools/queues, neutral unavailable/USB-owned
controllers, source/session/lease and sequence guards, identity-specific report bytes,
and ACK provenance from an immutable successful USB completion. Invalidated USB
snapshots must not produce ACKs. After faults stop, pending traffic remains and
all ownership is released. Scheduled controller restarts and reconnects still
run, and their completed counts must match fault-entry counts. Within a further
35 seconds of simulated time every
device must recover discovery, current source/session binding, exact input
reports and successful completion ACKs. All transmit queues must drain, and
transport destruction must return every fixed-pool block. The test uses simulated time, without
wall-clock sleeps, and is automatically included in the existing CI runner.

Failure diagnostics include seed, simulated time and event index. To replay
one seed, append `--stress-seed 0x13579bdf` to the runner command above; it still
runs the focused suites and repeats the selected stress seed twice. This stress
test checks portable behavior and does not emulate CAN arbitration, STM32 HAL
or physical USB. Bus-off is a simulated controller state, not electrical fault
injection, error-counter validation or execution of the HAL restart routine.
Corruption targets detectable malformed traffic: arbitrary
valid single-frame CAN FD payload changes have no Cyphal transfer CRC, while
physical CAN CRC rejection is outside the harness. Its fixed corpus is bounded coverage rather than exhaustive
fault exploration.

GitHub Actions runs the integration suites with AddressSanitizer and
UndefinedBehaviorSanitizer on pushes, pull requests and manual dispatches in
`protocol`, `device-template` and `device-gateway` through each repository's
`.github/workflows/cyphal-integration.yml`. The triggering repository uses the
event checkout (including the pull-request merge); companion revisions are
declared by each workflow or its repository-owned dependency manifest. Both firmware common pins must match, and that exact common commit is
checked out. The runner also enforces matching canonical Cyphal snapshots.
Coordinated protocol migrations therefore require compatible companion revisions
and dependency pins before all three checks can pass. All CI checkouts and scratch builds
live under `.research/ci/`; sanitizer findings fail the job.

On 2026-10-12 the Cyphal runner passed against the local gateway and generic
sources with AddressSanitizer and UndefinedBehaviorSanitizer enabled. Native
gateway behavior/journal, all eight generic profile/HID/client tests and the
shared transport suite also passed. Firmware-common can be supplied through `--common-source`; this migration
does not change acquisition or the devices' published common dependency pin.

## Automatic lifecycle validation on 2026-10-13

The sanitizer runner passed all focused lifecycle scenarios and all three seeded
fault traces, each replayed twice. Discovery now enrolls without a registration
command, and the real layout worker serializes/persists its immutable target
before publication. Disable/re-enable checks append order and survivor report
IDs; offline ghosts, guarded Forget and stale USB completions are exercised with
actual generic CAN clients. The ten-client stress corpus remains the same scale.
The real acquisition test also queues encoder turns while disabled, verifies
publication lease renewal discards them, and requires new USB completion for a
fresh turn after enable.
Canonical transport tests separately exercise announcements, inputs and presence
from 32 distinct sources. Neither result establishes 32-device electrical or
sustained configuration-throughput qualification. Matching common pins now include
the additive CDC-only helper used by the zero-controller gateway topology.

## Panel-event validation on 2026-10-13

The sanitizer runner passed the sealed-envelope event fixture, all existing
focused lifecycle/ten-client cases and all three seeded fault traces, each
replayed twice. Device/gateway shared-Cyphal copies remain byte-identical to
canonical sources. The common event collector also passes native bounds and
recovery tests with 99.14% line coverage. Software interoperability does not
establish electrical button capture, physical CAN or bench qualification.

The panel/action integration fixture also enables endpoint ownership through real
sealed CAN request/response fragments. It proves claim stores fallback without a
driver write, immutable claim/renew retries do not extend TTL, owned setters apply
once, a competing direct USB setter is rejected, public status hides secrets and
independent expiry/current-route loss clears the claim and applies exact fallback.
A zero-input nonzero-revision profile is valid but has no valid Input report.
Shared and product native suites separately cover TTL bounds, callback failures,
output-only CDC topology and registry/controller transitions. No physical device
or live-game qualification is implied.

Integration CI selects immutable device/gateway commits from the same contract
milestone, then resolves their identical firmware-common pin. Updating one
component requires checking both sealed Cyphal snapshots and matching common
revisions before publication; mixing older M3 firmware with M4 canonical source
is rejected before compilation.
