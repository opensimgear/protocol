# Device-advertised actions

Actions describe and operate temporary device outputs through a shared portable C
handler. Cockpit discovers the catalogue and presents manual controls from its
metadata. The generic device supports its physical board LED mode plus simulated
RGB and boolean LEDs in the native emulator. The Master Caution simulation
profile instead advertises the same board LED action. [Panel input events](panel-input-events.md)
supply the independent input contract used with these outputs by simulator bindings.

Devices may advertise [exclusive endpoint leases](output-ownership.md) with validated
fallbacks, independent expiry and owned invocation. Legacy setters and LED diagnostics
respect a claimed endpoint. Actions do not require a settings writer claim, change HID descriptors or write
flash. Device callbacks retain hardware ownership, apply values and report the
current software state. A successful reply confirms callback application; it does
not measure the electrical LED/display. LED overrides reset to `auto` on device
reboot. Catalogue IDs and endpoint IDs are stable product identifiers; an index
or CAN node ID is not an action identifier.

## Routing and commands

Direct module CDC accepts the same full child command as the CAN device handler:

```text
action describe <offset>
action state
action invoke <catalogue-revision> <action-id> <arguments...>
```

The gateway machine command routes to a current module identity and boot session:

```text
cockpit <request-id> <gateway-epoch> action <module-identity32> <module-boot> <child-command>
```

Here `<child-command>` omits the leading `action`; for example:

```text
cockpit 12 7 action 00000000000000000000000000000001 2 describe 0
cockpit 13 7 action 00000000000000000000000000000001 2 invoke 1 led.status.set_mode on
```

The gateway prepends `action ` to the bounded command carried on existing
`ConfigRequest.1.0` fragments. `ConfigResponse.1.0` returns `OK\n` followed by the
handler's JSON. These sealed DSDL types and subject allocations are unchanged.
The gateway response retains its ordinary inventory snapshot and exposes the
parsed handler result as top-level `targetAction`, a sibling of `state`, for this
request only. Settings requests continue to use top-level `targetConfig`.

The existing route/source/identity/boot/lease/epoch checks, fragment assembly and
last-completed-request reply cache apply to actions too. A duplicate of that
completed request returns its retained reply without invoking again. A fresh
request ID is a new invocation. No exactly-once promise survives cache eviction,
route changes or reboot. Setters can be reconciled through `state`; a one-shot
whose reply was lost must not be automatically resent.

A device sends application Status periodically regardless of HID enablement or
direct USB input ownership. Fresh accepted Status proves the action route after
lease changes; a disabled or direct USB device can therefore receive actions
without generating HID input. Standard node Heartbeat alone does not prove the
application lease. Offline modules and stale routes cannot execute actions.

Outer routing errors remain `configuration_busy`, `configuration_route_not_ready`,
`configuration_route_changed`, `configuration_timeout`, `stale_epoch`,
`module_offline`, `stale_module_boot`, `invalid_request` and
`invalid_module_response`. A read may retry after a fresh snapshot when the route
is not ready. An ambiguous invocation requires readback/reconciliation.

## Catalogue schema

`describe` uses a zero-based offset and returns up to four actions. Pages are also
bounded by encoded JSON bytes, so clients follow `next` even when a page contains
fewer than four entries. `next` is null at the end. Offset equal to `total` returns
an empty terminal page. The schema version identifies the JSON format; the nonzero
u32 catalogue revision identifies that device's action definitions. It is
independent of settings generation, profile revision and PCB version.

```json
{
  "ok": true,
  "schemaVersion": 1,
  "catalogueRevision": 1,
  "total": 1,
  "next": null,
  "actions": [{
    "id": "led.status.set_mode",
    "label": "Board status LED",
    "endpoint": "led.status",
    "semantics": "setter",
    "readable": true,
    "parameters": [{
      "id": "mode",
      "label": "Mode",
      "type": "enum",
      "options": ["auto", "on", "off"]
    }]
  }]
}
```

Actions have stable `id`, user-facing `label`, affected `endpoint`, `semantics`
(`setter` or `oneShot`), `readable` and ordered `parameters`. A setter assigns a
complete desired state and is idempotent for identical arguments. A one-shot
triggers an operation and has separate retry behavior. Zero parameters are valid.
An endpoint may have multiple actions, but action IDs within one catalogue and
parameter IDs within one action must be unique.

| Parameter type | Descriptor constraints | Command token | JSON value |
| --- | --- | --- | --- |
| `boolean` | None | `0` or `1` | Boolean |
| `integer` | Signed 32-bit `minimum` and `maximum`, inclusive | Signed decimal; no leading plus | Number |
| `enum` | `options`: 1–16 unique identifier strings | One advertised option | String |
| `rgb` | Three 8-bit color channels | Exactly six hex digits `RRGGBB` | Uppercase six-digit string |
| `string` | `maxBytes`: 1–63 UTF-8 bytes | UTF-8 encoded as hex, or `-` for empty | String |

