# Private CAN FD prototype v2

This reviewed codec snapshot is used by the gateway and generic-device firmware.
It is a prototype interoperability contract, not a Cyphal release. V1 peers are
incompatible; update gateway and devices together. The C codec is vendored into
each firmware so clean builds do not depend on unpublished commits or sibling
checkouts. `SHA256.json` pins the bytes. From a workspace containing all three
repositories, run `python3 protocol/tools/verify_can_vendors.py --workspace .`.
Without `--workspace`, the tool verifies this repository alone. To change the
contract, update the reference, both copies, manifest and conformance vectors in
one reviewed change. The existing firmware-common dependency pin is unchanged.

CAN uses extended IDs `0x1a500000 | (type << 8) | node`, FD and BRS, 500 kbit/s
arbitration and 2 Mbit/s data. The gateway's two 4P4C sockets continue one chain;
node IDs 1–10 are local to that chain. One gateway per electrically separate
chain is supported. The connector pinout is 1=protected 12 V, 2=CAN_H, 3=CAN_L,
4=GND; use continuity-verified pin-to-pin cables and terminate both bus ends.

All frames start with `4f 47 02 type node directUsb 00 00`. Multibyte integers
are little endian. The final four bytes are reflected CRC-32/ISO-HDLC of the
preceding bytes (polynomial 0xedb88320, initial/final XOR 0xffffffff). Reserved
bytes must be zero. Unsupported versions, classic CAN, missing BRS, bad CRC,
incorrect DLC and invalid identities/capabilities are rejected.

| Type | DLC bytes | Payload |
| --- | ---: | --- |
| 1 ANNOUNCE | 64 | Identity[16] at 8, boot counter at 24, buttons at 28, axes at 29, revision u16 at 30, axis usages[8] at 32, neutral u16[8] at 40, hats at 56, reserved 57–59, CRC at 60. Node=0. |
| 2 ASSIGN | 48 | Identity at 8, boot at 24, lease at 28, gateway epoch at 32, reserved 36–43, CRC at 44. |
| 3 INPUT | 64 | Boot at 8, lease at 12, sequence at 16, report[34] at 20, reserved 54–59, CRC at 60. |
| 4 HEARTBEAT | 48 | Identity at 8, boot at 24, lease at 28, sequence at 32, reserved 36–43, CRC at 44. Direct USB ownership may be asserted. |
| 5 INPUT_ACK | 64 | Same compact layout as INPUT, echoing the exact sequence and report actually delivered to the USB host. |

Capability bounds are 128 buttons, four hats and eight axes; revision is nonzero.
Axes use distinct Generic Desktop usages 0x30–0x38. Unused usage/neutral fields
are zero. Reports contain packed buttons, packed hat nibbles (0=N clockwise
through 7=NW;8=neutral), then u16 axes. Unused bits/nibbles/report bytes are zero.
Maximum report length is 34 bytes. INPUT and ACK omit UUID; the receiver resolves
the current identity through the assigned node, boot counter and lease. This
allows the complete generic-device profile to fit in one CAN FD frame.

Devices persist a boot counter before joining CAN. Gateway epochs and leases
are persistently reserved. Sequence ordering rejects stale inputs within a
lease. Direct USB takes input ownership; the gateway retains a neutral slot.
Only actual USB HID completion can acknowledge encoder delivery. Route/session
changes invalidate pending delivery; an old ACK cannot advance a later pulse.
This is freshness protection for cooperating devices, not authentication.

ASSIGN does not contain gateway identity. Epochs are ordered within one gateway,
not across gateways; moving a device to another chain requires route loss or a
restart. Concurrent gateways on one chain and hostile replay are unsupported.
Erasing all persistent state also erases its monotonic history. Device settings,
calibration, output control and remote firmware update are not defined here.
