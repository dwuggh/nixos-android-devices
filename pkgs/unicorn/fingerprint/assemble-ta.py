#!/usr/bin/env python3
"""Install only the hash-checked signed TA candidates, never device state."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile


def assemble(archive, manifest, output):
    metadata = json.loads(Path(manifest).read_text())
    archive = Path(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != metadata['archive_sha256']:
        raise ValueError('TA archive hash mismatch')
    files = {item['path']: item for item in metadata['files']}
    expected = {f'{name}.{suffix}' for name in ('gfuni', 'gfuni6')
                for suffix in ('mdt', *(f'b{i:02}' for i in range(9)))}
    if set(files) != expected or len(metadata['files']) != len(expected):
        raise ValueError('unexpected TA candidate manifest')
    # Validate every member before creating any output; extractfile avoids tar
    # path/link semantics and the exact allowlist excludes calibration and state.
    contents = {}
    with tarfile.open(archive, 'r:xz') as source:
        for member in source:
            if not member.isfile() or member.name not in expected or member.name in contents:
                raise ValueError('unexpected archive member')
            item = files[member.name]
            if member.size != item['bytes']:
                raise ValueError('TA length mismatch')
            data = source.extractfile(member).read()
            if hashlib.sha256(data).hexdigest() != item['sha256']:
                raise ValueError('TA file hash mismatch')
            contents[member.name] = data
    if set(contents) != expected:
        raise ValueError('missing TA segment')
    destination = Path(output) / 'lib/firmware'
    destination.mkdir(parents=True)
    for name, data in contents.items():
        path = destination / name
        path.write_bytes(data)
        path.chmod(0o644)
    report = Path(output) / 'share/unicorn-fingerprint'
    report.mkdir(parents=True)
    (report / 'ta-manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')


if __name__ == '__main__':
    assemble(*sys.argv[1:])
