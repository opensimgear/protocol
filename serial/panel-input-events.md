# Bounded panel input events

Panel events preserve debounced mapped-button transitions between Cockpit reads.
They use the portable `opensimgear/events.h` implementation and the production
profile's logical button indexes, rather than sampling the gateway's latest HID
report. Acquisition runs independently of direct USB ownership, controller
enablement, CAN input acknowledgements and the USB encoder pulse queue. Hats
remain outside this contract; calibrated axis snapshots are described below.

Direct module CDC accepts:

```text
events state
events read <stream> <after>
```

The gateway routes the child command through the existing sealed configuration
envelopes, with the same identity, boot, source, logical node, lease and epoch
checks as settings and actions:

```text
cockpit <request-id> <gateway-epoch> events <module-identity32> <module-boot> state
cockpit <request-id> <gateway-epoch> events <module-identity32> <module-boot> read <stream> <after>
```

The gateway prepends `events ` to `ConfigRequest.1.0`. `ConfigResponse.1.0`
contains `OK\n` plus the event JSON; the correlated gateway reply includes it as
top-level `targetEvents`, alongside the ordinary inventory `state`. Nested
`ok:false` describes a device event error; outer routing errors are unchanged.
Application Status proves reachability even with HID disabled or direct USB
active. No new DSDL subjects, configuration claim, flash write, HID descriptor
change or input acknowledgement is needed for an event read.

## Baseline and cursor

```json
{"ok":true,"schemaVersion":1,"stream":1,"head":2,"oldest":1,"overflow":0,"buttonCount":1,"held":[false],"encoderButtons":[]}
```

`state` establishes a current baseline. A newly connected, rebound or resumed
consumer sets its cursor to `head`, records `held`, and sends no commands for the
earlier retained events. Scope consists of gateway identity, module identity,
module boot and stream. Each completed input mapping activation starts a new
nonzero u32 stream (the current implementation uses active configuration
generation) and clears the log. A reboot clears it under a new module boot.
Gateway epoch/lease changes invalidate pending routed results; reacquire the
baseline after reconnecting instead of replaying historical presses.

`buttonCount` is 0–128 and matches the active logical button layout: configured
buttons first, two outputs per encoder, then three per selector. `held` has that
many booleans. Production debounce, held-at-start release-to-rearm, exclusive
groups, selector conflicts and expansion availability rules also apply to the
independent event acquisition. A contact must survive the 10 ms debounce period
and be sampled to become an event; shorter electrical glitches are filtered.

`encoderButtons` lists the two logical controls for each encoder, positive then
negative. A completed quadrature detent emits an immediate ordered press/release
pair on the corresponding control. Its `held` value stays false. These records
represent detents, not acknowledgement-paced HID pulses; stalled HID cannot
consume or delay them.

## Input kinds and calibrated axis snapshots

Schema 1 now has additive capability/snapshot fields in both `state` and `read`:

```json
{"ok":true,"schemaVersion":1,"stream":3,"head":1,"oldest":1,"overflow":0,"buttonCount":7,"held":[false,false,false,false,false,true,false],"encoderButtons":[2,3],"contactButtonCount":2,"maintainedButtons":[1],"selectorButtons":[[4,5,6]],"axisCount":1,"axisUsage":[48],"axisNeutral":[32768],"axes":[32768],"axisAvailable":[true],"sampledAtMs":1000}
```

`contactButtonCount` is the number of declared contact buttons, before encoder
pairs and selector triples. `maintainedButtons` lists the unique maintained
logical indexes within that range; the other contact buttons are momentary.
The existing `encoderButtons` are consecutive positive/negative pairs in encoder
order. `selectorButtons` lists consecutive `[low,center,high]` triples in selector
order after encoders. Counts and these indexes must account for the entire
`buttonCount` layout. There are at most 128 total logical controls, eight encoders
and eight selectors. This metadata distinguishes maintained state/selector
positions from momentary presses and encoder detents without reinterpreting the
existing boolean event records.

`axisCount` is 0–8. `axisUsage`, `axisNeutral`, `axes` and `axisAvailable` each have
that length in declared **logical axis order**, rather than physical ADC channel
order. Usages are the profile's unique HID axis codes 0x30–0x38; neutral and
current values are unsigned 16-bit numbers. The device independently acquires
axes with its active profile's minimum/maximum scaling and then applies its
runtime inversion/deadzone. It uses the same production calibration operation,
with a separate always-active report, even with direct USB or HID disabled.
Products register the common event report-transform callback after each mapping
activation; no HID report or delivery acknowledgement supplies these values.

