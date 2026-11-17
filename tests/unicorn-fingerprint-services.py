"""Root requester -> real broker -> actual user helper, with production sandboxes.

This deliberately stops before Wayland/illumination: it tests both SO_PEERCRED
checks and SCM_RIGHTS forwarding without pretending to emulate phone hardware.
"""
import socket
import struct
import time

for attempt in range(20):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
        client.connect('/run/unicorn-fingerprint-display.sock')
        client.send(struct.pack('=IIII', 0x55464831, 1, 2, 1000))
        client.settimeout(0.2)
        try:
            result = client.recv(1)
        except TimeoutError:
            # The helper has authenticated root and is waiting for the request.
            client.send(b'invalid request')
            client.settimeout(2)
            assert client.recv(1) == b''
            break
        assert result == b''
    time.sleep(0.1)
else:
    raise AssertionError('user hint did not authenticate/register and accept the root fd')
print('production broker/user service: root credentials and fd handoff passed')