Identifiers are 1–31 ASCII letters, digits, `.`, `_` or `-`. Labels contain 1–63 UTF-8 bytes. Labels and string values must be valid UTF-8 without NUL,
Unicode C0, DEL or C1 controls. Display text can contain spaces, quotes and backslashes;
hex command encoding prevents those characters from changing token boundaries.
There are at most 16 actions and four parameters per action. Devices reject a
catalogue whose worst-case invocation, descriptor or aggregate readable state
would exceed the transport budget, even if its counts fit these limits.

Clients assemble all catalogue pages with an unchanged revision and total,
reject duplicate IDs and validate all metadata before presenting controls.
A catalogue revision change invalidates cached descriptors and parameter values.
Saved bindings in later milestones must revalidate against fresh metadata.

### Bounded display surfaces

A readable setter with exactly one `string` parameter may advertise optional
`display` metadata on the action:

```json
{"id":"radio.frequency.set_text","label":"Radio frequency","endpoint":"radio.frequency","semantics":"setter","readable":true,"display":{"columns":7,"rows":1,"encoding":"ascii"},"parameters":[{"id":"text","label":"Text","type":"string","maxBytes":7}]}
```

`columns` is 1–63, `rows` is 1–4 and their product is at most 63 logical cells.
Encoding is `ascii` or `utf8`; `maxBytes` retains its independent 1–63 byte bound
and must be at least the cell count. ASCII accepts printable U+0020–U+007E,
one byte per cell. UTF-8 uses **one Unicode scalar per logical cell**; combining
marks and wide glyphs do not receive special width treatment. The hardware
callback owns font/repertoire support, and a device should advertise ASCII when
that matches its character interface. A label's UTF-8 encoding does not imply
its output accepts those characters.

One invocation assigns the whole surface in row-major order. No newline or other
control character marks a row; clients format/pad rows with spaces, then flatten
them. Empty/short values are valid but still mean a complete desired surface,
not a partial-write operation. Host formatting must satisfy both cell and byte
budgets, truncate only on scalar boundaries and define its ASCII replacement
policy. The shared handler validates input before the callback and validates
readback against the same geometry/encoding. Success describes applied software
state, not electrical display verification or observed pixels.

This is additive schema-1 metadata. Older consumers may ignore it and still
invoke bounded string actions; newer bindings require fresh display metadata
before relying on dimensions. An absent `display` retains the original UTF-8
string contract. Product descriptors come from actual output-port capabilities;
an LED-only board must not advertise an unwired display. Device-specific action
IDs, catalogue revisions and callbacks remain product-owned.

## State and invocation replies

`state` includes one entry for each readable action; `values` uses the descriptor's
parameter order and JSON types. Unreadable actions have no entry.

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"states":[{"id":"led.status.set_mode","values":["auto"]}]}
```

`invoke` checks the exact catalogue revision, advertised action ID, argument
count, types and bounds before calling the device. For a readable action its
reply obtains validated current state from the device after application. For an
unreadable action it echoes the validated input values as its acknowledgement.

```json
{"ok":true,"schemaVersion":1,"catalogueRevision":1,"id":"led.status.set_mode","values":["on"]}
```

Recognized action failures return `{"ok":false,"error":"<token>"}`. Tokens are
`invalidCatalogue`, `replyTooSmall`, `invalidArgument`, `unknownCommand`,
`staleCatalogue`, `unknownAction` and `failed`. `failed` can mean the callback
failed or its readback was invalid; the device may already have applied an
invocation before readback failed. Clients show the error and refresh state.
Nested `ok:false` is distinct from an outer routing failure. Legacy firmware
without this handler may return outer `unsupported` or `unknown_command`; clients
must present action support as unavailable.

The full module command is at most 383 bytes. The shared handler requires a
4096-byte reply buffer and emits at most 4091 bytes of JSON, leaving room for the
`OK\n` framing within the existing 4095-byte CAN reply limit. Transport fragments
carry 24 data bytes. Discovery and manual actions are low-rate operations; later
simulator bindings need measured coalescing/batching rather than polling complete
catalogues for every output update.

## Firmware and manual-control boundary

`opensimgear/actions.h` in firmware-common validates immutable device-owned
descriptors and dispatches the command to device-owned invoke/read callbacks.
Both direct CDC and routed CAN call it. The board action `led.status.set_mode`
has `auto`, `on` and `off`: auto uses the existing device health blink; overrides
hold the physical status LED steadily on or off. This changes no board copper,
BOM or PCB version. The native emulator advertises
`led.rgb.set_color` and `led.boolean.set_enabled` instead of the hardware LED mode, with independently readable
software state and reboot reset.

Cockpit binds a manual request to gateway identity, module identity and boot;
it validates the complete catalogue and types before serializing arguments.
After reconnect or reboot it discards stale results, reloads the catalogue and
reads current state. Manual actions remain available for online disabled/direct
USB modules. Manual controls and simulator bindings use the same descriptors and typed invocation contract; binding lifecycle behavior is defined by the host application.
