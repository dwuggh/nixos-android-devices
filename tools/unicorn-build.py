#!/usr/bin/env python3
"""Cross-compile Unicorn on the workstation with the native phone as a LAN helper."""

import argparse
import base64
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def phone_builder():
    """Pin the already verified LAN host key without relying on root's SSH config."""
    address = '192.168.3.94'
    home = Path.home()
    known = subprocess.check_output([
        'ssh-keygen', '-F', address, '-f', str(home / '.ssh/known_hosts')
    ], text=True)
    keys = [line.split() for line in known.splitlines() if line and not line.startswith('#')]
    key = next((fields for fields in keys if fields[1] == 'ssh-ed25519'), None)
    if key is None:
        raise ValueError('Missing verified LAN ed25519 host key in ~/.ssh/known_hosts')
    pinned = base64.b64encode(f'{key[1]} {key[2]}'.encode()).decode()
    identity = home / '.ssh/unicorn'
    if not identity.is_file() or any(c.isspace() for c in str(identity)):
        raise ValueError('Builder identity ~/.ssh/unicorn is missing or its path contains whitespace')
    return f'ssh-ng://root@{address} aarch64-linux {identity} 1 1 benchmark,big-parallel,gccarch-armv8-a - {pinned}'


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=source.parent / 'unicorn-config')
    parser.add_argument('--device', action='store_true', help='use device repository outputs instead of personal configuration')
    parser.add_argument('--local-only', action='store_true', help='finish on the workstation without the phone builder')
    parser.add_argument('--local-jobs', type=int, default=1, help='workstation concurrent builds (default: 1); phone has one slot')
    parser.add_argument('--cores', type=int, default=0, help='workstation cores per build (0 = all, default); phone uses all its cores')
    parser.add_argument('action', choices=['sync', 'build', 'plan', 'eval', 'check'])
    parser.add_argument('target', nargs='?', help='artifact (default: system), or a named check')
    args = parser.parse_args()
    if args.local_jobs < 1 or args.cores < 0:
        parser.error('--local-jobs must be positive; --cores must be nonnegative (0 = all cores)')
    config = args.config.resolve()
    helper = config / 'scripts/update-inputs.py'
    spec = importlib.util.spec_from_file_location('unicorn_snapshot', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    cache = Path.home() / '.cache/unicorn-config'
    cache.mkdir(parents=True, exist_ok=True)
    # Keep simultaneous sessions from replacing each other's source snapshots.
    with (cache / 'remote-build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        device = cache / 'android-devices'
        consumer = cache / 'configuration'
        for original, snapshot in [(source, device), (config, consumer)]:
            count = module.snapshot(original, snapshot)
            print(f'Snapshot: {original} -> {snapshot} ({count} files)', file=sys.stderr)
        personal = config / 'inputs/personal'
        if not (personal / 'flake.nix').is_file():
            raise ValueError(f'Missing pinned personal input: {personal}')
        if args.action == 'sync':
            return

        # Use our SSH config's strict host-key checking and LAN-only alias.
        os.environ.setdefault('NIX_SSHOPTS', '-o BatchMode=yes -o ConnectTimeout=10')
        store = 'daemon'
        nix = ['nix', '--extra-experimental-features', 'nix-command flakes',
               '--store', store]
        target = args.target or 'system'
        flake = device if args.device else consumer
        overrides = [] if args.device else [
            '--override-input', 'androidDevices', f'path:{device}',
            '--override-input', 'personal', f'path:{personal}',
        ]
        if args.action == 'check':
            if not args.target:
                parser.error('check requires a named check')
            attribute = f'checks.aarch64-linux.{target}'
        elif args.device:
            attribute = f'legacyPackages.x86_64-linux.devices.unicorn.{target}'
        elif target == 'kernel':
            attribute = 'nixosConfigurations.unicorn.config.androidDevices.kernelPackage'
        else:
            attribute = f'packages.aarch64-linux.{target}'
        installable = f'path:{flake}#{attribute}'
        command = 'eval' if args.action == 'eval' else 'build'
        options = ['--eval-store', 'auto']
        options += ['--no-update-lock-file'] if args.device else ['--no-write-lock-file']
        if command == 'eval':
            run(nix + [command, installable + '.drvPath', '--raw'] + options + overrides)
            return
        # Refuse to silently become a local-only build when the daemon ignores
        # restricted builder settings from an untrusted client.
        info = json.loads(subprocess.check_output(nix + ['store', 'info', '--json'], text=True))
        if not info.get('trusted'):
            raise ValueError('Mixed builds need one-time setup: sudo python3 tools/setup-unicorn-builders.py --user ' + os.environ.get('USER', 'dwuggh'))
        features = subprocess.check_output(nix + ['config', 'show', 'system-features'], text=True).split()
        if 'unicorn-workstation' not in features:
            raise ValueError('Missing unicorn-workstation system feature; run tools/setup-unicorn-builders.py with sudo')
        builders = '' if args.local_only else phone_builder()
        options += ['--no-link', '--print-build-logs', '--max-jobs', str(args.local_jobs), '--cores', str(args.cores),
                    '--builders', builders, '--option', 'builders-use-substitutes', 'true']
        if args.action == 'plan':
            run(nix + ['build', installable, '--dry-run'] + options + overrides)
            return
        # Repository examples have no workstation-specific policy. Realise their
        # shared kernel locally first, so a later mixed build can only reuse it.
        if args.device and args.action != 'check':
            kernel = f'path:{device}#legacyPackages.x86_64-linux.devices.unicorn.kernel'
            local_options = options.copy()
            local_options[local_options.index('--builders') + 1] = ''
            run(nix + ['build', kernel] + local_options + overrides)
        completed = run(nix + ['build', installable, '--json'] + options + overrides,
                        stdout=subprocess.PIPE, text=True)
        result = json.loads(completed.stdout)
        prefix = 'device-' if args.device else ''
        label = prefix + target.replace('/', '-').replace('.', '-')
        root = Path.home() / '.local/state/unicorn-build'
        root.mkdir(parents=True, exist_ok=True)
        manifest = {'store': store, 'installable': installable, 'builders': builders,
                    'local_jobs': args.local_jobs, 'cores': args.cores, 'results': result}
        for index, item in enumerate(result):
            for output, value in item['outputs'].items():
                # Nix versions expose either a string path or an output object.
                path = value if isinstance(value, str) else value['path']
                link = root / f'result-{label}-{index}-{output}'
                run(['nix-store', '--add-root', link, '--indirect', '--realise', path])
                print(f'{output}: {path} (workstation store)', file=sys.stderr)
        state = Path.home() / '.local/state/unicorn-build'
        state.mkdir(parents=True, exist_ok=True)
        manifest_path = state / f'{label}.json'
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Manifest: {manifest_path}', file=sys.stderr)


if __name__ == '__main__':
    try:
        main()
    except (subprocess.CalledProcessError, ValueError, OSError) as error:
        sys.exit(str(error))
