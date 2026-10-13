# Conformance vectors

`can-fd-v2.json` retains historical private codec frames for all five types and maximum
profiles. It is not a Cyphal fixture. `stm32-gateway-v2.json` contains actual C formatter snapshots for
discovered, pending, active, direct USB, offline, stale epoch and changed profile
states. Gateway tests generate the current CDC snapshots with
`osg_gateway_test --cockpit-vectors`; Cockpit Rust tests decode a vendored copy.
The CDC interface remains v2 and reports `canProtocol=3`. Canonical Cyphal tests
exercise generated payloads, actual libcanard transfers and node services.

Conformance expansion is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).
