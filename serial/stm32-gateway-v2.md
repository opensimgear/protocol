# STM32 gateway Cockpit adapter v2

This bounded private CDC interface connects the real CAN gateway to Cockpit. It
is independent of the Pico simulator's JSON protocol v1. Cockpit desktop selects
the `stm32_gateway` adapter only after the product/UID metadata and snapshot
handshake agree. No public VID/PID is assigned by this contract.

Transport is a newline-terminated ASCII request and a single JSON response line.
Requests are at most 79 characters before newline. Request IDs are unsigned 32-bit
decimal integers. Commands have exactly these forms:

```text
cockpit <id> snapshot
cockpit <id> <epoch> register <32-hex-module-identity>
cockpit <id> <epoch> unregister <32-hex-module-identity>
cockpit <id> <epoch> save
cockpit <id> <epoch> apply
```

The mutation epoch must match the current snapshot. A stale epoch rejects the
write and returns fresh state. Serial replies are correlated by ID; incomplete,
oversized, invalid or incompatible responses do not become trusted inventory.
IDs alone are not persistent routing identities. The native adapter verifies the
gateway's stable 24-hex MCU UID on every handshake/reconnection and changes its
epoch-bound session when the gateway restarts.

Responses use this envelope (`error` is null on success):

```json
{"id":1,"ok":true,"error":null,"state":{"gatewayType":"stm32-can-gateway","protocol":2,"canProtocol":2,"firmware":"0.2.0","identity":"00112233445566778899aabb","epoch":1,"running":true,"needsApply":false,"maxModules":10,"modules":[]}}
```

Each module supplies `identity` (32hex), `online`, `directUsb`, `node` (chain-local
0–10), `lease`, `profile`, `pendingSlot`, `activeSlot`, `pendingProfileMatch`,
`profileMatch`, `hasInput` and `report` (hexadecimal HID bytes). Profiles contain
`buttons` 0–128, `axes` 0–8, `hats` 0–4, nonzero u16 `revision`, and exactly `axes`
entries each in `axisUsage` and `neutral`. Profiles must contain at least one
control; usages are distinct 0x30–0x38 and neutral values are u16. Report length is
`ceil(buttons/8)+ceil(hats/2)+2*axes`; decoding follows the
[CAN v2 reference](../reference/can-fd-v2/README.md). Invalid padding, hats or
lengths are rejected. Slots are -1 (absent) or 0–9. Each occupied slot has one
identity. Input ownership and observed/published profile compatibility are
explicit, rather than inferred from a socket or node number.

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
and writable device settings are unsupported. Closing Cockpit closes serial
access while physical gateway HID continues running. Setup claims in the
desktop adapter do not imply a CAN device configuration/output session.

This adapter currently manages one selected gateway at a time. Independent
gateways must use electrically separate chains. Simultaneous combined gateway
inventories, persisted device settings/calibration, outputs and firmware updates
remain outside this implementation.
