# Protocol compatibility

The current application contracts are under development; this is not a production release matrix.

| Contract | Compatible consumers | Boundary |
| --- | --- | --- |
| Cyphal application v3 | Matching STM32 device-gateway, generic Rev B device and native-backed Python peer | Generated DSDL 1.0 over Cyphal/CAN; private CAN v1/v2 frames are rejected; update device/gateway together |
| Private CAN FD v2 (historical) | Older matching device/gateway firmware and archived peer | Incompatible with Cyphal firmware |
| Gateway CDC v2 | Gateway machine console and Cockpit's STM32 Rust adapter | `canProtocol=3` identifies Cyphal, `2` identifies legacy private CAN; Cockpit accepts both CDC snapshots and retains the reported version; writes require the observed epoch |
| Pico simulator protocol 1 | Pico W firmware and Cockpit's Pico adapter | Separate simulator contract; it does not interoperate with STM32 CAN or CDC v2 |

The gateway migrates the older persisted v1 registry into the v2 format. This storage migration does not make v1 CAN peers compatible. Rev A module hardware uses a different pin map and six-contact connector; its retained firmware at commit `5a78e46` must not be replaced with the current Rev B target.

Software conformance is recorded in [vectors](../test-vectors/README.md), [Cyphal tests](../cyphal/README.md) and [cross-firmware tests](../tests/README.md). Physical STM32 USB/CAN acceptance remains pending. Cyphal node IDs are local transport addresses and may change after collision recovery; identity and controller slots stay stable. Compatibility and release work is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).
