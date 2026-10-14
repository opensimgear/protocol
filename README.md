# OpenSimGear protocol

Shared wire contracts, generated DSDL and conformance checks for OpenSimGear firmware and Cockpit. The STM32 gateway and generic device use **Cyphal/CAN**, with a separate gateway CDC v3 API for Cockpit. The CAN application contract remains version 3; the replaced private CAN FD v2 codec remains as a historical reference.

This is work in progress ahead of going live. Existing contract identifiers describe the current implementation; no production release is declared.

## Implemented contracts

| Contract | Consumers | Current behavior |
| --- | --- | --- |
| [Cyphal/CAN](cyphal/README.md) | STM32 device-gateway, generic Rev B device and native-backed Python peer | Generated identity/profile, controller lease, input and delivery-ACK transfers, bounded module configuration, device actions and panel events; node-ID autoconfiguration, heartbeat and node information |
| [STM32 gateway CDC v3](serial/stm32-gateway-v3.md) | Gateway firmware and Cockpit's Rust STM32 adapter | Automatic enrollment, ordered enabled controllers, offline ghosts, remembered disable/enable, guarded offline removal and profile updates, asynchronous persistent USB publication and routed configuration/actions/events |
| [Device-advertised actions](serial/device-actions.md) | Module direct USB, CAN gateway, Cockpit and native firmware emulator | Bounded paginated typed catalogue, validated invocation/readback, LED overrides and display dimensions/encoding on string setters |
| [Panel input events](serial/panel-input-events.md) | Module direct USB, CAN gateway, Cockpit and native firmware emulator | Independent mapped-button/encoder transitions, typed control metadata, calibrated axis snapshots, bounded pages and stream/gap recovery |
| [Runtime module configuration](serial/runtime-configuration.md) | Module direct USB, CAN gateway, Cockpit and native firmware emulator | Exclusive drafts, all input mappings/calibration, Apply/Save/Discard, flash recovery and generated HID descriptors |

The gateway automatically enables and appends new compatible identities, with bounds of 32 runtime modules, remembered devices and enabled controllers. Profiles support 128 buttons, four hats and eight axes. Descriptor order and report IDs are independent of CAN routes and Cyphal node IDs. Enabled offline devices retain neutral collections; disabling removes a collection while remembering the choice, and re-enabling appends it. Offline devices can be forgotten with a binding warning. Zero enabled controllers expose CDC only. Direct device USB has input priority. Sequence/session/lease and source-node checks reject stale delivery; gateway USB completion credits encoder pulses. These application rules remain above the Cyphal transport.

Cockpit consumes actual C formatter fixtures. Software tests establish portable source interoperability; assembled STM32 USB/CAN, timing, power and flash brownout behavior remain unverified.

The [native integration harness](tests/README.md) checks ten concurrent device clients through discovery, messaging, frame loss/delay/reordering, reboots, CAN node-ID collisions and direct USB ownership changes. Seeded randomized stress combines these faults with duplicate and malformed frames, CAN transmit backpressure, simulated bus-off recovery and device disconnect/reconnect, checks delivery and queue invariants, and requires all ten devices to recover after faults stop. Each seed is replayed to verify determinism. GitHub Actions runs it with address and undefined-behavior sanitizers.

Cyphal firmware rejects the replaced private CAN v1/v2 wire peers. Sparse v1/v2 registry storage migrates to v3 while preserving registered controller order and report IDs. Cockpit retains a separate legacy CDC v2 adapter. The Pico W simulator uses its own protocol-1 adapter. Update matching STM32 device/gateway firmware together for the expanded logical-route bound. See [compatibility](compatibility/README.md).

## Controller lifecycle and bindings

The [automatic enrollment architecture](https://github.com/opensimgear/opensimgear.github.io/blob/main/src/content/docs/gear/architecture/usb-hid.md) preserves surviving controller order, report IDs and control definitions. No permanent management collection is exposed. A descriptor change reconnects USB after saving the exact generation. Removing a collection or crossing the single/multiple collection boundary can still change Windows identities and require game rebinding. Software evidence does not replace [physical Windows and game qualification](https://github.com/opensimgear/device-gateway/issues/6).

## Layout

- [cyphal/](cyphal/README.md): canonical bounded transport, generated bindings and pinned libcanard source, snapshotted into each device for independent builds.
- [reference/can-fd-v2/](reference/can-fd-v2/README.md): historical private codec snapshot and SHA-256 manifest.
- [serial/](serial/README.md): current STM32 CDC v3, legacy CDC v2, runtime configuration, device action and bounded input event APIs.
- [test-vectors/](test-vectors/README.md): historical private CAN frames and current gateway formatter snapshots.
- [tests/](tests/README.md): integration runner linking gateway, generic client and shared acquisition code.
- `tools/verify_can_vendors.py`: checks canonical transport snapshots and stored or freshly generated CDC fixtures.
- [dsdl/](dsdl/README.md): canonical application schemas and local subject allocation.
- [compatibility/](compatibility/README.md): prototype version boundaries and production release policy.

## Verify adjacent checkouts

From the parent workspace containing `protocol/`, `device-gateway/`, `device-template/` and `cockpit/`:

```sh
cmake -S device-gateway/firmware -B .research/protocol-gateway-native -DCMAKE_BUILD_TYPE=Release
cmake --build .research/protocol-gateway-native
ctest --test-dir .research/protocol-gateway-native --output-on-failure
python3 protocol/tools/verify_can_vendors.py --workspace . \
  --gateway-test .research/protocol-gateway-native/osg_gateway_test
TMPDIR="$PWD/.research" python3 protocol/tests/run_gateway_generic.py \
  --workspace . --common-source /absolute/path/to/pinned/firmware-common --sanitize
```

Supply the common checkout matching the generic firmware's full commit pin. These checks use a host compiler and existing sources; they do not fetch dependencies or flash hardware. The [integration guide](tests/README.md) documents the optional prepared-source default and test boundaries.

Implementation and qualification work is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).

## Related repositories

- [Device gateway](https://github.com/opensimgear/device-gateway): gateway CAN/CDC/HID implementation and electronics.
- [Device template](https://github.com/opensimgear/device-template): generic Cyphal device with direct USB input ownership.
- [Firmware common](https://github.com/opensimgear/firmware-common): reusable acquisition and USB code; pinned protocol source dependency.
- [Cockpit](https://github.com/opensimgear/cockpit): native host adapters, device lifecycle, module configuration/calibration and manual device actions UI.
- [Website](https://github.com/opensimgear/opensimgear.github.io): architecture and user documentation.

## Licensing

No OpenSimGear license grant is implied for newly authored definitions or reference code. Production protocol, firmware and hardware releases require documented licensing and compatible USB identifiers.
