#!/usr/bin/env python3
"""Test the real panel lease ownership/timer callbacks with deterministic kernel fakes."""
import argparse
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from importlib.machinery import SourceFileLoader

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
args=parser.parse_args()
extract=SourceFileLoader('display_extract',str(Path(__file__).with_name('unicorn-display-lifecycle.py'))).load_module()
s=(args.source/'drivers/gpu/drm/panel/panel-xiaomi-l2s-380c0a.c').read_text()
fixture=Path(__file__).with_suffix('.c').read_text()
fixture=fixture.replace('/* @FOD_STRUCT@ */',extract.block(s,s.index('struct l2s_fod {'))+';')
functions=['l2s_fod_timeout','l2s_fod_free','l2s_fod_open','l2s_fod_close','l2s_fod_destroy','l2s_fod_init','l2s_fod_ioctl']
fixture=fixture.replace('/* @FOD_FUNCTIONS@ */','\n'.join(extract.function(s,n) for n in functions))
with tempfile.TemporaryDirectory(prefix='unicorn-fod-lease-') as d:
 p=Path(d)/'test.c';p.write_text(fixture);exe=Path(d)/'test'
 subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
