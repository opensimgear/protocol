#!/usr/bin/env python3
"""Optional integration of adjacent local firmware checkouts; never fetch or flash."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True,
                        help="Directory containing device-gateway/ and device-template/ checkouts")
    parser.add_argument("--common-source", type=Path,
                        help="Pinned firmware-common source checkout (no download is attempted)")
    parser.add_argument("--cc", default="cc", help="Host C compiler executable")
    parser.add_argument("--sanitize", action="store_true", help="Enable AddressSanitizer and UndefinedBehaviorSanitizer")
    parser.add_argument("--stress-seed", type=lambda value: int(value, 0),
                        help="Replay one nonzero 32-bit stress seed (decimal or 0x hex); default runs three fixed seeds")
    args = parser.parse_args()
    if args.stress_seed is not None and not 1 <= args.stress_seed <= 0xffffffff:
        parser.error("--stress-seed must be in 1..4294967295")
    workspace = args.workspace.expanduser().resolve()
    common = (args.common_source or workspace / ".research/generic-pinned-arm-example/firmware_common-src").expanduser().resolve()
    gateway = workspace / "device-gateway/firmware"
    generic = workspace / "device-template/firmware"
    compiler = shutil.which(args.cc)
    if compiler is None:
        parser.error(f"Host C compiler is unavailable: {args.cc}")
    # Both devices carry the canonical stack for independent offline builds.
    canonical = Path(__file__).resolve().parents[1] / "cyphal"
    for source in canonical.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(canonical)
        for root in (gateway, generic):
            copy = root / "shared-cyphal" / relative
            if not copy.is_file() or source.read_bytes() != copy.read_bytes():
                parser.error(f"Cyphal snapshot differs from canonical source: {copy}")
    cyphal = gateway / "shared-cyphal"
    sources = [
        Path(__file__).resolve().with_name("gateway_generic_integration.c"),
        Path(__file__).resolve().with_name("multi_device_integration.c"),
        gateway / "src/gateway.c",
        cyphal / "src/gateway_protocol.c",
        cyphal / "src/cyphal.c",
        cyphal / "vendor/libcanard/canard.c",
        gateway / "src/gateway_registry.c",
        generic / "src/can_client.c",
        generic / "src/can_delivery.c",
        common / "src/inputs/button.c",
        common / "src/inputs/inputs.c",
        common / "src/inputs/profile.c",
    ]
    for source in sources:
        if not source.is_file():
            parser.error(f"Required source is missing: {source}")
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-UNDEBUG"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    scratch = workspace / ".research"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="osg-gateway-generic-", dir=scratch) as temporary:
        executable = Path(temporary) / "gateway-generic-integration"
        command = [compiler, *flags, "-I", str(gateway / "include"), "-I", str(generic / "include"),
                   "-I", str(common / "include"), "-I", str(cyphal / "include"),
                   "-I", str(cyphal / "generated"), "-I", str(cyphal / "vendor/libcanard"),
                   *(str(source) for source in sources), "-o", str(executable)]
        print(f"Workspace: {workspace}", flush=True)
        print(f"Common source: {common}", flush=True)
        print("Compile: " + shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        # Keep CI's default seed corpus independent of inherited overrides.
        environment.pop("OSG_CYPHAL_STRESS_SEED", None)
        if args.stress_seed is not None:
            environment["OSG_CYPHAL_STRESS_SEED"] = str(args.stress_seed)
        subprocess.run([str(executable)], check=True, env=environment)
        digest = hashlib.sha256((cyphal / "src/cyphal.c").read_bytes()).hexdigest()
        print(f"Matched canonical/gateway/generic Cyphal transport SHA-256: {digest}", flush=True)


if __name__ == "__main__":
    main()
