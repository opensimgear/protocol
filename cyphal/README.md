# Shared Cyphal firmware library

This self-contained C11 library is used by both gateway and generic device firmware.
It embeds pinned libcanard 5.0, checked-in Nunavut 2.3.1 C bindings, and the common
application profile/identity helpers. CMake target `osg_cyphal` exports its include
directories and is suitable for native and embedded builds without network access.

`osg_cyphal_init` creates a CAN FD node with a stable hardware UID, boot session,
reversed-domain name and a raw CAN-frame enqueue callback. libcanard immediately
selects a CAN node-ID using an identity/session seed and repairs observed collisions.
Drivers must receive extended data frames broadly enough for occupancy tracking;
they must not feed the node its own transmit loopback frames. Cyphal node-ID migration
requires a fresh application announcement/lease before accepting input from the new
source. The gateway's logical node values 1–10 remain chain-local application routes.

Outgoing application messages use the 13-bit subject-ID transport supported by Cyphal
1.0 and 1.1. The deployment bindings are 2000–2004 as listed in the canonical
protocol checkout's `dsdl/README.md`. Firmware snapshots include the generated
bindings; source DSDL and regeneration are maintained in the protocol checkout.
Each application payload fits in one CAN FD frame, including at the maximum HID shape.
libcanard owns CAN identifier construction, DLC padding, tail bytes, transfer-ID
counters/deduplication, multi-frame CRC and reassembly. Incoming Classic CAN transfers
are supported too. Standard Heartbeat is published on 7509 at one-second intervals;
GetInfo service 430 returns generated standard node information, stable UID and name.

Instances contain a 128-block, 256-byte aligned pool (32 KiB) with constant-time
allocation/release and a 16-frame TX queue. Allocation failure and full queues return
failure without accepting an application transfer. `send` success means transport
queue admission; CAN driver backpressure leaves queued frames pending for `tick` to
retry. The application USB-completion acknowledgement remains the delivery boundary.
Pending application transfers expire after 100 ms; Heartbeat/GetInfo use 1 s deadlines.
Emitted frames carry local `deadline_valid`/`deadline_ms` driver metadata. A hardware
driver must discard an expired software-queued frame before handing it to the CAN
controller, including across the uint32 millisecond wrap. These fields are not wire data.
Memory pressure/expiration appears as advisory Heartbeat health. RX sessions are
reclaimed incrementally by libcanard polling after their timeout; ten sources on all
five application subjects consume 50 blocks and are covered by the portable tests.

Call `tick` every main-loop iteration and whenever the driver can accept more frames.
All receive/send/tick operations must share one serialized execution context.
The `uint32_t` millisecond clock may wrap; small backward snapshots are clamped.
Poll at least once per half clock wrap (about 24.9 days). `reset` purges pending
transfers and RX sessions while retaining transport node-ID, outgoing transfer
counters, heartbeat cadence and uptime. Flush the hardware frame queue at the same
ownership/reset boundary. Do not move or copy initialized instances.

Generate or check canonical bindings from this protocol checkout:

```sh
python3 -m venv ../.research/cyphal-tools
../.research/cyphal-tools/bin/python -m pip install -r cyphal/tools/requirements.txt
../.research/cyphal-tools/bin/python cyphal/tools/generate.py --check
```

The generator normalizes only host-dependent header comments. It leaves generated
serialization code untouched. Generated support code's upstream license is included
as `generated/NUNAVUT_LICENSE.rst`; regulated DSDL provenance/license is in `../dsdl/`.

Portable verification, with all build products under the workspace `.research/`:

```sh
cmake -S protocol/cyphal -B .research/cyphal-build -DOSG_CYPHAL_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS='-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build .research/cyphal-build
ctest --test-dir .research/cyphal-build --output-on-failure
```

Tests cover generated payloads and real libcanard FD/Classic traffic, malformed
frames, duplicates and CRC, standard Heartbeat/multi-frame GetInfo, queue limits,
allocation failure, reset, automatic collision repair, clock snapshots/wrap and ten
remote nodes. Hardware interoperability, bus timing and USB delivery still require
board testing.
