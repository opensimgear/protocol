#!/usr/bin/env python3
"""Check the reviewed private CAN v2 codec and optionally both firmware copies."""
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
    reference = Path(__file__).resolve().parents[1] / 'reference/can-fd-v2'
    manifest = json.loads((reference / 'SHA256.json').read_text())
    checked = 0
    for name, expected in manifest.items():
        paths = [reference / name]
        if args.workspace:
            if name.endswith('.h'):
                paths += [args.workspace / 'device-gateway/firmware/include' / name,
                          args.workspace / 'device-template/firmware/shared-gateway/include' / name]
            else:
                paths += [args.workspace / 'device-gateway/firmware/src' / name,
                          args.workspace / 'device-template/firmware/shared-gateway/src' / name]
        for path in paths:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != expected:
                raise SystemExit(f'CAN v2 codec differs from reviewed snapshot: {path}')
            checked += 1
    print(f'{checked} codec files match the reviewed CAN v2 snapshot')
    fixtures = reference.parents[1] / 'test-vectors'
    if args.workspace:
        copies = [
            ('can-fd-v2.json', args.workspace / 'device-gateway/firmware/tests/can-fd-v2-vectors.json'),
            ('stm32-gateway-v2.json', args.workspace / 'cockpit/src-tauri/tests/fixtures/stm32-gateway-v2.json'),
        ]
        for name, path in copies:
            if json.loads(path.read_text()) != json.loads((fixtures / name).read_text()):
                raise SystemExit(f'Conformance fixture differs from canonical snapshot: {path}')
        print('Both firmware/Cockpit fixture copies match canonical snapshots')
    if args.gateway_test:
        for name, option in [('can-fd-v2.json', '--vectors'), ('stm32-gateway-v2.json', '--cockpit-vectors')]:
            fresh = json.loads(subprocess.check_output([str(args.gateway_test), option]))
            if fresh != json.loads((fixtures / name).read_text()):
                raise SystemExit(f'Fresh gateway C output differs from canonical fixture: {name}')
        print('Both canonical fixtures match fresh gateway C output')


if __name__ == '__main__':
    main()
