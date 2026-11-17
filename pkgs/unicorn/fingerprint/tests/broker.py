#!/usr/bin/env python3
"""Exercise the real descriptor broker; the C peer shim supplies a root test namespace."""
import array
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile

binary=str(Path(sys.argv[1]).resolve())
magic=0x55464831
with tempfile.TemporaryDirectory(prefix='unicorn-hint-broker-') as tmp:
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET)
    listener.bind(tmp+'/broker.sock');listener.listen(16)
    if listener.fileno()!=3:
        os.dup2(listener.fileno(),3,inheritable=True)
    def activate():
        os.environ['LISTEN_PID']=str(os.getpid())
        os.environ['LISTEN_FDS']='1'
    process=subprocess.Popen([binary],pass_fds=(3,),preexec_fn=activate)
    def connect(role,uid=0,signature=magic):
        s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        s.settimeout(3);s.connect(tmp+'/broker.sock')
        s.send(struct.pack('IIII',signature,1,role,uid))
        return s
    try:
        helper=connect(1)
        duplicate=connect(1);assert duplicate.recv(1)==b'';duplicate.close()
        unknown=connect(2,999);assert unknown.recv(1)==b'';unknown.close()
        wrong=connect(2,signature=0);assert wrong.recv(1)==b'';wrong.close()
        forged=connect(1,1000);assert forged.recv(1)==b'';forged.close()
        root=connect(2);root.send(b'nonce:one')
        data,ancillary,flags,_=helper.recvmsg(1,socket.CMSG_SPACE(16),socket.MSG_CMSG_CLOEXEC)
        assert data==b'F' and not flags&socket.MSG_CTRUNC
        descriptors=array.array('i')
        for level,kind,value in ancillary:
            if level==socket.SOL_SOCKET and kind==socket.SCM_RIGHTS: descriptors.frombytes(value)
        assert len(descriptors)==1
        passed=socket.socket(fileno=descriptors[0]);passed.settimeout(3)
        # SO_PEERCRED still names the original requester after descriptor passing.
        pid,uid,_=struct.unpack('3i',passed.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
        assert pid==os.getpid() and uid==os.getuid()
        assert passed.recv(64)==b'nonce:one'
        passed.send(b'ready:one');assert root.recv(64)==b'ready:one'
        passed.close();assert root.recv(1)==b'';root.close();helper.close()
        print('hint broker: peer/role checks, duplicate registration, exact descriptor handoff, credentials, nonce exchange and disconnect passed')
    finally:
        process.terminate()
        try: process.wait(timeout=3)
        except subprocess.TimeoutExpired: process.kill();process.wait()
        listener.close()
