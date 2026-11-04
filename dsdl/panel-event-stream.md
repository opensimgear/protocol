# CAN panel event stream

Subject 2007 carries `opensimgear.input.PanelEvent.1.0`. Each variant fits in
one CAN FD frame with its transport tail. The module's existing input journal
is the source. One publisher per assigned route reads its own cursor without
consuming the journal or crediting HID delivery.

The receiver binds the sender CAN node to an announced hardware UID and checks
logical slot, boot session, lease, gateway epoch and configuration generation
before using any payload. Sequence numbers start at one and increase for every
admitted transfer in this scope. A publisher changes scope before resetting
sequence. Exhaustion withdraws the stream and requires a new scope.

| Variant | Meaning |
| --- | --- |
| Layout | Contact count, maintained contact mask, encoder pair count and selector triple count. Layout is immutable in a configuration generation. Axis count, usages and neutral values come from the matching announcement. |
| State | Complete held mask and calibrated axes, with axis availability. The cursor is the edge head represented by this state. The capture clock is the module's millisecond sample clock. |
| Edges | One to six consecutive journal edges ending at the packet cursor. Each retains its original 32-bit capture timestamp. Control occupies the lower seven bits and pressed the high bit. |
| Gap | Reason 1 denotes journal continuity loss. This includes overflow, an expired pending record or a local reset within the same wire scope. Reason 2 denotes counter exhaustion. A gap carries no input state or occurrence. |

The layout order is contacts, encoder pairs and selector triples. Packed masks
use the least significant bit first. A receiver validates total controls and
axis fields against the route profile. Bits for unavailable controls or axes
must fall within the declared shape. Module millisecond timestamps may wrap.
The receiver carries capture evidence and its own receive time separately.

A publisher emits layout and current state when a route first becomes usable.
State then follows committed changes or source health deadlines. When edges
are pending, publish them before any state that includes them. Edge batches
can be sent as soon as data is available, without waiting for a batch to fill.
Layout is repeated at bounded source health intervals so a lost layout can
recover without a periodic state request. Quiet health records witness actual
samples. Transport receipt alone cannot make frozen input fresh.

A sequence gap, a ring gap, timeout or route withdrawal invalidates the
consumer's current continuity. The consumer withdraws old state and requires a
fresh complete state and matching layout before accepting more edges. A fresh
baseline anchors the current head without replaying held controls. Late,
duplicate or mismatched-route packets cannot restore continuity. Transmit
backpressure keeps the current bounded record immutable or reports explicit
loss. Sequence gaps detect transfers lost after transport queue admission.

The codec rejects incomplete variants and nonzero FD padding. The Cyphal
consumer explicitly enables the subscription. Publishing nodes avoid receive
sessions for other panels. This contract does not add a host acquisition
timer. Gateway receive capacity and shared CDC forwarding are qualified with
the production gateway integration.
