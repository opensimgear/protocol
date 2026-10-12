#!/usr/bin/env python3
"""Check canonical Cyphal snapshots and gateway CDC conformance fixtures."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, help='Parent containing device-gateway and device-template')
    parser.add_argument('--gateway-test', type=Path, help='Fresh native osg_gateway_test executable')
    args = parser.parse_args()
    protocol = Path(__file__).resolve().parents[1]
    reference = protocol / 'cyphal'
    manifest = json.loads((reference / 'SHA256.json').read_text())
    checked = 0
    for name, expected in manifest.items():
        paths = [reference / name]
        if args.workspace:
            paths += [args.workspace / repo / 'firmware/shared-cyphal' / name
                      for repo in ('device-gateway', 'device-template')]
        for path in paths:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != expected:
                raise SystemExit(f'Cyphal source differs from canonical snapshot: {path}')
            checked += 1
    for root in ([reference] + ([args.workspace / repo / 'firmware/shared-cyphal'
                               for repo in ('device-gateway', 'device-template')]
                              if args.workspace else [])):
        actual = {str(path.relative_to(root)) for path in root.rglob('*')
                  if path.is_file() and path.name != 'SHA256.json'}
        if actual != set(manifest):
            raise SystemExit(f'Unexpected or missing Cyphal snapshot files: {root}')
    print(f'{checked} Cyphal files match the canonical snapshot')
    fixtures = protocol / 'test-vectors'
    if args.workspace:
        copies = [
            ('stm32-gateway-v2.json', args.workspace / 'cockpit/src-tauri/tests/fixtures/stm32-gateway-v2.json'),
        ]
        for name, path in copies:
            if json.loads(path.read_text()) != json.loads((fixtures / name).read_text()):
                raise SystemExit(f'Conformance fixture differs from canonical snapshot: {path}')
        print('Cockpit CDC fixture matches canonical snapshot')
    if args.gateway_test:
        for name, option in [('stm32-gateway-v2.json', '--cockpit-vectors')]:
            fresh = json.loads(subprocess.check_output([str(args.gateway_test), option]))
            if fresh != json.loads((fixtures / name).read_text()):
                raise SystemExit(f'Fresh gateway C output differs from canonical fixture: {name}')
        print('Canonical CDC fixture matches fresh gateway C output')


if __name__ == '__main__':
    main()
