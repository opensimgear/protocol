# USB serial contracts

[stm32-gateway-v2.md](stm32-gateway-v2.md) documents the implemented private STM32 gateway API: bounded correlated snapshots, discovered capabilities, pending/active controller slots, epoch-guarded register/unregister, routed module configuration and explicit persistent Apply layout. Cockpit's native STM32 adapter consumes the gateway's actual C formatter fixtures.

The generic module's direct USB console provides diagnostics and `config <child-command>` through the shared [runtime settings/calibration handler](runtime-configuration.md). The gateway routes the same commands over Cyphal. The Pico W simulator has a separate protocol-1 API documented in [its repository](https://github.com/opensimgear/cockpit/tree/main/simulator/pico); neither API is the finalized production serial protocol.

Gateway routes bind gateway identity, module identity and current session; node IDs are chain-local. A physical socket is not an address on the gateway's single shared bus. Missing integration work is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).
