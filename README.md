# OpenSimGear protocol

Canonical, separately versioned protocol definitions shared by firmware and the desktop cockpit. Firmware consumes these definitions through firmware-common; the Rust cockpit consumes them without depending on the C firmware library.

## Status

Repository scaffold initialized. Architecture semantics are agreed, but concrete schemas, Cyphal identifiers, command names, frame limits, timeouts, and fixtures are not yet implemented or assigned. This scaffold is not a wire-compatible protocol release.

## Layout

- [dsdl/](dsdl/README.md): OpenSimGear Cyphal application definitions.
- [serial/](serial/README.md): USB CDC command/framing and capability schemas.
- [compatibility/](compatibility/README.md): version rules and compatibility requirements.
- [test-vectors/](test-vectors/README.md): shared conformance inputs and expected outputs.

## Agreed semantics

USB serial and CAN share a versioned, bounded capability model; their wire framing may differ. The model covers stable unit identity, model/hardware information, available controls/outputs, configuration, health, and compatibility. Incompatible versions produce a clear diagnostic rather than silently guessing.

Node IDs are allocated per CAN segment; gateway controller assignments persist by stable module identity. Registration/layout changes require deliberate USB reconnect, while temporary unplugging preserves slots. Configuration permits one writer per module with explicit Apply/Save and interrupted-write protection. Output ownership uses explicit active sessions and freshness, with direct USB serial priority and fresh gateway fallback.

## Implementation and release policy

Generate bindings where supported, otherwise validate adapters against the same test vectors. Do not assume a supported DSDL-to-Rust generator. Version protocol releases independently and publish the firmware/cockpit compatibility they actually support. Exact identifiers and timing/size limits require implementation and measurement before release.

## Related repositories

- [Protocol](https://github.com/opensimgear/protocol): shared definitions and compatibility rules.
- [Firmware common](https://github.com/opensimgear/firmware-common): reusable C implementation.
- [Device template](https://github.com/opensimgear/device-template): new-device scaffold with planned hello-world firmware and starter KiCad board.
- [Cockpit](https://github.com/opensimgear/cockpit): desktop configuration application.
- [Website](https://github.com/opensimgear/opensimgear.github.io): system architecture and user documentation.

## Licensing

License selection is pending. No license grant is implied by this scaffold. Firmware, mechanical design, and electronics will each need documented license coverage before releases and any public USB identifier application.
