# Conformance vectors

`can-fd-v2.json` contains fresh C codec frames for all five types and maximum
profiles. `stm32-gateway-v2.json` contains actual C formatter snapshots for
discovered, pending, active, direct USB, offline, stale epoch and changed profile
states. Gateway tests generate these with `osg_gateway_test --vectors` and
`--cockpit-vectors`; Cockpit Rust tests decode a vendored copy of the latter.
These exercise prototype v2 interoperability, not the future Cyphal schema.

Future canonical vectors must also cover simultaneous independent gateways,
cross-chain identity conflicts and output/configuration session routing.
