# Volatile controller publication override, schema 1

This additive CDC v3 capability controls the gateway's composite CAN controller
publisher. It changes actual HID collection visibility, retaining CDC management,
the independent panel event stream and action access. It never changes the saved
enabled choice, retained input profile, report ID or registry order. Firmware
startup uses configured state and no resumed owner. Software evidence does not
qualify physical STM32 USB/CAN or game bindings.

## Capability and snapshots

Require a validated `stm32-can-gateway` protocol 3 snapshot with this exact
`controllerPublicationOverride` object before offering the operation:

```json
{"schema":1,"scope":"gateway-can","nonpersistent":true,"captureAtClaim":true,"restore":"compare-if-unchanged","minLeaseMs":250,"maxLeaseMs":5000,"maxTargets":32,"reconnectRequired":true}
```

Absent, null, unknown or malformed capability metadata means unsupported. It is
independent of persistent Enable/Disable and output-action ownership. This unit
does not add a direct-module USB or Pico debug publication capability. A module
also publishing through its own USB connection cannot be claimed through this
gateway capability: `directUsb` must be false at claim and throughout ownership.

Snapshots add these fields:

| Field | Meaning |
| --- | --- |
| `publicationRevision` | Nonzero u32 native snapshot revision. Advances on registry changes, descriptor activation, relevant module presence/boot/configuration/profile/direct-USB/route changes, and publication ownership changes. Zero means exhausted; acquisition is disabled rather than reusing a revision. |
| `configuredGeneration` | Last successfully persisted configured registry generation. |
| `effectiveGeneration` | Configured generation underlying the active HID descriptor; temporary visibility can differ without changing this value. |
| `publication` | The bounded status object described below. |
| Module `publicationSlot` | Remembered registry slot 0–31, or -1 before enrollment. This is the bit index for claim masks, independent of inventory iteration, report ID and descriptor order. |
| Module `configuredEnabled` | Last persisted enabled choice. Existing `enabled` is staged persistent intent and `published` is actual HID visibility. |
| Module `publicationReportId` | Captured configured report ID even while disabled; zero for no input profile or no configured slot. |
| Module `configuredDescriptorIndex` | Configured order, or -1 when configured disabled. |

Status has `phase` (`idle`, `owned`, `restoring`, `attention`), `outcome` (`ok`,
`no_change`, `restore`, `preserved`, `unverified`, `exhausted`), `leaseId` (u32,
zero before any claim), `expiresAtMs` (wrapping device monotonic u32), `transition`
(decimal u64 string), `claimRevision` (the initial revision), `targetMask` and
`enabledMask` (eight lowercase hexadecimal digits), and `expectedReconnect`
(boolean). It never exposes the private owner. `layoutPending` continues to
describe an in-progress descriptor reconnect. Only settled readback proves the
result; an accepted claim alone does not prove descriptor activation.

## Bounded grammar

All requests retain the newline-terminated correlated envelope. Numeric fields
are exact ASCII unsigned decimal u32, with no signs or suffixes. Owner is a
nonzero, non-all-ones 16-byte identity encoded as exactly 32 hexadecimal digits.
Masks contain exactly eight hexadecimal digits, without `0x`. TTL is 250–5000 ms.

```text
cockpit <id> <epoch> publication describe
cockpit <id> <epoch> publication state
cockpit <id> <epoch> publication inspect <owner32hex> <leaseId>
cockpit <id> <epoch> publication claim <owner32hex> <publicationRevision> <targetMask8hex> <enabledMask8hex> <ttlMs>
cockpit <id> <epoch> publication renew <owner32hex> <leaseId> <ttlMs>
cockpit <id> <epoch> publication release <owner32hex> <leaseId>
```

`targetMask` must be nonzero; `enabledMask` must be a subset. A target bit with
an enabled bit of zero requests unpublished HID, and one requests the actual
configured HID profile. Bits cannot invent a profile or select a later arrival.
All 32 targets fit the existing 384-byte child-command bound; buffers are not
widened and there is no list-of-UIDs fallback. Describe/state return the ordinary
snapshot. Claim/renew/release return the same snapshot and status, with no target
child JSON. The gateway epoch must match for every publication request.

Inspect adds a request-scoped top-level `publicationOwned` boolean to the
correlated response. True proves that this exact private owner and lease match
the current epoch's owned/restoring/attention record; idle returns false. It does
not reveal the owner, alter expiry or admit an operation. A status snapshot with
matching revision and masks is insufficient ownership proof: another owner can
win the same request while a reply is lost. Reconcile an ambiguous claim by
reading a candidate lease from status, then inspecting that lease with the
journaled private owner. Only a positive exact probe plus settled `owned` state
permits execution. Recovery may probe read-only repeatedly and release only a
positively matched existing lease. It must not renew or reclaim to prove ownership.

## Atomic ownership and reconciliation

The serialized firmware owner resolves the current revision and target slots,
validates all targets, and captures their actual UID, module boot, configuration
generation, full native profile, configured enabled/order/report ID and current
CAN node/source/lease tuple in the same call as acquiring ownership. Only an
online, input-capable, profile-matching CAN target with a live nonzero boot and
configuration generation is eligible. Acquisition requires a settled configured
layout. One owner controls one composite gateway publication transaction.

An exact repeated claim from the same owner, initial revision and immutable masks
returns its existing lease. It never captures a second baseline, extends expiry
or schedules a second apply. Changed requests and other owners are busy. Only an
explicit matching renew changes expiry. There is no session replay or reclaim
after a lost reply. The host journals its sealed receipt before claim, then
reconciles read-only status and only releases its exact existing lease during
recovery. It must never recover by claiming, enabling or reapplying a graph.

Immediately before descriptor IO, firmware admits the immutable one-shot apply.
The separate readback confirmation cannot be used without admission. Release,
expiry, module reboot/configuration/route/direct-USB change and true transport
loss revoke delivery first and compare actual state against the exact admitted
owned state before restoring the captured original. An unknown read retains an
`attention` record and blocks new ownership. A newer independent state is
preserved and reported; it is never overwritten with an old whole registry.

Affected input configuration writers are busy while owned. Persistent lifecycle
commands are busy for the composite transaction. Output actions and panel-event
management remain independent. Unrelated automatic arrivals can be persisted
normally, while the effective descriptor is rederived around the sealed overlay.
Restoration keeps those arrivals and the captured original target report IDs and
configured order. Override apply/restore never call registry persistence.

USB HID may disappear entirely, leaving CDC-only USB. Each actual visibility
change renews the affected CAN delivery lease and requires a fresh input
baseline; old input and USB completions cannot acknowledge the new layout.
The exact scheduled USB detach/attach has an explicit 1500 ms reconnect bound.
Its owner survives only that bounded transition. Physical VBUS loss, ordinary
DTR close, USB suspend, and a failed expected reconnect revoke ownership. An
unread serial response cannot defer apply beyond the lease expiry; expiry runs
without a reader. A new route generation fences all old scheduled transitions.

Errors include `stale_epoch`, `stale_publication_revision`,
`invalid_publication_request`, `publication_busy`, `publication_not_settled`,
`publication_target_not_ready`, `publication_not_owned`, `publication_expired`,
`publication_stale_time`, `publication_exhausted`, `publication_unverified` and
`publication_preserved`. Rejection does not modify persistent settings. A caller
must reconcile actual status after an ambiguous mutation instead of replaying it.

The adjacent [production formatter vectors](../test-vectors/controller-publication-override-v1.json)
show baseline, settled owned, positive owner inspection and settled restoration.
They are emitted by `device-gateway`'s `publication_test --publication-vectors`.
