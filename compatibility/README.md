# Protocol compatibility

The current application contracts are under development; this is not a production release matrix.

| Contract | Compatible consumers | Boundary |
| --- | --- | --- |
| Cyphal application v3 | Matching STM32 device-gateway, generic Rev B device and native-backed Python peer | Generated DSDL 1.0 over Cyphal/CAN; private CAN v1/v2 frames are rejected; update device/gateway helpers together for logical routes 1–32 |
| Private CAN FD v2 (historical) | Older matching device/gateway firmware and archived peer | Incompatible with Cyphal firmware |
| Gateway CDC v3 | Current gateway machine console and Cockpit's STM32 Rust adapter | Automatic lifecycle; explicit enabled/published state, generations and resource bounds; writes require the observed epoch |
| Gateway CDC v2 (legacy) | Older gateway firmware and Cockpit's separate legacy STM32 adapter | Slot/register/apply lifecycle; `canProtocol=3` identifies Cyphal, `2` identifies legacy private CAN; writes require the observed epoch |
| Pico simulator protocol 1 | Pico W firmware and Cockpit's Pico adapter | Separate simulator contract; it does not interoperate with STM32 CAN or CDC v3 |

The gateway migrates older persisted v1/v2 registries into v3, preserving existing descriptor order and report IDs and marking registered entries enabled. Disabled intent persists across restarts. This storage migration does not make v1 CAN peers compatible. Rev A module hardware uses a different pin map and six-contact connector; its retained firmware at commit `5a78e46` must not be replaced with the current Rev B target.

The firmware upgrade can still require a one-time game rebind; retained report IDs do not guarantee Windows collection identity, particularly when counts change or the old empty-layout placeholder disappears.

Software conformance is recorded in [vectors](../test-vectors/README.md), [Cyphal tests](../cyphal/README.md) and [cross-firmware tests](../tests/README.md). Physical STM32 USB/CAN acceptance remains pending. Cyphal node IDs are local transport addresses and may change after collision recovery; module identities, retained profiles and enabled controller order remain independent. Compatibility and release work is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).
