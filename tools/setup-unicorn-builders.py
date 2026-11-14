#!/usr/bin/env python3
"""One-time workstation Nix permission setup; run with sudo."""

import argparse
import os
from pathlib import Path
import pwd
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user', default=os.environ.get('SUDO_USER'))
    args = parser.parse_args()
    if os.geteuid() != 0 or not args.user or args.user == 'root':
        parser.error('run with sudo, specifying the workstation user with --user if needed')
    user = pwd.getpwnam(args.user)
    if not (Path(user.pw_dir) / '.ssh/unicorn').is_file():
        parser.error('missing existing ~/.ssh/unicorn identity')
    if not Path('/proc/sys/fs/binfmt_misc/qemu-aarch64').is_file():
        parser.error('qemu-aarch64 binfmt registration is missing')
    conf = Path('/etc/nix/nix.conf')
    text = conf.read_text()
    include = '!include /etc/nix/unicorn-builders.conf'
    backup = conf.with_name(f'nix.conf.before-unicorn-{time.time_ns()}')
    backup.write_text(text)
    # Nix trusted users can control builders and are effectively administrators.
    Path('/etc/nix/unicorn-builders.conf').write_text(
        '# Managed by tools/setup-unicorn-builders.py\n'
        f'extra-trusted-users = {user.pw_name}\n'
        'extra-system-features = unicorn-workstation\n'
    )
    if include not in text.splitlines():
        conf.write_text(text.rstrip() + '\n' + include + '\n')
    # The installed service uses KillMode=process: existing worker processes
    # survive restarting the listener. Refuse to risk active builds otherwise.
    mode = subprocess.check_output(
        ['systemctl', 'show', 'nix-daemon.service', '-p', 'KillMode', '--value'], text=True
    ).strip()
    if mode != 'process':
        raise SystemExit('Config written. Restart nix-daemon manually when existing builds finish (KillMode is not process).')
    subprocess.run(['systemctl', 'restart', 'nix-daemon.service'], check=True)
    print(f'Configured {user.pw_name}; backup: {backup}')
    print('Existing build workers preserved; no global remote builder or concurrency settings changed.')


if __name__ == '__main__':
    main()
