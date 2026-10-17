# Endpoint output ownership

Devices that enable the shared ownership handler advertise this additive field on
successful catalogue/state/invoke and lease replies:

```json
"ownership": {"schemaVersion": 1, "scope": "endpoint", "maxLeaseMs": 5000, "restorePolicy": "if-unchanged", "fallbackPolicy": "capture-at-claim"}
```

A host must discover this capability before claiming an output. Legacy catalogues
may omit it. `restorePolicy` is additive: endpoints advertising only the legacy
schema/scope/TTL capability remain discoverable, but do not promise guarded
restoration. A host requiring reversible owned Setter writes must explicitly
require `restorePolicy: "if-unchanged"`. Claims are separate from configuration
writer drafts: they are
volatile, never save settings, and never change input mappings or HID descriptors.
The fitted device callback remains the output driver. A successful readback is
software state, not an electrical measurement.

## Commands and responses

Direct CDC and routed CAN use the same full child commands:

```text
action claim <catalogueRevision> <actionId> <token> <ttlMs> <fallbackArgs...>
action claim_current <catalogueRevision> <readableSetterId> <token> <ttlMs>
action renew <token> <leaseId> <ttlMs>
action invoke_owned <token> <leaseId> <catalogueRevision> <actionId> <args...>
action release <token> <leaseId>
action ownership <offset>
```

The token is a fresh secret nonzero UUID: 36 hexadecimal characters including hyphens at
UUID positions. Hex case normalizes to lowercase. It is never returned in device
JSON, inventory or public status. The host keeps it in memory; it must not place
it in saved profiles, logs or UI diagnostics. This is cooperative ownership on a
local trusted transport, not authentication against a bus observer.

`ttlMs` is decimal 250–5000, inclusive. Hosts normally renew every 1000 ms. Each
new claim receives a nonzero monotonically increasing u32 `leaseId`. Exhaustion
fails closed until device reboot; old IDs are not reused within that boot. A
claim on an already owned endpoint with the same token and transport scope keeps
its ID and atomically replaces its validated fallback and expiry. Other owners
receive `outputBusy`. A new host session uses a new token.

`fallbackPolicy: "capture-at-claim"` is an optional advertised capability for
`claim_current`. A host requiring restoration of the actual prior output must
require it explicitly. The command has no fallback arguments. In the product's
serialized endpoint owner context, one validated read captures both fallback and
last-owned tuple before granting the lease; it performs no output write. A host
read followed by a legacy `claim` cannot emulate this guarantee, because another
writer may change the output between those commands.

Repeating `claim_current` with the exact live token, scope and readable setter
keeps the lease ID and original captured fallback/last-owned tuple, extending
only expiry. It never recaptures the run's own output or an external change.
Switching claim mode or readable setter on an existing captured lease returns
`outputBusy`; explicitly release/expire it before starting another claim mode.
A legacy live claim cannot be upgraded in place. Ordinary legacy claims keep
their previous fallback-replacement behavior. An ambiguous mutation is still
never retried automatically.

A legacy claim accepts only a readable setter and the same ordered, typed arguments as
an invocation. The complete fallback passes range, UTF-8, display geometry and
encoding checks before the claim changes. Claiming stores fallback without writing
it. A claim also reads and validates the current tuple in that setter's exact
argument shape; it fails without changing ownership when that read fails. A lease
covers the advertised physical `endpoint`, including every alias and
one-shot action affecting that endpoint. Different endpoints can have different
owners. Legacy `invoke` and LED diagnostic overrides are rejected only while
their endpoint is claimed. Read-only catalogue and state requests remain allowed.
An owned invocation validates token, lease ID, transport scope, catalogue and
endpoint before calling the driver; it does not renew expiry. Successful owned
invocations read the claimed Setter again to retain its exact last-owned tuple,
even when the invoked action is an alias with another argument shape. A failed
or uncertain invoke/readback invalidates restoration authority until a later
explicit successful owned invocation or claim supplies a new validated tuple.

Claim and renew reply:

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"ownership":{"schemaVersion":1,"scope":"endpoint","maxLeaseMs":5000,"restorePolicy":"if-unchanged"},"leaseId":7,"endpoint":"led.rgb","remainingMs":5000}
```

Release first compares current readable state with the lease's last confirmed
owned tuple. Only an unchanged endpoint applies and reads back the stored
fallback. An externally changed endpoint is relinquished without any setter
call. The reply reports `fallbackStatus` as `applied` or `preserved`:

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"ownership":{"schemaVersion":1,"scope":"endpoint","maxLeaseMs":5000,"restorePolicy":"if-unchanged"},"leaseId":7,"endpoint":"led.rgb","released":true,"fallbackStatus":"applied"}
```

