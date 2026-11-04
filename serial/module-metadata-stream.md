# Committed module configuration notifications

Direct module CDC accepts `metadata subscribe` and `metadata unsubscribe`.
Their JSON replies are respectively `{"ok":true,"streaming":true}` and
`{"ok":true,"streaming":false}`. Subscription is passive: it neither claims
configuration nor renews editing or output authority. DTR loss, USB unmount and
configuration re-enumeration end the subscription.

After the subscribe reply, the device emits a baseline and then notices only
when committed runtime metadata changes:

```text
@meta {"boot":7,"sequence":1,"kind":"baseline","revision":4,"exhausted":false,"state":{"generation":1,"savedGeneration":1,"owner":9,"claimExpiresMs":31000,"dirty":false,"pendingApply":false}}
@meta {"boot":7,"sequence":2,"kind":"change","revision":5,"exhausted":false,"state":{"generation":1,"savedGeneration":1,"owner":0,"claimExpiresMs":0,"dirty":false,"pendingApply":false}}
```

Every field shown is required. Counters and timestamps are unsigned 32-bit
integers; flags are JSON booleans. Boot, sequence, revision and generation are
nonzero. The complete line, including CRLF, fits 384 bytes. Unknown fields,
duplicate fields and malformed values are errors. Lines never interleave with
input event lines or command replies; partially written lines are immutable.
Commands have priority between complete stream lines.

The central configuration runtime starts revision at one. A successful claim,
renewal, setter, discard, reset, save, accepted apply, completed apply or actual
claim withdrawal increments it. Expiry and configuration writes received over
CAN use that same runtime. Quiet reads, rejected commands and repeated
invalidation of an already withdrawn claim do not increment it. Command
configuration snapshots include `revision` and `revisionExhausted` from the same
committed state, allowing a host to correlate replies with notices.

Notices contain absolute metadata, not draft configuration bodies. Revisions may
coalesce while output is blocked; each transmitted line has the next consecutive
sequence. A fresh subscription starts at sequence one with `kind: baseline`;
subsequent lines use `kind: change`. Configuration and saved generations cannot
decrease within a boot. Saved generation is at most generation, except an
accepted pending apply may already have saved generation plus one. An unowned
claim has zero expiry; an owned claim may have zero expiry when the device clock
wraps. Hosts must not compare that clock directly with their wall clock.

Counters never wrap. Runtime revision exhaustion or the final stream sequence
emits one terminal `exhausted: true` notice. Exhaustion may reuse the preceding
revision once and ends the subscription. No later line can restore healthy
metadata within that subscription. The runtime rejects new mutations after
revision exhaustion but still permits safe release/expiry and completion of an
already accepted apply. A host must withdraw cached authority on loss, malformed
data, sequence gaps or terminal exhaustion and establish a fresh verified source.

A metadata notice is evidence of source state, never a grant of a host editing
capability. Hosts must preserve command-receipt ordering, prevent older notices
from rolling back newer replies, and invalidate private authority after an
unacknowledged external ownership transition. Active configuration bodies can be
revalidated on a generation-change notice; periodic configuration queries are
not part of acquisition. Input events retain their independent ordered stream.

Host configuration mutations use `config if_revision <revision> <child-command>`.
The runtime processes actual lease expiry, compares the expected nonzero revision
and executes the child under the same serialized owner. A mismatch returns
`ERR revision_changed` plus the current configuration snapshot without executing
the child. This wrapper accepts mutations, not `get`/`get_active`. A host uses its
private last acknowledged writer revision for owned mutations; a plain read or
public notice cannot advance that capability. Fresh explicit claims use the
latest verified metadata revision. Successful mutations confirm exactly the next
revision; rejected guarded commands are never automatically replayed. This also
fences expiry and reacquisition that happen to reuse the same numeric owner.
