# Local cross-firmware integration

The optional runner builds the actual gateway core/registry/codec and generic
CAN client/delivery with a local pinned firmware-common input implementation.
It requires adjacent `device-gateway/` and `device-template/` checkouts, the pinned common source and a host C compiler. It never
fetches a branch, flashes a board or changes an existing firmware checkout.

```sh
mkdir -p .research
TMPDIR="$PWD/.research" python3 protocol/tests/run_gateway_generic.py \
  --workspace /path/to/opensimgear \
  --common-source /path/to/pinned/firmware-common \
  --sanitize
```

When omitted, `--common-source` uses
`.research/generic-pinned-arm-example/firmware_common-src` below the workspace.
The runner verifies the gateway and generic vendored CAN codec copies are
identical before linking the gateway implementation. Compilation uses C11,
warnings as errors and enabled assertions; the optional sanitizer flag enables
AddressSanitizer and UndefinedBehaviorSanitizer. Temporary build files are
removed after execution. No unpublished remote commit is required.

The test drives real debounced contacts, eight ADC axes, four hats and a
quadrature encoder with 128 logical buttons. It exchanges actual encoded CAN
frames through ANNOUNCE/ASSIGN, registry serialization, apply and full input
delivery. ACKs come from the gateway's successful USB interrupt completion
function, then pass through the generic client's history/lease checks before
crediting encoder phases. Submission, corrupted completion, stale ACKs and
elapsed time alone cannot release a pending encoder press/release. Direct USB
ownership neutralizes gateway input and invalidates pending delivery; fallback
rearms contacts and requires fresh input. Bus reset rejects stale leases,
temporary absence retains neutral registry slots, and registry restoration
preserves their identities.

This verifies portable firmware interoperability. It does not establish actual
CAN arbitration, USB enumeration, interrupt delivery, physical wiring or flash
power-loss behavior; those require their own board and journal checks. Canonical
wire examples remain under `../test-vectors/`.

On 2026-10-12 this runner passed against the local gateway and generic sources
with AddressSanitizer and UndefinedBehaviorSanitizer enabled. The common source
checkout matched the generic firmware's published pin
`35e06787fb9696977fe92d20c5ff5afc1e3e1ebf`.
