#!/usr/bin/env python3
"""Run the production QSEECOM memref checks, layout and invoke with fake SCM."""
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
source = (args.source / 'drivers/tee/qseecom/core.c').read_text()
fixture = Path(__file__).with_suffix('.c').read_text()
constants = '\n'.join(re.findall(r'^#define (?:QSEECOM_TEE_MAX_(?:XFER|PATCH)|QSEECOM_ALLOC_PRIVATE|QSEECOM_PATCH_PRIVATE).*$', source, re.M))
fixture = fixture.replace('/* @LIMITS@ */', constants)
functions = ['qseecom_tee_memref', 'qseecom_tee_check_patch', 'qseecom_tee_staging_layout', 'qseecom_tee_invoke_func']
fixture = fixture.replace('/* @FUNCTIONS@ */', '\n'.join(extract.function(source, name) for name in functions))
with tempfile.TemporaryDirectory(prefix='unicorn-qsee-staging-') as directory:
    path = Path(directory) / 'test.c'
    path.write_text(fixture)
    exe = Path(directory) / 'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
