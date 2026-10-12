# Canonical Cyphal input definitions

Application contract 3 uses the `opensimgear.input` 1.0 DSDL definitions here.
They preserve stable 128-bit hardware identity, nonzero boot sessions and leases,
the gateway epoch, application sequence, direct-USB ownership and exact HID input
reports. `Profile` supports 128 buttons, eight axes and four packed hats; unused
axis entries and report bytes must be zero. Axis usages and neutral values retain
the USB profile semantics used by both firmware implementations.

The following subject bindings are a private deployment configuration in the
unregulated 13-bit subject-ID range. They are not regulated fixed port IDs, and
there are no numeric port prefixes in application DSDL filenames. Every node on
an OpenSimGear chain must use the same bindings; other applications sharing the
bus must reserve these IDs or change the bindings consistently before building.

| Subject | DSDL type | Payload bytes | Publisher |
| --- | --- | ---: | --- |
| 2000 | `opensimgear.input.Announce.1.0` | 50 | Generic device |
| 2001 | `opensimgear.input.Assignment.1.0` | 29 | Gateway |
| 2002 | `opensimgear.input.Input.1.0` | 47 | Generic device |
| 2003 | `opensimgear.input.Status.1.0` | 30 | Generic device |
| 2004 | `opensimgear.input.InputAck.1.0` | 47 | Gateway after USB completion |
| 7509 | `uavcan.node.Heartbeat.1.0` | 7 | Every node, at least once per second |
| Service 430 | `uavcan.node.GetInfo.1.0` | Request 0; response up to 313 | Every node responds |

Application subjects are broadcast. Assignment identifies the intended device by
UID and boot session. The device records the gateway's source CAN node-ID and
accepts later assignment/ACK only from its established gateway source. Gateway
input/status acceptance requires the source CAN node-ID established by the UID
announcement, plus the current logical route, boot session and lease. InputAck
echoes the application sequence and exact report only after successful USB interrupt
completion; transport admission, CAN acknowledgement or elapsed time cannot credit
encoder delivery. Application identity/source checks provide routing and freshness,
not authentication against a malicious CAN participant.

`logical_node` values 1–10 identify gateway-local routes and are independent of
the transport's 7-bit CAN node-ID. Each chain has one physical controller shared
by the gateway's two sockets. Pinned libcanard 5.0 uses Cyphal/CAN 1.1 automatic
node-ID occupancy/collision tracking, so logical lease assignment does not allocate
CAN node-IDs. A migrated transport source requires renewal of its application lease.
Independent gateway chains may reuse both kinds of IDs; stable gateway identity
scopes host routes.

The subset under `uavcan/` is copied unchanged from
[OpenCyphal/public_regulated_data_types](https://github.com/OpenCyphal/public_regulated_data_types),
commit `86e2f332419ff015f4cc9f1bb7843b02d5f7a5ed`, with its MIT license. The shared
library under `../cyphal/` includes deterministic generated C bindings and the pinned
generator/check command. Outgoing 13-bit subjects and standard services remain
compatible with Cyphal 1.0 transport kinds while the embedded stack implements 1.1.