An owned invoke uses the ordinary action result (`id` and typed `values`). The
ownership query uses `ownership` as an array, so it omits the capability object:

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"total":2,"next":null,"ownership":[{"endpoint":"led.rgb","actionId":"led.rgb.set_color","owned":false,"leaseId":7,"remainingMs":0,"fallback":["000000"],"fallbackStatus":"applied"},{"endpoint":"led.boolean","actionId":"led.boolean.set_enabled","owned":false,"leaseId":0,"remainingMs":0,"fallback":[],"fallbackStatus":"none"}]}
```

`total` counts unique endpoints. Follow `next`; pages contain at most four entries
and are additionally bounded by JSON bytes. Offset equal to total gives an empty
terminal page. `actionId` identifies the setter whose fallback is retained, or
the endpoint's first action before any claim. `fallbackStatus` is `none`,
`applied`, `preserved` or `failed`; lease ID and fallback remain visible after
release/expiry.
No token appears. A new claim resets this status.

## Expiry, loss and temporary overrides

The device ticks leases independently of USB input ownership, HID delivery,
acquisition, event polling and output requests. Expiry clears ownership and its
secret, then compares current state with the last confirmed owned tuple.
Explicit release does the same immediately. `restorePolicy: "if-unchanged"`
requires this comparison for expiry and transport loss as well as explicit
release: declining release at the host cannot disable a later unsafe TTL write.
A mismatching value is preserved without invoking the fallback and reports
`preserved`. A failed comparison read or uncertain last-owned tuple reports
`failed` and performs no fallback write. The product serializes registry calls,
tick, scope loss and every write to the same software endpoint in one
non-reentrant owner context, so no other product write can interleave between
comparison and restoration. An applied fallback requires the write callback to
succeed and readable values
to equal the stored fallback exactly. Failure leaves the endpoint unowned and
records `failed`; an explicit release returns `fallbackFailed`. The device does
not later retry an old fallback over a new owner's output.

Direct USB disconnect invalidates USB-scoped leases. CAN assignment/source,
boot/epoch/lease route changes and local route resets invalidate CAN-scoped leases.
A module-local route generation detects timeout/reset even if reassignment repeats
the same wire IDs; it also invalidates pre-reset request assembly/reply caches.
Pure USB input ownership handoff does not change this generation. Losing one transport
does not release another transport's endpoints. A silent loss still expires at
TTL. Reboot clears all claims and restores the product's volatile output defaults.
The Pico debug gateway's virtual device bus uses CAN scope and invalidates it on
debug gateway CDC disconnect, virtual route changes, offline transitions and
reboot. This exercises the production handler without claiming physical CAN
qualification.

A temporary manual override belongs to the host holding the existing lease. It
suspends binding writes to that endpoint for a bounded timer, invokes the manual
value with `invoke_owned`, and continues renewal. On timer completion it resumes
the latest committed fresh provider value, or the configured fallback. A second
client cannot bypass this by using legacy actions or LED diagnostics. If the
host exits without releasing, independent expiry uses the same guarded restoration policy. Manual
requests never acquire configuration drafts or write flash.

The existing sealed `ConfigRequest/ConfigResponse` envelopes and guarded
`cockpit <id> <epoch> action <identity> <boot> <child>` route are unchanged.
Whole action commands fit the 384-byte buffer (383 characters plus NUL); replies
are at most 4091 JSON bytes plus `OK\n`. Enabling ownership rejects a catalogue
whose worst-case owned invocation cannot fit. A normal one-string action with
31-character ID and 63 UTF-8 bytes uses at most 236 command characters, including
UUID, u32 lease/catalogue numbers and hex encoding.

CAN's last completed-request cache returns its immutable reply without applying,
claiming or renewing again. A cache hit cannot extend a lease. No exactly-once
promise survives cache eviction or route/reboot changes. Hosts must not retry an
ambiguous mutation automatically: query public ownership and readable state,
then reconcile within a fresh current lease. A retained reply can describe an
older state; it does not prove current ownership or extend its remaining time.

Errors add `ownershipUnsupported`, `outputBusy`, `outputNotOwned`,
`staleOutputLease`, `leaseExhausted` and `fallbackFailed` to the existing action
validation errors. They remain nested `targetAction` errors on a successful
transport reply. There is no new DSDL subject or protocol-version change.

## Output-only devices

A valid nonzero profile revision with zero buttons, axes and hats advertises an
output-only module. Axis usage/neutral tails remain zero. Ordinary discovery and
application Status establish its guarded configuration/action/event route, but
it sends no Input reports and requires no InputAck. Its stored registry slot uses
report ID `0` and order `255`, reserving neither a HID report ID nor controller
position. The raw CDC inventory reports `enabled:false`, `reportId:null`,
`descriptorIndex:-1`, `published:false` and `outputOnly:true`, consistent with the
[gateway CDC contract](stm32-gateway-v3.md). Cockpit normalizes the unpublished
descriptor index to `null` in its device model. `Enable`
returns `input_not_supported`. Inventory/settings persistence may occur; an
unchanged controller descriptor activates without a USB reconnect.

The direct device USB descriptor is CDC only for zero inputs. Adding inputs
through explicit configuration builds the normal HID descriptor and activates
it through the existing detach/apply path. An output-only device that later
advertises inputs remains disabled until enabled; removing all inputs removes
its HID collection through normal layout activation. Surviving controllers keep
their report IDs and order. Output capability metadata describes fitted ports;
the starter board still advertises only its monochrome status LED.
