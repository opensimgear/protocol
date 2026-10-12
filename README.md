# OpenSimGear protocol

Shared wire contracts, generated DSDL and conformance checks for OpenSimGear firmware and Cockpit. The STM32 gateway and generic device use **Cyphal/CAN**, with a separate gateway CDC v2 API for Cockpit. The application contract is version 3; the replaced private CAN FD v2 codec remains as a historical reference.

This is work in progress ahead of going live. Existing contract identifiers describe the current implementation; no production release is declared.

## Implemented contracts

| Contract | Consumers | Current behavior |
| --- | --- | --- |
| [Cyphal/CAN](cyphal/README.md) | STM32 device-gateway, generic Rev B device and native-backed Python peer | Generated identity/profile, controller lease, input and delivery-ACK transfers; node-ID autoconfiguration, heartbeat and node information |
| [STM32 gateway CDC v2](serial/stm32-gateway-v2.md) | Gateway firmware and Cockpit's Rust STM32 adapter | Correlated bounded snapshots, inventory and registry staging, epoch-guarded mutation and explicit persistent Apply layout |

The gateway supports up to ten controller slots and profiles with 128 buttons, four hats and eight axes. These slots are independent of Cyphal node IDs. Direct device USB has input priority. Sequence/session/lease and source-node checks reject stale delivery; gateway USB completion credits encoder pulses. Persisted gateway slots survive temporary absence and remain neutral when inputs are unavailable. These application rules remain above the Cyphal transport.

Cockpit consumes actual C formatter fixtures. Software tests establish portable source interoperability; assembled STM32 USB/CAN, timing, power and flash brownout behavior remain unverified.

The [native integration harness](tests/README.md) checks ten concurrent device clients through discovery, messaging, frame loss/delay/reordering, reboots, CAN node-ID collisions and direct USB ownership changes. Seeded randomized stress combines these faults with duplicate and malformed frames and CAN transmit backpressure, checks delivery and queue invariants, and requires all ten devices to recover after faults stop. Each seed is replayed to verify determinism. GitHub Actions runs it with address and undefined-behavior sanitizers.

Cyphal firmware rejects the replaced private CAN v1/v2 wire peers. Existing registry storage remains compatible, including its older v1 migration. The Pico W simulator uses its own protocol-1 adapter. Update matching STM32 device/gateway firmware together. See [compatibility](compatibility/README.md).

## Layout

- [cyphal/](cyphal/README.md): canonical bounded transport, generated bindings and pinned libcanard source, snapshotted into each device for independent builds.
- [reference/can-fd-v2/](reference/can-fd-v2/README.md): historical private codec snapshot and SHA-256 manifest.
- [serial/](serial/README.md): implemented STM32 CDC v2 API and future serial configuration/output scope.
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

## Planned production semantics

Runtime capabilities, module settings/calibration and output sessions need a shared bounded model, explicit compatibility rules and stable gateway/module identities. Multiple independent chains may reuse node IDs; host routes must resolve the current identity and session rather than treat node IDs as globally unique. Direct USB/CAN inventory merging, cross-chain duplicate-identity diagnostics and migration handling remain future work in Cockpit.

Configuration will allow one writer per module, separate drafts, explicit Apply/Save and interrupted-write protection. Output ownership will use active sessions and fresh commands, with direct USB priority. Firmware update/recovery behavior is not defined by the prototype contracts. Those features must not be inferred from an input heartbeat or gateway registry API.

Generate bindings where supported, otherwise check adapters against shared vectors. Do not assume an available DSDL-to-Rust generator. Keep these contracts as work in progress until going live; production compatibility will require tested firmware/Cockpit combinations and measured limits. No production compatibility matrix has been released.

## Related repositories

- [Device gateway](https://github.com/opensimgear/device-gateway): gateway CAN/CDC/HID implementation and electronics.
- [Device template](https://github.com/opensimgear/device-template): generic Cyphal device with direct USB input ownership.
- [Firmware common](https://github.com/opensimgear/firmware-common): reusable acquisition and USB code; pinned protocol source dependency.
- [Cockpit](https://github.com/opensimgear/cockpit): native host adapters and controller registration UI.
- [Website](https://github.com/opensimgear/opensimgear.github.io): architecture and user documentation.

## Licensing

OpenSimGear license selection remains pending; no license grant is implied for newly authored definitions or reference code. Production protocol, firmware and hardware releases require documented licensing and compatible USB identifiers.
