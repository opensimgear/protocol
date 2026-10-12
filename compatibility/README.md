# Protocol compatibility

The current contracts are private prototypes, not a production release matrix.

| Contract | Compatible consumers | Boundary |
| --- | --- | --- |
| CAN FD v2 | Matching STM32 device-gateway, generic Rev B module and Python peer | v1 wire frames are rejected; update module/gateway firmware together |
| Gateway CDC v2 | Gateway machine console and Cockpit's STM32 Rust adapter | Snapshot handshake checks role, versions, identity and bounded capabilities; writes require the observed epoch |
| Pico simulator protocol 1 | Pico W firmware and Cockpit's Pico adapter | Separate simulator contract; it does not interoperate with STM32 CAN or CDC v2 |

The gateway migrates the older persisted v1 registry into the v2 format. This storage migration does not make v1 CAN peers compatible. Rev A module hardware uses a different pin map and six-contact connector; its retained firmware at commit `5a78e46` must not be replaced with the current Rev B target.

Software conformance is recorded in [vectors](../test-vectors/README.md) and [cross-firmware tests](../tests/README.md). Physical STM32 USB/CAN acceptance remains pending. Future protocol releases need explicit accepted versions, bounded resource requirements, tested firmware/Cockpit combinations and migration rules. No canonical Cyphal application compatibility is defined yet.
