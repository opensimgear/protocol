# OpenSimGear protocol

Shared wire contracts, reference code and conformance vectors for OpenSimGear firmware and Cockpit. The current implementation is a **private CAN FD v2 prototype** with a separate STM32 gateway CDC v2 API. Canonical Cyphal application schemas and a production protocol release remain future work.

## Implemented contracts

| Contract | Consumers | Current behavior |
| --- | --- | --- |
| [Private CAN FD v2](reference/can-fd-v2/README.md) | STM32 device-gateway, generic Rev B module and Python peer | Identity/profile announcement, node/lease assignment, input reports, heartbeats and USB-completion acknowledgements |
| [STM32 gateway CDC v2](serial/stm32-gateway-v2.md) | Gateway firmware and Cockpit's Rust STM32 adapter | Correlated bounded snapshots, inventory and registry staging, epoch-guarded mutation and explicit persistent Apply layout |

CAN v2 supports up to ten chain-local nodes and profiles with 128 buttons, four hats and eight axes. Direct module USB has input priority. Sequence/session/lease checks reject stale delivery; gateway USB completion credits encoder pulses. Persisted gateway slots survive temporary absence and remain neutral when inputs are unavailable. These application rules are implemented by the consuming firmware, not by Cyphal.

As of 2026-10-12, reference-code/fixture checks and cross-firmware integration pass, including sanitized software runs. Cockpit consumes actual C formatter fixtures. This establishes portable source interoperability; assembled STM32 USB/CAN, timing, power and flash brownout behavior remain unverified.

CAN v2 rejects v1 wire peers. The gateway can migrate its older persisted v1 registry, which is distinct from wire compatibility. The Pico W simulator uses its own protocol-1 adapter and is not a CAN v1/v2 peer. Update matching STM32 module/gateway firmware together. See [compatibility](compatibility/README.md).

## Layout

- [reference/can-fd-v2/](reference/can-fd-v2/README.md): reviewed C codec snapshot and SHA-256 manifest.
- [serial/](serial/README.md): implemented STM32 CDC v2 API and future serial configuration/output scope.
- [test-vectors/](test-vectors/README.md): canonical CAN frames and actual gateway formatter snapshots.
- [tests/](tests/README.md): integration runner linking gateway, generic client and shared acquisition code.
- `tools/verify_can_vendors.py`: checks reference/firmware codec copies and stored or freshly generated fixtures.
- [dsdl/](dsdl/README.md): placeholder for future Cyphal DSDL definitions; no application schemas or port IDs are assigned.
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

Generate bindings where supported, otherwise check adapters against shared vectors. Do not assume an available DSDL-to-Rust generator. Release protocol versions independently with tested firmware/Cockpit combinations and measured limits. No production compatibility matrix has been released.

## Related repositories

- [Device gateway](https://github.com/opensimgear/device-gateway): gateway CAN/CDC/HID implementation and electronics.
- [Device template](https://github.com/opensimgear/device-template): generic module and private v2 client.
- [Firmware common](https://github.com/opensimgear/firmware-common): reusable acquisition and USB code; pinned protocol source dependency.
- [Cockpit](https://github.com/opensimgear/cockpit): native host adapters and controller registration UI.
- [Website](https://github.com/opensimgear/opensimgear.github.io): architecture and user documentation.

## Licensing

OpenSimGear license selection remains pending; no license grant is implied for newly authored definitions or reference code. Production protocol, firmware and hardware releases require documented licensing and compatible USB identifiers.
