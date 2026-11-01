#!/usr/bin/env python3
"""Build the Linux reference registry in an isolated compiler project."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def build(output):
    if platform.system() != 'Linux' or platform.machine().lower() not in ['x86_64', 'amd64']:
        raise SystemExit('The reference registry build currently requires Linux x86_64')
    compiler = os.environ.get('BUOY', shutil.which('buoy') or 'buoy')
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='bpm-registry-build-') as temp:
        stage = Path(temp)
        source = stage / 'main.bui'
        shutil.copy2(HERE / 'main.bui', source)
        packages = stage / 'packages'
        packages.mkdir()
        toolchain_packages = Path(os.environ.get('BUOY_PACKAGES', '/usr/share/buoy/packages'))
        for pattern in ['HTTPServer-*.buipkg', 'crypto-*.buipkg']:
            matches = sorted(toolchain_packages.glob(pattern))
            if len(matches) != 1:
                raise SystemExit(f'Expected exactly one {pattern} under {toolchain_packages}')
            shutil.copy2(matches[0], packages / matches[0].name)
        native = stage / 'libbpm_registry_storage.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fPIC', '-shared', str(HERE / 'storage.c'), '-o', str(native)], check=True)
        for target in ['x86_64-linux', 'arm64-linux', 'x86_64-darwin', 'arm64-darwin',
                       'x86_64-windows', 'arm64-windows']:
            checked = subprocess.run([compiler, str(source), '--check', '--diagnostic-format', 'json',
                                      '--target', target], cwd=stage, text=True, capture_output=True, check=True)
            diagnostics = json.loads(checked.stdout)
            if diagnostics.get('errors') != [] or diagnostics.get('warnings') != []:
                raise RuntimeError(f'{target}: {diagnostics}')
        stubs = stage / 'host-stubs'
        shutil.copytree('/usr/lib/buoy/stubs/x86_64-unknown-linux-gnu', stubs)
        for library in ['libc.so.6', 'libstdc++.so.6']:
            (stubs / library).unlink()
            shutil.copy2(Path('/usr/lib/x86_64-linux-gnu') / library, stubs / library)
        subprocess.run([compiler, str(source), '--stubs-dir', str(stubs), '-L', str(stage),
                        '--extra-obj=/usr/lib/buoy/libbuoy_tls-x86_64-linux.a',
                        '--extra-obj=/usr/lib/buoy/libssl-x86_64-linux.a',
                        '--extra-obj=/usr/lib/buoy/libcrypto-x86_64-linux.a',
                        '--extra-obj=/usr/lib/x86_64-linux-gnu/libc_nonshared.a',
                        '-o', str(output)],
                       cwd=stage, check=True)
        shutil.copy2(native, output.parent / native.name)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', default='dist/bpm-registry')
    print(build(parser.parse_args().output))
