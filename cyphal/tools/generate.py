#!/usr/bin/env python3
"""Regenerate/check committed Nunavut C bindings; scratch files belong in .research/."""
import argparse
import importlib.metadata
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def normalize(text: str, protocol: Path) -> str:
    def source(match):
        return "// Source file:   " + Path(match.group(1)).relative_to(protocol).as_posix()
    text = re.sub(r"^// Source file:   (.+)$", source, text, flags=re.MULTILINE)
    text = re.sub(r"^// Generated at:  .+$", "// Generated at:  reproducible snapshot (see tools/generate.py)", text, flags=re.MULTILINE)
    text = re.sub(r"// Platform\n(?:.*\n)*?// Language Options", "// Language Options", text)
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Compare without modifying maintained files")
    args = parser.parse_args()
    for package, version in (("nunavut", "2.3.1"), ("pydsdl", "1.25.0")):
        if importlib.metadata.version(package) != version:
            raise SystemExit(f"Expected {package}=={version}; install tools/requirements.txt")
    cyphal = Path(__file__).resolve().parents[1]
    protocol = cyphal.parent
    if not (protocol / "dsdl" / "opensimgear").is_dir():
        raise SystemExit("Run the generator from the canonical protocol checkout, which contains dsdl/")
    scratch = protocol.parent / ".research"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cyphal-generated-", dir=scratch) as temporary:
        output = Path(temporary)
        for namespace in ("opensimgear", "uavcan"):
            subprocess.run([sys.executable, "-m", "nunavut", "--target-language", "c", "--language-standard", "c11",
                            "--target-endianness", "any", "--outdir", str(output),
                            str(protocol / "dsdl" / namespace)], check=True)
        expected = set()
        for generated in sorted(output.rglob("*.h")):
            relative = generated.relative_to(output)
            expected.add(relative)
            normalized = normalize(generated.read_text(), protocol)
            destination = cyphal / "generated" / relative
            if args.check:
                if not destination.is_file() or destination.read_text() != normalized:
                    raise SystemExit(f"Generated binding differs: {relative}")
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists():
                    destination.chmod(0o644)
                destination.write_text(normalized)
        existing = {path.relative_to(cyphal / "generated") for path in (cyphal / "generated").rglob("*.h")}
        if existing != expected:
            raise SystemExit(f"Unexpected generated headers: {sorted(existing - expected)}")
    print("PASS: committed bindings match pinned Nunavut/DSDL" if args.check else "Generated canonical Cyphal C bindings")


if __name__ == "__main__":
    main()
