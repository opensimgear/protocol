# STM32 gateway Cockpit adapter v3

CDC protocol 3 exposes gateway-owned automatic controller enrollment and an
ordered enabled HID list. The Cyphal application contract remains version 3:
its existing identity/profile announcements supply enrollment data, and logical
CAN routes now range from 1 to 32. This is a prototype contract; physical
STM32 USB/CAN and Windows game-binding qualification are tracked separately.

## Lifecycle

A first-seen compatible identity automatically enables and appends its HID
collection. The gateway persists and activates changes without Cockpit running.
An enabled identity that temporarily disappears retains its collection and sends
neutral inputs. A returning identity with an unchanged profile resumes at its
existing position without changing the descriptor.

Disable removes a collection but remembers the identity, profile and explicit
disabled choice. Announcements and reboots do not undo that choice. Enable
appends the remembered controller at the end of the enabled list; surviving
collections retain their order, report IDs and controls. Disabled online devices
remain reachable for configuration and device-advertised actions. Forget is restricted to offline identities
and deletes remembered state. Later rediscovery then behaves as first discovery.
Forgetting an already disabled record does not change USB.

Only enabled controllers appear in the HID descriptor, including enabled offline
ghosts. There is no permanent management collection or placeholder joystick.
Zero enabled controllers use CDC-only USB configuration. VID/PID, serial, CDC
interfaces 0/1 and the HID interface number 2 when present remain unchanged.
Removing a collection or moving between one and multiple collections can change
Windows controller identities; preserving report IDs alone does not guarantee
saved game bindings.

## Requests and mutation reconciliation

The transport retains newline-terminated ASCII requests and one correlated JSON
response per request. IDs are unsigned 32-bit decimals. Gateway mutations require
the current nonzero epoch from a validated snapshot:

```text
cockpit <id> snapshot
cockpit <id> <epoch> enable <32-hex-module-identity>
cockpit <id> <epoch> disable <32-hex-module-identity>
cockpit <id> <epoch> forget <32-hex-module-identity>
cockpit <id> <epoch> update-profile <32-hex-module-identity>
cockpit <id> <epoch> config <32-hex-module-identity> <module-boot> <child-command>
cockpit <id> <epoch> action <32-hex-module-identity> <module-boot> <child-command>
cockpit <id> <epoch> events <32-hex-module-identity> <module-boot> <child-command>
```

There is no register/save/apply onboarding. Update-profile is the explicit action
for replacing an existing retained HID profile with a changed observed profile;
it may affect bindings. Configuration child commands retain the existing
[runtime configuration grammar](runtime-configuration.md), writer ownership,
session guards and Apply/Save/Discard semantics. Action child commands use the
[device action grammar](device-actions.md), without configuration claims or
persistence. The reply snapshot adds `targetAction` for actions and `targetConfig`
for settings; both are request-scoped.
Panel input reads use the [bounded event grammar](panel-input-events.md), return
request-scoped `targetEvents` and do not consume or acknowledge HID input.

An accepted lifecycle command can acknowledge staging before persistence and
USB publication finish. Clients observe subsequent snapshots and recover the
same gateway UID after an expected reconnect, including a changed CDC path.
After a lost reply, read back intent/publication state and generations rather
than replaying the mutation. A stale epoch rejects the command. Forget also
checks current presence so a device that returned after the UI snapshot is not
silently forgotten.

The gateway batches arrivals, validates the complete layout and persists the
exact generation before detaching USB. Arrivals during activation are part of
the next transaction. Unchanged announcements do not write flash or reconnect.
Failures preserve the prior active layout and expose an actionable error.
Controllers require fresh accepted input after USB publication.
Publishing an enable/disable visibility change renews that device's CAN lease.
The next assignment rearms acquisition and discards encoder events accumulated
while disabled. Old input and completion acknowledgements cannot cross this
boundary. Lease capacity is reserved before detach; exhaustion leaves the prior
layout active and reports `lease_exhausted`.
Configuration and action dispatch wait for accepted traffic on the new lease.
Application Status supplies that proof even while HID is disabled or direct USB
owns the inputs; route readiness does not require input reports. Before that
proof, requests fail before transmission with `configuration_route_not_ready`;
clients may retry read-only configuration queries after fresh snapshots. An
ambiguous configuration mutation is reconciled by readback rather than replay.

