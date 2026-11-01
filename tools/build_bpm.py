#!/usr/bin/env python3
"""Build bpm and its private native host in an isolated compiler project."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import xml.etree.ElementTree as ET
import tempfile
import zipfile

import build_bpm_host as host

ROOT = Path(__file__).resolve().parents[1]


def macos_runtime(binary, directory):
    """Bundle non-system Mach-O dependencies and discard build-machine rpaths."""
    def rpaths(filename):
        details = subprocess.check_output(['otool', '-l', str(filename)], text=True)
        return re.findall(r'^\s*path (.+) \(offset \d+\)$', details, re.MULTILINE)

    def expand(value, loader):
        return Path(value.replace('@executable_path', str(binary.parent)).replace('@loader_path', str(loader.parent)))

    executable_paths = rpaths(binary)
    pending = [(binary, binary)]
    known = {binary.name: hashlib.sha256(binary.read_bytes()).hexdigest()}
    completed = set()
    while pending:
        original, copied = pending.pop()
        if copied.name in completed:
            continue
        completed.add(copied.name)
        search = rpaths(original) + executable_paths
        listing = subprocess.check_output(['otool', '-L', str(original)], text=True)
        for line in listing.splitlines()[1:]:
            dependency = line.strip().split(' (compatibility version', 1)[0]
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith('@rpath/'):
                relative = dependency[len('@rpath/'):]
                candidates = [expand(root, original) / relative for root in search]
            else:
                candidates = [expand(dependency, original)]
            source = next((candidate for candidate in candidates if candidate.is_file()), None)
            if source is None:
                raise RuntimeError(f'Cannot bundle Mach-O dependency {dependency} of {original.name}')
            source = source.resolve()
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            destination = directory / source.name
            if destination.name in known and known[destination.name] != digest:
                raise RuntimeError(f'Conflicting Mach-O dependency basenames: {source.name}')
            if destination.name not in known:
                known[destination.name] = digest
                shutil.copy2(source, destination)
                pending.append((source, destination))
            replacement = '@rpath/' + source.name
            if dependency != replacement:
                subprocess.run(['install_name_tool', '-change', dependency, replacement, str(copied)], check=True)
        for old in dict.fromkeys(rpaths(copied)):
            subprocess.run(['install_name_tool', '-delete_rpath', old, str(copied)], check=True)
        subprocess.run(['install_name_tool', '-add_rpath', '@executable_path' if copied == binary else '@loader_path', str(copied)], check=True)
    # Changing load commands invalidates the compiler/linker's ad-hoc signatures.
    for filename in sorted(directory.iterdir()):
        subprocess.run(['codesign', '--force', '--sign', '-', str(filename)], check=True)


def windows_manifest(binary):
    """Add UTF-8 activation while preserving the compiler's UAC/DPI manifest.

    Buoy already emits a resource object; lld cannot link another one. Edit only
    the existing manifest allocation, failing closed if it has insufficient room.
    https://learn.microsoft.com/windows/win32/sbscs/application-manifests
    """
    data = bytearray(binary.read_bytes())
    if data[:2] != b'MZ':
        raise RuntimeError('Expected PE executable')
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise RuntimeError('Invalid PE signature')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20B:
        raise RuntimeError('Expected PE32+ executable')
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    resource_rva, resource_size = struct.unpack_from('<II', data, optional + 112 + 16)
    certificate, certificate_size = struct.unpack_from('<II', data, optional + 112 + 32)
    if certificate or certificate_size:
        raise RuntimeError('Refusing to modify an Authenticode-signed executable')
    sections = []
    for index in range(count):
        at = optional + optional_size + index * 40
        virtual_size, address, raw_size, offset = struct.unpack_from('<IIII', data, at + 8)
        sections.append((address, raw_size, offset))

    def file_offset(rva, length):
        for address, size, offset in sections:
            if address <= rva and rva + length <= address + size:
                position = offset + rva - address
                if position + length <= len(data):
                    return position
        raise RuntimeError('Invalid resource address')

    root = file_offset(resource_rva, resource_size)

    def entries(relative):
        if relative < 0 or relative + 16 > resource_size:
            raise RuntimeError('Invalid resource directory')
        named, numeric = struct.unpack_from('<HH', data, root + relative + 12)
        if relative + 16 + 8 * (named + numeric) > resource_size:
            raise RuntimeError('Resource entries exceed directory')
        return [struct.unpack_from('<II', data, root + relative + 16 + index * 8)
                for index in range(named + numeric)]

    updated = 0
    for kind, names in entries(0):
        if kind != 24:
            continue
        if not names & 0x80000000:
            raise RuntimeError('Expected manifest name directory')
        for identifier, languages in entries(names & 0x7FFFFFFF):
            if identifier != 1:
                continue
            if not languages & 0x80000000:
                raise RuntimeError('Expected manifest language directory')
            for language, leaf in entries(languages & 0x7FFFFFFF):
                if leaf & 0x80000000 or leaf + 16 > resource_size:
                    raise RuntimeError('Invalid manifest data entry')
                rva, capacity = struct.unpack_from('<II', data, root + leaf)
                position = file_offset(rva, capacity)
                text = bytes(data[position:position + capacity]).rstrip(b'\0').decode('utf-8-sig')
                document = ET.fromstring(text)
                active = document.find('.//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage')
                if active is not None:
                    if active.text != 'UTF-8':
                        raise RuntimeError('Unexpected existing active code page')
                    updated += 1
                    continue
                if document.find('.//{urn:schemas-microsoft-com:asm.v3}windowsSettings') is None:
                    raise RuntimeError('Missing Windows settings in compiler manifest')
                element = '<activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>'
                text, replaced = re.subn(r'</(?:[A-Za-z_][\w.-]*:)?windowsSettings>', lambda match: element + match.group(), text, count=1)
                if replaced != 1:
                    raise RuntimeError('Cannot locate Windows settings closing tag')
                # XML defaults to UTF-8. Removing its optional declaration and
                # formatting preserves policy while fitting the existing slot.
                text = re.sub(r'^\s*<\?xml[^?]*\?>\s*', '', text)
                text = re.sub(r'>\s+<', '><', text).strip()
                parsed = ET.fromstring(text)
                if parsed.find('.//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage').text != 'UTF-8':
                    raise RuntimeError('Invalid updated UTF-8 manifest')
                payload = text.encode('utf-8')
                if len(payload) > capacity:
                    raise RuntimeError('Compiler manifest has insufficient space for UTF-8 activation')
                data[position:position + capacity] = payload + b'\0' * (capacity - len(payload))
                struct.pack_into('<I', data, root + leaf + 4, len(payload))
                updated += 1
    if not updated:
        raise RuntimeError('Missing executable activation manifest')
    # Normal application PE checksums may be zero; signatures are applied later.
    struct.pack_into('<I', data, optional + 64, 0)
    binary.write_bytes(data)


def build(target=None, cc=None, output=None, skip_native=False):
    target = target or host.host_target()
    inputs = ['tools/bpm/main.bui', 'tools/build_bpm.py', 'tools/build_bpm_host.py',
              'tools/bpm/native/bridge.c', 'libraries/processkit/native/bridge.c']
    source_inputs = {name: (ROOT / name).read_bytes() for name in inputs}
    source_hashes = {name: hashlib.sha256(data).hexdigest()
                     for name, data in source_inputs.items()}
    native_source = host.source_digest()
    if not skip_native:
        host.build_native(target, cc)
    destination = Path(output).resolve() if output else ROOT / 'dist' / ('bpm.exe' if target.endswith('windows') else 'bpm')
    destination.parent.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('BPM_BUOY', os.environ.get('BUOY', 'buoy'))
    compiler_path = shutil.which(compiler)
    if not compiler_path:
        raise RuntimeError('Compiler executable was not found')
    compiler_hash = hashlib.sha256(Path(compiler_path).read_bytes()).hexdigest()
    compiler_version = subprocess.check_output([compiler, '--version'], text=True).strip()
    if hashlib.sha256(Path(compiler_path).read_bytes()).hexdigest() != compiler_hash:
        raise RuntimeError('Compiler executable changed during identification; refusing publication')
    with tempfile.TemporaryDirectory(prefix='bpm-build-') as temporary:
        stage = Path(temporary)
        (stage / 'main.bui').write_bytes(source_inputs['tools/bpm/main.bui'])
        (stage / 'packages').mkdir()
        archive = host.seal(stage / 'packages/bpmhost-0.1.0.buipkg')
        native = host.runtime_path(target)
        native_details = json.loads(native.with_name(native.name + '.build.json').read_text())
        if native_details.get('sourceSHA256') != native_source:
            raise RuntimeError('Selected native host receipt is stale')
        package_details = json.loads(archive.with_name(archive.name + '.build.json').read_text())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != package_details['sha256']:
            raise RuntimeError('Staged package differs from its build receipt')
        with zipfile.ZipFile(archive) as package:
            member = 'runtime/' + host.TARGETS[target] + '/' + native.name
            if hashlib.sha256(package.read(member)).hexdigest() != native_details['files'][native.name]:
                raise RuntimeError('Staged native package differs from selected host receipt')
        environment = os.environ.copy()
        environment.update(HOME=str(stage / 'home'), USERPROFILE=str(stage / 'home'),
                           XDG_DATA_HOME=str(stage / 'data'), XDG_CACHE_HOME=str(stage / 'cache'),
                           LOCALAPPDATA=str(stage / 'local'), APPDATA=str(stage / 'roaming'))
        for checked_target in host.TARGETS:
            result = subprocess.run([compiler, 'main.bui', '--check', '--diagnostic-format', 'json', '--target', checked_target], cwd=stage, env=environment, text=True, capture_output=True, check=True)
            diagnostics = json.loads(result.stdout)
            if diagnostics['errors'] or diagnostics['warnings']:
                raise RuntimeError(f'{checked_target}: {result.stdout}')
        emitted = stage / 'output'
        emitted.mkdir()
        staged_binary = emitted / destination.name
        arguments = [compiler, 'main.bui', '--target', target, '-o', str(staged_binary)]
        subprocess.run(arguments, cwd=stage, env=environment, check=True)
        if target.endswith('windows'):
            windows_manifest(staged_binary)
        if target.endswith('darwin'):
            macos_runtime(staged_binary, emitted)
        if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest
               for name, digest in source_hashes.items()):
            raise RuntimeError('Build inputs changed during compilation; refusing publication')
        if shutil.which(compiler) != compiler_path or hashlib.sha256(Path(compiler_path).read_bytes()).hexdigest() != compiler_hash:
            raise RuntimeError('Compiler executable changed during compilation; refusing publication')
        # A killed multi-file copy must leave no old receipt claiming new bytes.
        destination.with_name(destination.name + '.runtime.json').unlink(missing_ok=True)
        files = {}
        for source in sorted(emitted.iterdir()):
            if not source.is_file():
                raise RuntimeError(f'Unexpected compiler output directory: {source.name}')
            files[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
            descriptor, temporary_output = tempfile.mkstemp(prefix='.' + source.name + '-', dir=destination.parent)
            os.close(descriptor)
            try:
                shutil.copy2(source, temporary_output)
                os.replace(temporary_output, destination.parent / source.name)
            finally:
                Path(temporary_output).unlink(missing_ok=True)
        # This records the exact files to keep together when copying an install.
        receipt = {'schema': 1, 'target': target, 'executable': destination.name, 'files': files}
        receipt['build'] = {
            'sourceSHA256': source_hashes,
            'nativeSourceSHA256': native_source,
            'nativeBuild': native_details,
            'packager': package_details['packager'],
            'hostPackageSHA256': hashlib.sha256((stage / 'packages' / archive.name).read_bytes()).hexdigest(),
            'compilerVersion': compiler_version,
            'compilerExecutableSHA256': compiler_hash,
        }
        descriptor, temporary_receipt = tempfile.mkstemp(prefix='.bpm-runtime-', dir=destination.parent)
        os.close(descriptor)
        try:
            Path(temporary_receipt).write_text(json.dumps(receipt, indent=2) + '\n')
            os.replace(temporary_receipt, destination.with_name(destination.name + '.runtime.json'))
        finally:
            Path(temporary_receipt).unlink(missing_ok=True)
    print(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=host.TARGETS)
    parser.add_argument('--cc', help='Native host C compiler, including cross compiler')
    parser.add_argument('--output')
    parser.add_argument('--skip-native', action='store_true', help='Use host slices rebuilt from the current sources')
    arguments = parser.parse_args()
    build(arguments.target, arguments.cc, arguments.output, arguments.skip_native)


if __name__ == '__main__':
    main()
