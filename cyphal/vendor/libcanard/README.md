# Pinned libcanard

## Development status and disclaimer

This project is heavily under development and is not ready for use. Use it entirely at your own risk. To the fullest extent permitted by applicable law, the authors and contributors disclaim all liability for any damage, loss, or other issues arising from its use.

Unmodified source snapshot from [OpenCyphal/libcanard](https://github.com/OpenCyphal/libcanard),
commit `120600375535b6a383fbe99a9d77c04b7a65e81a`, library version 5.0,
Cyphal/CAN 1.1. `canard.c` and `canard.h` came from `libcanard/`;
`cavl2.h` came from `lib/cavl2/` in the same commit. The upstream MIT license is included.

One instance is used for the one physical CAN controller. Its occupancy tracking
and collision repair provide the transport CAN node-ID. Logical gateway routes
and permanent USB registry slots are separate application state.
