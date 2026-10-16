# USB serial contracts

## Development status and disclaimer

This project is heavily under development and is not ready for use. Use it entirely at your own risk. To the fullest extent permitted by applicable law, the authors and contributors disclaim all liability for any damage, loss, or other issues arising from its use.

[stm32-gateway-v3.md](stm32-gateway-v3.md) documents the current STM32 gateway API: automatic enrollment, ordered enabled controllers, offline ghosts, remembered disabling, guarded offline removal and profile updates, and asynchronous persistence/USB publication. Cockpit's native STM32 adapter consumes the gateway's actual C formatter fixtures. [CDC v2](stm32-gateway-v2.md) remains documented for its separate legacy slot/register/apply adapter.

The generic module's direct USB console provides diagnostics and `config <child-command>` through the shared [runtime settings/calibration handler](runtime-configuration.md). The gateway routes the same commands over Cyphal. Temporary outputs use the separate [device-advertised actions](device-actions.md) handler through direct `action <child-command>` CDC or the same bounded CAN request/reply envelopes. Cockpit discovers typed descriptors and provides manual LED controls without changing settings or HID layout. The current Pico W debug target reuses CDC3 lifecycle/configuration/actions/events with an explicit debug role and guarded input-injection extension documented in [the gateway repository](https://github.com/opensimgear/device-gateway/tree/main/simulation/pico). Its virtual devices use an internal application-message bus, not physical CAN. Older Pico images retain their separate prototype protocol-1 API.

Gateway routes bind gateway identity, module identity and current session; node IDs are chain-local. A physical socket is not an address on the gateway's single shared bus. Missing integration work is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).

[Panel input events](panel-input-events.md) preserve mapped button press/release and encoder-detent transitions in a bounded, nonconsuming log. The separate `events` handler shares the guarded CAN envelopes and uses explicit stream/cursor baselines independently of HID delivery.

Additive schema-1 metadata identifies maintained contacts, encoder pairs and
selector triples, and supplies independently calibrated axis snapshots with
sensor availability. [Display actions](device-actions.md#bounded-display-surfaces)
declare endpoint geometry/encoding and cell/byte limits on readable string
setters. The sealed configuration envelopes and CAN application version stay
unchanged; host bindings validate catalogue interfaces before sending commands.

[Endpoint output ownership](output-ownership.md) adds bounded volatile leases,
validated fallback, competing-client exclusion and output-only enrollment without
a HID collection. The commands reuse the same action dispatcher and sealed routes.
