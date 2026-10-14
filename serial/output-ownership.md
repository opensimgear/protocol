# Endpoint output ownership

Devices that enable the shared ownership handler advertise this additive field on
successful catalogue/state/invoke and lease replies:

```json
"ownership": {"schemaVersion": 1, "scope": "endpoint", "maxLeaseMs": 5000}
```

A host must discover this capability before claiming an output. Legacy catalogues
may omit it. Claims are separate from configuration writer drafts: they are
volatile, never save settings, and never change input mappings or HID descriptors.
The fitted device callback remains the output driver. A successful readback is
software state, not an electrical measurement.

## Commands and responses

Direct CDC and routed CAN use the same full child commands:

```text
action claim <catalogueRevision> <actionId> <token> <ttlMs> <fallbackArgs...>
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

A claim accepts only a readable setter and the same ordered, typed arguments as
an invocation. The complete fallback passes range, UTF-8, display geometry and
encoding checks before the claim changes. Claiming stores fallback without writing
it. A lease covers the advertised physical `endpoint`, including every alias and
one-shot action affecting that endpoint. Different endpoints can have different
owners. Legacy `invoke` and LED diagnostic overrides are rejected only while
their endpoint is claimed. Read-only catalogue and state requests remain allowed.
An owned invocation validates token, lease ID, transport scope, catalogue and
endpoint before calling the driver; it does not renew expiry.

Claim and renew reply:

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"ownership":{"schemaVersion":1,"scope":"endpoint","maxLeaseMs":5000},"leaseId":7,"endpoint":"led.rgb","remainingMs":5000}
```

Release first applies and reads back the stored fallback, then replies:

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"ownership":{"schemaVersion":1,"scope":"endpoint","maxLeaseMs":5000},"leaseId":7,"endpoint":"led.rgb","released":true}
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
`applied` or `failed`; lease ID and fallback remain visible after release/expiry.
No token appears. A new claim resets this status.

## Expiry, loss and temporary overrides

The device ticks leases independently of USB input ownership, HID delivery,
acquisition, event polling and output requests. Expiry clears ownership and its
secret, then attempts fallback once. Explicit release does the same immediately.
An applied fallback requires the write callback to succeed and readable values
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
host exits without releasing, independent expiry applies fallback. Manual
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
it sends no Input reports and requires no InputAck. The gateway remembers it with
`enabled:false`, `reportId:null`, `descriptorIndex:null`, `published:false` and
`outputOnly:true`. It allocates no HID report ID or controller order. `Enable`
returns `input_not_supported`. Inventory/settings persistence may occur; an
unchanged controller descriptor activates without a USB reconnect.

The direct device USB descriptor is CDC only for zero inputs. Adding inputs
through explicit configuration builds the normal HID descriptor and activates
it through the existing detach/apply path. An output-only device that later
advertises inputs remains disabled until enabled; removing all inputs removes
its HID collection through normal layout activation. Surviving controllers keep
their report IDs and order. Output capability metadata describes fitted ports;
the starter board still advertises only its monochrome status LED.
