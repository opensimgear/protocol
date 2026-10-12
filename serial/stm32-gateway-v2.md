# STM32 gateway Cockpit adapter v2

This bounded private CDC interface connects the real CAN gateway to Cockpit. It
is independent of the Pico simulator's JSON protocol v1. Cockpit desktop selects
the `stm32_gateway` adapter only after the product/UID metadata and snapshot
handshake agree. No public VID/PID is assigned by this contract.

Transport is a newline-terminated ASCII request and a single JSON response line.
The firmware line buffer accepts at most 479 characters before newline; the
configuration child command is bounded to 383 characters. Request IDs are unsigned 32-bit
decimal integers. Commands have exactly these forms:

```text
cockpit <id> snapshot
cockpit <id> <epoch> register <32-hex-module-identity>
cockpit <id> <epoch> unregister <32-hex-module-identity>
cockpit <id> <epoch> save
cockpit <id> <epoch> apply
cockpit <id> <epoch> config <32-hex-module-identity> <module-boot> <child-command>
```

The mutation epoch must match the current snapshot. A stale epoch rejects the
write and returns fresh state. Serial replies are correlated by ID; incomplete,
oversized, invalid or incompatible responses do not become trusted inventory.
IDs alone are not persistent routing identities. The native adapter verifies the
gateway's stable 24-hex MCU UID on every handshake/reconnection and changes its
epoch-bound session when the gateway restarts.

Responses use this envelope (`error` is null on success):

```json
{"id":1,"ok":true,"error":null,"state":{"gatewayType":"stm32-can-gateway","protocol":2,"canProtocol":3,"firmware":"0.3.0","identity":"00112233445566778899aabb","epoch":1,"running":true,"needsApply":false,"maxModules":10,"modules":[]}}
```

Each module supplies `identity` (32hex), `online`, `directUsb`, `node` (chain-local
0–10), `boot`, `sourceNode`, `configurationGeneration`, `lease`, `profile`, `pendingSlot`, `activeSlot`, `pendingProfileMatch`,
`profileMatch`, `hasInput` and `report` (hexadecimal HID bytes). Profiles contain
`buttons` 0–128, `axes` 0–8, `hats` 0–4, nonzero u16 `revision`, and exactly `axes`
entries each in `axisUsage` and `neutral`. Profiles must contain at least one
control; usages are distinct 0x30–0x38 and neutral values are u16. Report length is
`ceil(buttons/8)+ceil(hats/2)+2*axes`; decoding follows the
[Cyphal application contract](../cyphal/README.md). Invalid padding, hats or
lengths are rejected. Slots are -1 (absent) or 0–9. Each occupied slot has one
identity. Input ownership and observed/published profile compatibility are
explicit, rather than inferred from a socket or node number.

`protocol=2` versions this CDC console. `canProtocol=3` identifies the Cyphal
application contract; older private CAN gateways report `2`. Cockpit accepts
both console variants. The module `node` field is a logical controller route
0–10 and is independent of its automatically configured Cyphal CAN node ID.

The report uses the **observed** profile. Offline, direct-USB-owned, stale,
inactive or profile-mismatched modules return its neutral report. `profileMatch`
compares observed and active profiles; `pendingProfileMatch` compares observed
and staged profiles. Cockpit must not present stale reports as live controls.
Persisted offline registrations remain in snapshots after restart.

Discovery is automatic. Register/unregister changes the pending registry;
`save` persists it without changing USB. `apply` persists it, drains the response,
detaches USB and publishes the saved pending layout. It is an explicit user
action. The host reconnects by gateway UID, accommodating a changed serial path.
If the reply is lost, inspect the restored active/pending registry rather than
blindly applying a second time. A save failure preserves the active layout.
Offline devices retain their occupied slot and produce neutral HID reports.

The firmware exposes one HID interface with a report ID/top-level collection
per slot (`reportId=activeSlot+1`). Layout changes may change host bindings. The
native adapter reads physical inputs; simulator start/stop/input/add/presence
are unsupported on physical STM32 gateways. Runtime settings use the module's
exclusive writer, bounded drafts and explicit Apply/Save/Discard. The gateway
forwards commands and replies over Cyphal with identity, boot, source, lease and
epoch guards. Configuration replies add `targetConfig` to the JSON envelope;
see [runtime configuration](runtime-configuration.md) for the child grammar.
Configuration generation changes invalidate cached settings independently of the
module boot session and HID profile revision. Legacy snapshots may omit the field.
Closing Cockpit releases setup ownership and serial access while physical gateway
HID continues running. A setup claim does not create an output session.

This adapter currently manages one selected gateway at a time. Independent
gateways must use electrically separate chains. Simultaneous combined gateway
inventories, outputs and firmware updates
remain outside this implementation.
