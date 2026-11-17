#!/usr/bin/env python3
"""Exercise the actual patched kernel listener loop, without secure data."""
import argparse
from importlib.machinery import SourceFileLoader
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
extract = SourceFileLoader('display_extract', str(Path(__file__).with_name('unicorn-display-lifecycle.py'))).load_module()
source = (args.source / 'drivers/firmware/qcom/qcom_scm.c').read_text()
fixture = Path(__file__).with_suffix('.c').read_text()
limits = '\n'.join(re.findall(r'^#define QSEECOM_(?:MAX_LISTENER_\w+|LISTENER_BUDGET).*$', source, re.M))
fixture = fixture.replace('/* @LIMITS@ */', limits)
fixture = fixture.replace('/* @FUNCTION@ */', extract.function(source, 'qcom_scm_qseecom_service_listeners'))
with tempfile.TemporaryDirectory(prefix='unicorn-qsee-listener-') as directory:
    path = Path(directory) / 'test.c'
    path.write_text(fixture)
    exe = Path(directory) / 'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
