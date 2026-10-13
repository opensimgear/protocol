# Runtime module configuration

The work-in-progress configuration flow uses the same portable module handlers
on the STM32 target and in the native firmware emulator. Cockpit talks to the
gateway's correlated CDC console; the gateway forwards bounded command and reply
chunks over Cyphal to the identified module. Direct module CDC uses the same
configuration handler. Temporary device outputs use the separate
[device-advertised action handler](device-actions.md) carried by the same bounded
transport; actions do not claim or persist configuration.

## Configuration and ownership

A module owns bounded storage for its name, expansion-bank mask, button mappings,
axis mappings and calibration, hats, encoders and selectors. Configuration records
contain values, rather than pointers into a compiled profile. The input profile,
report packing and USB HID descriptor are derived from validated active settings.

An exclusive writer claim creates a draft from the active configuration. A writer
must use the current module boot session and live route. Competing
writers receive a busy error. Expiry, route loss and reboot require a fresh claim;
an old draft must not silently become a new session's configuration. Reading or
editing a draft does not change the active HID layout.

Apply validates and activates a draft. Save persists active settings through a
two-page journal. Apply and save validates and persists the draft before
activation. A failed save leaves the last committed record readable. Discard
restores the draft from active settings, and release ends the writer session.
Settings storage and the monotonic boot counter use separate flash pages.
An erased persistent store first commits compiled defaults as generation 1, so
the first user save has a valid fallback record. Interrupted factory initialization
can retry only an incomplete deterministic factory record while the second page
is still erased; arbitrary damaged nonblank settings fail closed.

The gateway machine command is:

```text
cockpit <request-id> <gateway-epoch> config <module-identity32> <module-boot> <child-command>
```

The response retains the ordinary gateway snapshot and adds `targetConfig`
for this request. Request identifiers correlate replies; reads and
mutations both use a fresh route. The child-command grammar is:

| Command | Arguments after the command name |
| --- | --- |
| `get`, `get_active` | None; `get` reads the writer's draft if claimed, `get_active` always reads active settings |
| `claim`, `release`, `discard`, `reset` | Nonzero writer token; reset restores the compiled default into the draft |
| `counts` | Writer, button count, axis count, hat count, encoder count, selector count, expansion-bank bit mask |
| `name` | Writer, UTF-8 name encoded as hex; at most 63 UTF-8 bytes |
| `button` | Writer, index, input, maintained (0/1), exclusive group |
| `axis` | Writer, index, ADC channel, HID usage, raw minimum, raw maximum, neutral, inversion (0/1), deadzone |
| `hat` | Writer, index, up, right, down, left inputs |
| `encoder` | Writer, index, phase A input, phase B input |
| `selector` | Writer, index, low input, high input |
| `apply`, `save`, `apply_save` | Writer |

Indexes are zero-based. Inputs 0–23 select direct contacts; inputs 24–151 select
TCA9555 contacts and require the corresponding enabled bank. Encoders use direct
contacts. The effective HID button count includes two per encoder and three per
selector and cannot exceed 128; axes and hats are bounded by eight and four.
Axis calibration endpoints are raw ADC values from 0 to 4095 with minimum below
maximum. HID usages are 48–56 and must be unique. Neutral uses unsigned HID units
0–65535; deadzone uses 0–32767 units and rescales the remaining range around the
neutral value. Existing contact debounce remains fixed at 10 ms.

Snapshots include `generation`, `savedGeneration`, `owner`, `claimExpiresMs`,
`dirty`, `pendingApply`, `view`, `valid`, `config`, `layout` and `descriptorHex`.
Mappings are compact numeric tuples in `config.buttons`, `axes`, `hats`,
`encoders` and `selectors`, in the same argument order as their respective
commands after writer and index. This keeps maximum-size profiles within the
bounded response. An invalid draft has no layout or descriptor and cannot apply.

## Descriptor changes

Changing a name or controller shape is an explicit USB lifecycle operation:
quiesce and invalidate old reports, detach, activate the validated profile,
rebuild descriptors and reconnect. Inputs must be freshly acquired and rearmed
before delivery resumes. A completion from the old layout cannot acknowledge a
new encoder event or revive an invalidated report.

The module announces its effective profile and independent configuration generation
on CAN after activation. A generation change renews the route lease even when the
HID shape is unchanged. The gateway refreshes the pending registered profile from
that announcement. Its active USB layout remains fixed until the user explicitly
applies the gateway layout. A profile mismatch produces neutral
input in the retained slot. The registered profile includes control counts, axis
usages and neutral values. Changing a neutral requires gateway Apply even when
descriptor bytes are unchanged. Names, raw calibration endpoints, inversion and
deadzone preserve the registered profile. Discovery, a temporary disconnect and calibration
draft edits do not remove or renumber registered slots.

## Routing and bounded transport

Configuration commands and replies are carried on local application subjects
2005 and 2006. Each fragment carries the stable module identity, boot session,
logical route, lease, gateway epoch, request identifier, total size and offset.
The gateway and module verify the current Cyphal source as well as these fields.
Node IDs alone are insufficient to identify a configuration target.

Each configuration payload is 62 bytes, including up to 24 data bytes. Commands
are bounded to 383 bytes and replies to 4095 bytes. Fragment loss, duplication,
reordering, inconsistent lengths or a changed route must not execute a partial
command. A repeat of the last completed request returns its retained reply rather than
repeating a settings mutation. A lost commit reply requires reading back active
and saved generations before the client decides whether another action is needed.

## Native acceptance boundary

The native emulator executes the production C configuration, journal,
descriptor, gateway console and Cyphal code. Cockpit's Rust STM32 service uses its
ordinary parsing and configuration paths against this process. The development
browser bridge forwards requests to that service instead of constructing firmware
snapshots in JavaScript.

Tests inspect generated descriptor bytes, packed inputs, active and saved
generations, writer ownership, simulated flash failures, restart restoration and
the USB detach/rebuild/reconnect state machine. The emulator does not create an
OS USB device. STM32 HAL and physical USB enumeration, electrical CAN behavior
and real power-loss timing require separate board acceptance.
