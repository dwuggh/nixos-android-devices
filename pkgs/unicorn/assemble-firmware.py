#!/usr/bin/env python3
"""Verify the selected OEM inputs and assemble canonical Linux firmware names."""

import argparse
import hashlib
import json
import struct
import tarfile
from pathlib import Path, PurePosixPath


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_mdt(name, files):
    """Check split ELF32 metadata, every nonempty segment, and packed hash data.

    This checks loader input completeness, not secure-world authentication.
    """
    data = files[name]
    require(data[:7] == b"\x7fELF\x01\x01\x01", f"{name}: expected little-endian ELF32")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    phoff, ehsize, phentsize, phnum = header[5], header[8], header[9], header[10]
    header_size = phoff + phentsize * phnum
    require(ehsize == 52 and phentsize == 32 and phnum > 0, f"{name}: invalid ELF header")
    require(header_size <= len(data), f"{name}: truncated program headers")
    require(files[name.removesuffix('.mdt') + '.b00'] == data[:header_size],
            f"{name}: ELF header segment mismatch")
    hashes = []
    loadable = []
    expected_segments = set()
    for index in range(phnum):
        kind, _, _, paddr, size, memsize, flags, _ = struct.unpack_from(
            "<IIIIIIII", data, phoff + phentsize * index
        )
        is_hash = flags & (7 << 24) == 2 << 24
        if size:
            segment = name.removesuffix('.mdt') + f'.b{index:02d}'
            require(segment in files, f"{name}: missing segment {index}")
            require(len(files[segment]) == size, f"{segment}: ELF segment length mismatch")
            expected_segments.add(segment)
            if is_hash:
                hashes.append(files[segment])
        if kind == 1 and not is_hash:
            require(size <= memsize, f"{name}: segment {index} exceeds memory size")
            if memsize:
                loadable.append((paddr, paddr + memsize))
    require(len(hashes) == 1, f"{name}: expected one hash segment")
    require(data[header_size:] == hashes[0], f"{name}: packed authentication metadata mismatch")
    prefix = name.removesuffix('.mdt') + '.b'
    require({path for path in files if path.startswith(prefix)} == expected_segments,
            f"{name}: unexpected split segment")
    require(loadable, f"{name}: no loadable segments")
    return {"metadata": name, "segments": len(expected_segments),
            "loadable_segments": len(loadable),
            "memory_span_bytes": max(end for _, end in loadable) - min(start for start, _ in loadable)}


def element(kind, data):
    return struct.pack("<II", kind, len(data)) + data + b"\0" * (-len(data) % 4)


def assemble(archive_path, manifest_path, output):
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    require(manifest['schema'] == 1, 'unsupported manifest schema')
    expected = {entry['path']: entry for entry in manifest['files']}
    require(len(expected) == len(manifest['files']), 'duplicate manifest path')
    with tarfile.open(archive_path, 'r:xz') as archive:
        members = archive.getmembers()
        require(len({member.name for member in members}) == len(members), 'duplicate archive path')
        require({member.name for member in members} == set(expected) | {'manifest.json'},
                'archive file set differs from manifest')
        require(all(member.isfile() for member in members), 'archive contains non-regular entries')
        require(archive.extractfile('manifest.json').read() == manifest_bytes,
                'archive manifest differs from pinned manifest')
        files = {}
        for name, entry in expected.items():
            path = PurePosixPath(name)
            require(not path.is_absolute() and '..' not in path.parts, 'unsafe archive path')
            data = archive.extractfile(name).read()
            require(len(data) == entry['bytes'], f'{name}: length mismatch')
            require(hashlib.sha256(data).hexdigest() == entry['sha256'], f'{name}: SHA256 mismatch')
            files[name] = data
    mdts = [validate_mdt(name, files) for name in sorted(files) if name.endswith('.mdt')]
    require({Path(entry['metadata']).stem for entry in mdts} ==
            {'a730_zap', 'adsp', 'slpi', 'cdsp'},
            'expected GPU, ADSP, SLPI and CDSP metadata')
    for name, data in files.items():
        if name.startswith('firmware/'):
            target = output / 'lib' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    wlan = output / 'lib/firmware/ath11k/WCN6855/hw2.1'
    wlan.mkdir(parents=True, exist_ok=True)
    (wlan / 'amss.bin').write_bytes(files['wlan/amss20.bin'])
    (wlan / 'm3.bin').write_bytes(files['wlan/m3.bin'])
    (wlan / 'regdb.bin').write_bytes(files['wlan/regdb_xiaomi.bin'])
    magic = b'QCA-ATH11K-BOARD\0'
    key = manifest['wlan']['board_key'].encode('ascii')
    board = magic + b'\0' * (-len(magic) % 4)
    # ath11k API2 BOARD=0/REGDB=1, nested NAME=0/DATA=1 (ath11k/hw.h).
    for kind, payload in ((0, files['wlan/bd_l2sgf.elf']), (1, files['wlan/regdb_xiaomi.bin'])):
        board += element(kind, element(0, key) + element(1, payload))
    (wlan / 'board-2.bin').write_bytes(board)
    share = output / 'share/unicorn-firmware'
    share.mkdir(parents=True, exist_ok=True)
    (share / 'manifest.json').write_bytes(manifest_bytes)
    report = {'verified_inputs': len(files), 'mdt': mdts,
              'board_key': key.decode('ascii'),
              'board_2_sha256': hashlib.sha256(board).hexdigest(),
              'secure_world_authentication_verified': False}
    (share / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    arguments = parser.parse_args()
    assemble(arguments.archive, arguments.manifest, arguments.output)
