# USB serial protocol

Define bounded framing, addressing, capabilities, configuration, diagnostics, and explicit output-session ownership here. Supply versioned SimHub-compatible command definitions when implemented.

The shared model must distinguish stable gateway identity, stable module identity, and transient chain-local node ID. Define gateway inventory snapshots/change notifications covering capabilities, registration/slot, ownership, health/freshness, and connection/session revision. Commands and responses must identify the intended unit and current session; reject stale address/identity pairings after address reuse or gateway restart. Support independent gateway serial endpoints, direct USB/CAN deduplication, attachment changes, and explicit identity-conflict diagnostics. No physical socket address exists on the gateway's single bus.

No wire format or command API is finalized yet.
