#!/usr/bin/env python3
"""Optional integration of adjacent local firmware checkouts; never fetch or flash."""
from __future__ import annotations

import argparse
import hashlib
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
    args = parser.parse_args()
    workspace = args.workspace.expanduser().resolve()
    common = (args.common_source or workspace / ".research/generic-pinned-arm-example/firmware_common-src").expanduser().resolve()
    gateway = workspace / "device-gateway/firmware"
    generic = workspace / "device-template/firmware"
    compiler = shutil.which(args.cc)
    if compiler is None:
        parser.error(f"Host C compiler is unavailable: {args.cc}")
    # Generic ships the codec for independent builds. Reject a stale vendored
    # copy before linking the real gateway codec into this integration binary.
    for relative in ("include/opensimgear/gateway_protocol.h", "src/gateway_protocol.c"):
        source = gateway / relative
        copy = generic / "shared-gateway" / relative
        if not source.is_file() or not copy.is_file():
            parser.error(f"Required CAN v2 codec source is missing: {source} or {copy}")
        if source.read_bytes() != copy.read_bytes():
            parser.error(f"Gateway and generic CAN codec copies differ: {relative}")
    sources = [
        Path(__file__).resolve().with_name("gateway_generic_integration.c"),
        gateway / "src/gateway.c",
        gateway / "src/gateway_protocol.c",
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
    with tempfile.TemporaryDirectory(prefix="osg-gateway-generic-") as temporary:
        executable = Path(temporary) / "gateway-generic-integration"
        command = [compiler, *flags, "-I", str(gateway / "include"), "-I", str(generic / "include"),
                   "-I", str(common / "include"), *(str(source) for source in sources), "-o", str(executable)]
        print(f"Workspace: {workspace}", flush=True)
        print(f"Common source: {common}", flush=True)
        print("Compile: " + shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
        digest = hashlib.sha256((gateway / "src/gateway_protocol.c").read_bytes()).hexdigest()
        print(f"Matched gateway/generic codec SHA-256: {digest}", flush=True)


if __name__ == "__main__":
    main()