## Snapshot

Responses retain the envelope `{id,ok,error,state}`. The state declares
`gatewayType="stm32-can-gateway"`, `protocol=3`, `canProtocol=3`,
`lifecycle="automatic"`, firmware, stable gateway `identity` (24 hexadecimal
characters), epoch and running state.

| Field | Meaning |
| --- | --- |
| `maxModules`, `maxRemembered`, `maxControllers` | Published resource bounds; currently 32 for each. Remembered disabled records use storage but have no HID collection. Enabled offline ghosts still use controller capacity. |
| `activeGeneration`, `pendingGeneration` | Active and staged registry generations. Presence is independent of these generations. |
| `layoutPending` | Saved/staged publication or USB activation is still in progress. |
| `layoutError` | Null or the latest persistence/descriptor/activation error token. |
| `rejectedAnnouncements` | Counter of announcements rejected because admission could not complete. |
| `lastRejectedIdentity` | Null or the stable identity of the last rejected announcement. |
| `admissionError`, `admissionFailure` | Admission error token and identity-bound detail; null when no failure is reported. `admissionFailure` has `identity` and `error`. |
| `modules` | Remembered/discovered inventory with explicit lifecycle and descriptor position. |

Each module supplies a 32-hex identity, `online`, `enabled` (requested intent),
`published` (active HID collection), `descriptorIndex` (-1 when unpublished),
`reportId` (null when unpublished), `directUsb`, `node`, `sourceNode`, boot,
configuration generation, lease, observed `profile`, `profileMatch`, `hasInput`
and hexadecimal `report`. Logical node 0 means no live route; 1–32 are
gateway-local routes independent of the Cyphal transport node ID.

Profiles retain button counts 0–128, axes 0–8, hats 0–4, nonzero u16 revision,
distinct axis usages 0x30–0x38 and u16 neutral values. Axis arrays have exactly
`axes` entries. At least one control is required. Report length is
`ceil(buttons/8)+ceil(hats/2)+2*axes`. The snapshot report follows the observed
profile; a changed profile, disabled/unpublished device, stale input or direct
USB owner must not be presented as live gateway input. The actual USB collection
keeps its retained profile until an explicit update succeeds.

Identity and report ID are independent of list position. The host derives
Windows collection identity from the descriptor, not from the module's CAN
identity. Cockpit should present enabled controller order accurately and mark
enabled offline devices as ghosts. Disabled devices remain in Cockpit's inventory
but are absent from Windows HID.

## Compatibility and evidence

CDC v2 is retained as a separate legacy adapter/fixture; its slot/register/apply
contract must not be mistaken for automatic lifecycle support. V3 consumers
validate the lifecycle and published capacity before trusting inventory.
The older v1/v2 sparse registries migrate in existing descriptor order, keeping
their report IDs and enabling their previously registered devices.
Preserving that order does not guarantee Windows or game identity across a
firmware upgrade; users may need a one-time rebind after migration, especially
when controller counts change or a zero-controller placeholder disappears.

Changing the accepted logical route bound to 32 requires matching device and
gateway application helpers; the DSDL field remains uint8 and does not change.
Canonical transport snapshots in both firmware repositories are hash-checked.
The Pico protocol-1 demo remains a separate adapter with its own behavior and
capacity; it does not prove production STM32 Windows enumeration.

Canonical CDC v3 fixtures come from the gateway's C formatter and are consumed
by Cockpit's native tests. Native simulations exercise production handlers but
do not create OS USB controllers. Physical qualification remains tracked in
[gateway #6](https://github.com/opensimgear/device-gateway/issues/6).
