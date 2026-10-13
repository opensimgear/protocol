# Conformance vectors

`can-fd-v2.json` retains historical private codec frames for all five types and maximum
profiles. It is not a Cyphal fixture. `stm32-gateway-v2.json` retains historical
slot/register/apply snapshots for the legacy Cockpit adapter. `stm32-gateway-v3.json`
contains current automatic-lifecycle C formatter snapshots. Gateway tests generate
the current CDC v3 snapshots with `osg_gateway_test --cockpit-vectors`; Cockpit Rust
tests decode a vendored copy of both versions. The current CDC interface reports
`protocol=3`, `canProtocol=3` and `lifecycle="automatic"`. Canonical Cyphal tests
exercise generated payloads, actual libcanard transfers and node services.

Conformance expansion is tracked in the [GitHub project](https://github.com/orgs/opensimgear/projects/2).