Fresh valid ADC samples set availability true. A missing/invalid sample retains
the previous value only until the 100 ms sensor watchdog expires; it then returns
the declared neutral with availability false. Initialization starts neutral and
unavailable. `sampledAtMs` is the latest collector observation's module-local u32
millisecond clock and may wrap; it is neither a sequence nor a host timestamp.
It does not imply that every sensor supplied a new sample. Availability carries
the per-axis sensor freshness verdict. Route/boot/stream validation and request
correlation still determine whether a reply belongs to the current device.

Axis values are **snapshots**, outside the 64-record ring: intermediate movement
is intentionally coalesced and does not advance `head` or `overflow`. Consumers
use the latest available state for a catalogue-validated absolute `set_state`
binding, scale to that interface's advertised `max_value`, and suppress unchanged
values. They must not turn an unavailable neutral into a live axis command.
Encoder detents and button edges retain their ordered nonconsuming semantics;
they must not be collapsed into absolute snapshots or blindly retried.

Older schema-1 firmware may omit all these additive fields. Existing momentary
bindings continue to use their original contract; clients require the new fields
for typed switch, selector or axis bindings and do not guess from board names.
The maximum 128-button/eight-axis page remains below the existing 4091-byte JSON
budget. No new subject, DSDL version, settings claim, flash write or HID ownership
change is introduced.

`head` is the latest sequence, initially zero. The ring retains at most 64
records; `oldest` is the oldest retained sequence, or `head + 1` for an empty
ring. `overflow` counts evicted records and saturates at u32 maximum. Sequences
never wrap. Exhaustion fails the stream closed until a fresh activation or boot;
an encoder pair is rejected atomically when fewer than two sequences remain.

## Nonconsuming pages

```json
{"ok":true,"schemaVersion":1,"stream":1,"head":2,"oldest":1,"overflow":0,"buttonCount":1,"held":[false],"encoderButtons":[],"after":0,"next":2,"more":false,"events":[{"sequence":1,"control":0,"pressed":true},{"sequence":2,"control":0,"pressed":false}]}
```

`read` accepts an exact stream and a u32 cursor. It returns up to 16 consecutive
records after `after`. `next` is the last returned sequence, or `after` for an
empty page; `more` indicates additional records through this reply's `head`.
Metadata and `held` describe the state at that `head`, which may be newer than
`next` on a partial page. Consumers advance their cursor only after validating
and processing each record; held-state metadata is not a substitute for draining
ordered pages. Multiple consumers can read independently, and reads never clear
or acknowledge the ring.

Consumers reject unexpected boot/stream, mismatched `after`, duplicate/out-of-order
sequences, impossible controls and gaps. A page must contain consecutive sequences
ending at `next`. Already processed sequences never trigger simulator commands
again. On a gap or stream change, release any previously forwarded held input,
obtain a fresh baseline, and require a fresh physical press. On simulator
reconnect, establish the same baseline before forwarding new transitions.

The module's existing last-completed-request cache retains an immutable response:
a retry of the same complete request and route returns that snapshot even if new
events have arrived. A fresh request can safely repeat a read cursor, but the
consumer must discard already processed records. No event history survives reboot
or mapping activation, and the bounded log cannot guarantee delivery across an
unbounded outage.

Recognized failures are `{"ok":false,"error":"<token>"}`:

| Token | Meaning / recovery |
| --- | --- |
| `invalidStream` | No valid input mapping/event collector |
| `staleStream` | Requested stream changed; acquire a fresh baseline |
| `eventGap` | Cursor is less than `oldest - 1`; acquire a fresh baseline |
| `invalidCursor` | Cursor exceeds `head`; reject the stale result and rebaseline |
| `streamExhausted` | Sequence space exhausted; a new activation or boot is required |
| `invalidArgument` | Malformed or out-of-u32 command arguments |
| `unknownCommand` | Unsupported event child command |
| `replyTooSmall` | Reply buffer cannot hold the bounded response |

The full module command is at most 383 bytes. The handler requires a 4096-byte
buffer and leaves room for `OK\n` framing inside the 4095-byte CAN response limit.
Calls, sensor observation and transport handling run in one serialized context.

## Master-caution fixture and validation

`device-template/firmware/profiles/master_caution.h` and
`simulation/configs/master-caution.json` map D0 to momentary logical control 0.
The simulator's `outputs: "board-status"` advertises the same
`led.status.set_mode` action and `auto/on/off` values as the existing monochrome
board LED. Other simulation profiles retain their RGB/boolean output catalogue.
This gives simulator bindings the same button/event/LED contract on board and
native firmware. Physical STM32, button wiring and LED validation remain deferred;
native tests and cross-builds do not establish electrical or bench behavior.
