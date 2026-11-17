#!/usr/bin/env python3
"""Audit the local OEM analysis inputs without copying or executing them."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def digest(path):
    data = path.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


def main():
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=repository / '.work/unicorn/fingerprint')
    args = parser.parse_args()
    package = repository / 'pkgs/unicorn/fingerprint'
    evidence = json.loads((package / 'evidence.json').read_text())
    for item in evidence['inputs']:
        if digest(args.source / item['path']) != (item['bytes'], item['sha256']):
            raise ValueError(f"analysis input differs: {item['path']}")
    firmware = json.loads((package / 'inputs/manifest.json').read_text())
    for item in firmware['files']:
        if digest(args.source / 'ta' / item['path']) != (item['bytes'], item['sha256']):
            raise ValueError(f"TA input differs: {item['path']}")
    # Pinned libgf_hal6's first PT_LOAD has VA == file offset here. Explicitly
    # verify that ELF mapping rather than assuming it for another vendor build.
    hal = (args.source / 'vendor/lib64/libgf_hal6.so').read_bytes()
    if hal[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', hal, 18)[0] != 183:
        raise ValueError('expected AArch64 little-endian ELF64 HAL')
    phoff = struct.unpack_from('<Q', hal, 32)[0]
    phsize, count = struct.unpack_from('<HH', hal, 54)
    segments = [struct.unpack_from('<IIQQQQQQ', hal, phoff + i * phsize) for i in range(count)]
    constants = {
        'keymaster_auth_key': (0x1f3d0, (517, 2)),
        'install_auth_key': (0x1f3f8, (1000, 10)),
        'detector_init': (0x1f388, (1002, 0)),
        'detector_info': (0x1f488, (1002, 1)),
        'base_sensor_init': (0x1f390, (1002, 2)),
        'calibration_load_error': (0x1f2c8, (1002, 103)),
        'wake': (0x1f3e8, (1002, 9)),
        'sleep': (0x1f468, (1002, 8)),
        'pre_enroll': (0x1f320, (1000, 2)),
        'enroll': (0x1f598, (1000, 3)),
        'post_enroll': (0x1f2a0, (1000, 4)),
        'save': (0x1f4b8, (1000, 9)),
        'remove': (0x1f4c0, (1000, 6)),
        'enumerate': (0x1f518, (1000, 7)),
        'cancel': (0x1f4c8, (1000, 8)),
        'capture': (0x1f580, (1002, 3)),
        'base_enroll_image': (0x1f198, (1001, 1)),
        'algo_init': (0x1f5a0, (1001, 0)),
        'sensor_ids': (0x1f3a0, (1001, 22)),
        'enroll_finish': (0x1f3c8, (1001, 24)),
        'read_image': (0x1f4b0, (1002, 101)),
        'irq': (0x1f5b8, (1002, 5)),
        'post_auth': (0x1f340, (1000, 12)),
    }
    report = {}
    for name, (address, expected) in constants.items():
        matches = [s for s in segments if s[0] == 1 and s[3] <= address and address + 8 <= s[3] + s[5]]
        if len(matches) != 1:
            raise ValueError(f'ambiguous constant mapping: {name}')
        s = matches[0]
        words = struct.unpack_from('<II', hal, s[2] + address - s[3])
        if words != expected:
            raise ValueError(f'command constant differs: {name}')
        report[name] = {'virtual_address': hex(address), 'words': words}
    # Both pinned HALs export the same 308-entry table. Verify names and
    # numeric values, not an enum copied from the gfenu reference driver.
    tables = []
    for filename, address in [('libgf_hal.so', 0xee0b8), ('libgf_hal6.so', 0xa3c38)]:
        binary = (args.source / 'vendor/lib64' / filename).read_bytes()
        phoff = struct.unpack_from('<Q', binary, 32)[0]
        phsize, phcount = struct.unpack_from('<HH', binary, 54)
        headers = [struct.unpack_from('<IIQQQQQQ', binary, phoff+i*phsize) for i in range(phcount)]
        def file_offset(va):
            matching = [h for h in headers if h[0] == 1 and h[3] <= va < h[3]+h[5]]
            if len(matching) != 1:
                raise ValueError(f'invalid error-table pointer in {filename}')
            h = matching[0]
            return h[2]+va-h[3]
        entries = []
        for i in range(308):
            offset = file_offset(address+i*16)
            code = struct.unpack_from('<I', binary, offset)[0]
            pointer = struct.unpack_from('<Q', binary, offset+8)[0]
            start = file_offset(pointer)
            name = binary[start:start+128].split(b'\0',1)[0].decode('ascii')
            entries.append((code,name))
        tables.append(entries)
    if tables[0] != tables[1] or len(set(code for code, _ in tables[0])) != 308:
        raise ValueError('OEM error tables differ')
    keymaster = (args.source / 'authorization-20261117/libkeymasterdeviceutils.so').read_bytes()
    if struct.unpack_from('<IIII', keymaster, 0x2e00) != (0x207, 4, 5, 4):
        raise ValueError('Gatekeeper client negotiation constants differ')
    # Exact OEM sendCmd argument flow: x2 = CBOR length + command word,
    # x8 = aligned response offset, x3 = response pointer. Do not turn the
    # alignment gap into input bytes. The pinned LOAD maps VA == file offset.
    for address, instruction in [(0x5854, 0xb1001102), (0x5868, 0x121e7508),
                                  (0x5888, 0xaa1403e3)]:
        if struct.unpack_from('<I', keymaster, address)[0] != instruction:
            raise ValueError('Gatekeeper request length/response alignment differs')
    qcbor = (args.source / 'authorization-20261117/libqcbor.so').read_bytes()
    if struct.unpack_from('<II', qcbor, 0x4c28) != (0x1a9f07e8, 0x531c6d00):
        raise ValueError('stock QCBOR trailing-data result differs')
    # Signed resident TA image was read from code partitions, never from secure
    # credential storage. Map its dispatch table through ELF PT_LOAD metadata.
    ta = (args.source / evidence['gatekeeper']['signed_ta']['input']).read_bytes()
    if ta[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', ta, 18)[0] != 183:
        raise ValueError('expected signed AArch64 Keymaster image')
    phoff = struct.unpack_from('<Q', ta, 32)[0]
    phsize, count = struct.unpack_from('<HH', ta, 54)
    segments = [struct.unpack_from('<IIQQQQQQ', ta, phoff + i * phsize) for i in range(count)]
    address = 0x505b0
    matches = [s for s in segments if s[0] == 1 and s[3] <= address and address + 43 * 16 <= s[3] + s[5]]
    if len(matches) != 1:
        raise ValueError('ambiguous signed TA dispatch mapping')
    segment = matches[0]
    offset = segment[2] + address - segment[3]
    dispatch = dict((cmd, (flags, fn)) for cmd, flags, fn in
                    (struct.unpack_from('<IIQ', ta, offset + i * 16) for i in range(43)))
    for cmd, function in [(0x200, 0xc500), (0x207, 0xc5ec), (0x21001, 0x46a0),
                          (0x21002, 0x5100), (0x21003, 0x5470), (0x21004, 0x551c)]:
        if dispatch.get(cmd) != (0x100, function):
            raise ValueError(f'signed TA command dispatch differs: {cmd:#x}')
    print(json.dumps({'analysis_inputs_checked' : len(evidence['inputs']),
                      'TA_files_checked': len(firmware['files']),
                      'static_constants': report, 'identical_error_table_entries': len(tables[0]),
                      'selected_ta': None, 'hardware_commands_sent': False}, indent=2))


if __name__ == '__main__':
    main()
