#!/usr/bin/env python3
"""Build bpm's private portable host bridge and seal its native-only package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import struct
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools/bpm/native/bridge.c'
PROCESS = ROOT / 'libraries/processkit/native/bridge.c'
OUTPUT = ROOT / 'tools/bpm/native/build'
TARGETS = {'x86_64-linux':'x86_64-unknown-linux-gnu','arm64-linux':'aarch64-unknown-linux-gnu',
           'x86_64-darwin':'x86_64-apple-macos','arm64-darwin':'arm64-apple-macos',
           'x86_64-windows':'x86_64-pc-windows-msvc','arm64-windows':'aarch64-pc-windows-msvc'}

def host_target():
    arch={'aarch64':'arm64','amd64':'x86_64'}.get(platform.machine().lower(),platform.machine().lower())
    system={'darwin':'darwin','linux':'linux','win32':'windows'}.get(sys.platform)
    target=f'{arch}-{system}'
    if target not in TARGETS: raise RuntimeError(f'Unsupported host {target}')
    return target

def source_digest():
    return hashlib.sha256(SOURCE.read_bytes()+PROCESS.read_bytes()+Path(__file__).read_bytes()).hexdigest()

def private_process_source(body=None):
    # Exact replacement guards deliberately fail closed on ProcessKit changes.
    # NULL env values mean deletion; empty non-NULL values remain empty overrides.
    if body is None:body=PROCESS.read_text()
    edits=[
      ('int started, exited, eof[2], owned_tree, signal_number, tree_terminated;', 'int started, exited, eof[2], owned_tree, signal_number, tree_terminated; int64_t file_limit;'),
      ('/* execve, unlike execvp, never falls back', 'if (p->file_limit) { struct rlimit limit = {(rlim_t)p->file_limit, (rlim_t)p->file_limit}; if (setrlimit(RLIMIT_FSIZE, &limit)) { child_start_failure(3, errno); } }\n        /* execve, unlike execvp, never falls back'),
      ('*value = wide_text(p->env_values[i]);','*value = wide_text(p->env_values[i] ? p->env_values[i] : "");'),
      ('entries[count++] = entry;\n    }\n    qsort', 'if (p->env_values[i]) entries[count++] = entry; else { clear_text(entry, wcslen(entry) * sizeof(wchar_t)); free(entry); }\n    }\n    qsort'),
      ('value_size = strlen(p->env_values[i]);','value_size = p->env_values[i] ? strlen(p->env_values[i]) : 0;'),
      ('memcpy(entry + name_size + 1, p->env_values[i], value_size + 1);','memcpy(entry + name_size + 1, p->env_values[i] ? p->env_values[i] : "", value_size + 1);'),
      ('entries[count++] = entry; entries[count] = NULL;', 'if (p->env_values[i]) entries[count++] = entry; else free_environment_text(entry); entries[count] = NULL;')]
    for before,after in edits:
        if body.count(before)!=1: raise RuntimeError(f'ProcessKit source changed: expected one environment hook {before!r}')
        body=body.replace(before,after)
    return re.sub(r'\bbpk_', 'bph_proc_', body)

def runtime_path(target):
    filename='buoy_bpm_host.dll' if target.endswith('windows') else 'libbuoy_bpm_host.'+('dylib' if target.endswith('darwin') else 'so')
    return OUTPUT/'runtime'/TARGETS[target]/filename

def verify_native_target(filename,target):
    data=Path(filename).read_bytes()
    actual=None
    if data.startswith(b'\x7fELF') and len(data)>=20 and data[4]==2 and data[5] in (1,2):
        machine=struct.unpack_from('<H' if data[5]==1 else '>H',data,18)[0]
        arch={62:'x86_64',183:'arm64'}.get(machine)
        if arch:actual=arch+'-linux'
    elif data.startswith(b'MZ') and len(data)>=64:
        offset=struct.unpack_from('<I',data,0x3c)[0]
        if offset<=len(data)-24 and data[offset:offset+4]==b'PE\0\0':
            machine=struct.unpack_from('<H',data,offset+4)[0]
            arch={0x8664:'x86_64',0xaa64:'arm64'}.get(machine)
            if arch:actual=arch+'-windows'
    elif len(data)>=8 and data[:4] in (b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf'):
        machine=struct.unpack_from('<I' if data[:4]==b'\xcf\xfa\xed\xfe' else '>I',data,4)[0]
        arch={0x1000007:'x86_64',0x100000c:'arm64'}.get(machine)
        if arch:actual=arch+'-darwin'
    if actual!=target:raise RuntimeError(f'Native binary target mismatch: requested {target}, detected {actual or "unknown/unsupported format"}: {filename}')
    return actual

def publish_file(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.bpm-native-',dir=destination.parent)
    os.close(fd);stage=Path(name)
    try:
        shutil.copy2(source,stage)
        os.replace(stage,destination)
    finally:
        stage.unlink(missing_ok=True)

def build_native(target=None, cc=None):
    target=target or host_target()
    output=runtime_path(target);output.parent.mkdir(parents=True,exist_ok=True)
    compiler=shlex.split(cc or os.environ.get('CC','cl' if sys.platform=='win32' else 'cc'))
    executable=shutil.which(compiler[0])
    compiler_hash=hashlib.sha256(Path(executable).read_bytes()).hexdigest() if executable else None
    inputs={path:path.read_bytes() for path in (SOURCE,PROCESS,Path(__file__))}
    digest=hashlib.sha256(b''.join(inputs.values())).hexdigest()
    with tempfile.TemporaryDirectory(prefix='bpm-native-') as temp:
        directory=Path(temp);snapshot=directory/'bridge.c';snapshot.write_bytes(inputs[SOURCE])
        (directory/'bpm_processkit.inc').write_text(private_process_source(inputs[PROCESS].decode()))
        compiled=directory/output.name
        if target.endswith('windows'):
            link=OUTPUT/'link'/TARGETS[target]/'buoy_bpm_host.lib';link.parent.mkdir(parents=True,exist_ok=True)
            if Path(compiler[0]).stem.lower() in ('cl','clang-cl'):
                command=compiler+['/nologo','/LD','/O2','/std:c11','/D_CRT_SECURE_NO_WARNINGS',f'/I{directory}',str(snapshot),f'/Fe:{compiled}','/link',f'/IMPLIB:{directory / link.name}','bcrypt.lib']
            else:
                command=compiler+['-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-static','-static-libgcc','-I',str(directory),str(snapshot),'-o',str(compiled),'-Wl,--out-implib,'+str(directory/link.name),'-lbcrypt']
        else:
            flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-pthread','-fPIC']
            if target.endswith('darwin'): flags+=['-dynamiclib','-arch',target.split('-')[0],'-mmacosx-version-min=11.0','-Wl,-install_name,@rpath/'+output.name]
            else: flags+=['-shared','-Wl,-soname,'+output.name]
            command=compiler+flags+['-I',str(directory),str(snapshot),'-o',str(compiled)]
        subprocess.run(command,cwd=directory,check=True)
        verify_native_target(compiled,target)
        if any(path.read_bytes()!=data for path,data in inputs.items()):
            raise RuntimeError('Native build inputs changed during compilation; refusing publication')
        files={output.name:hashlib.sha256(compiled.read_bytes()).hexdigest()}
        if target.endswith('windows'):files[link.name]=hashlib.sha256((directory/link.name).read_bytes()).hexdigest()
        details={'schema':1,'target':target,'sourceSHA256':digest,'files':files,
                 'compilerArguments':compiler,'compilerExecutableSHA256':compiler_hash}
        publish_file(compiled,output)
        if target.endswith('windows'):publish_file(directory/link.name,link)
        receipt=directory/'source-sha256';receipt.write_text(digest+'\n')
        publish_file(receipt,output.with_name(output.name+'.source-sha256'))
        receipt=directory/'build.json';receipt.write_text(json.dumps(details,indent=2)+'\n')
        publish_file(receipt,output.with_name(output.name+'.build.json'))
    print(f'Built {output}',flush=True)
    return output

def seal(output=None):
    compiler=os.environ.get('BPM_PACKAGER',os.environ.get('BUOY','buoy'))
    located=shutil.which(compiler)
    if located and Path(located).resolve()==Path('/usr/local/bin/buoy') and Path('/usr/bin/buoy').is_file():compiler='/usr/bin/buoy'
    output=Path(output) if output else ROOT/'packages/bpmhost-0.1.0.buipkg'
    output.parent.mkdir(parents=True,exist_ok=True)
    expected_source=source_digest()
    tool={'executable':compiler,'executableSHA256':hashlib.sha256(Path(located).read_bytes()).hexdigest() if located else None}
    # Account for the Linux package-CLI wrapper override above.
    actual=shutil.which(compiler)
    if actual:tool['executableSHA256']=hashlib.sha256(Path(actual).read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='bpm-host-seal-') as temp:
        stage=Path(temp)
        (stage/'manifest.json').write_text(json.dumps({'schema':1,'name':'bpmhost','version':'0.1.0','description':'Private portable filesystem/process host for bpm','libraries':[{'libName':'buoy_bpm_host','system':False,'minVersion':'0.1.0'}]}))
        count=0
        for target,triple in TARGETS.items():
            runtime=runtime_path(target);receipt=runtime.with_name(runtime.name+'.source-sha256')
            details_path=runtime.with_name(runtime.name+'.build.json')
            if not runtime.is_file() or not receipt.is_file() or receipt.read_text().strip()!=expected_source or not details_path.is_file():continue
            details=json.loads(details_path.read_text())
            if details.get('sourceSHA256')!=expected_source or details.get('target')!=target:continue
            verify_native_target(runtime,target)
            destination=stage/'runtime'/triple;destination.mkdir(parents=True)
            shutil.copy2(runtime,destination/runtime.name)
            if hashlib.sha256((destination/runtime.name).read_bytes()).hexdigest()!=details.get('files',{}).get(runtime.name):
                raise RuntimeError('Native runtime differs from its build receipt')
            if target.endswith('windows'):
                link=OUTPUT/'link'/triple/'buoy_bpm_host.lib'
                if not link.is_file():raise RuntimeError(f'Missing import library {link}')
                destination=stage/'link'/triple;destination.mkdir(parents=True)
                shutil.copy2(link,destination/link.name)
                if hashlib.sha256((destination/link.name).read_bytes()).hexdigest()!=details.get('files',{}).get(link.name):
                    raise RuntimeError('Native import library differs from its build receipt')
            count+=1
        if count == 0:raise RuntimeError('No host slices rebuilt from current sources')
        raw=stage/'sealed.buipkg'
        subprocess.run([compiler,'pkg','seal',str(stage),'-o',str(raw)],cwd=ROOT,check=True)
        if source_digest()!=expected_source:raise RuntimeError('Native inputs changed while sealing')
        # Buoy0.72 Windows cannot inflate deflated package payloads. Store-only
        # changes archive bytes but preserves the sealer's content digest.
        fd,name=tempfile.mkstemp(prefix='.bpmhost-',suffix='.buipkg',dir=output.parent)
        os.close(fd);stored=Path(name)
        try:
            with zipfile.ZipFile(raw) as original, zipfile.ZipFile(stored,'w',compression=zipfile.ZIP_STORED) as rewritten:
                for entry in original.infolist():
                    data=original.read(entry.filename)
                    entry.compress_type=zipfile.ZIP_STORED
                    rewritten.writestr(entry,data)
            os.replace(stored,output)
            receipt=stage/'package-build.json'
            receipt.write_text(json.dumps({'schema':1,'sourceSHA256':expected_source,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'packager':tool},indent=2)+'\n')
            publish_file(receipt,output.with_name(output.name+'.build.json'))
        finally:
            stored.unlink(missing_ok=True)
    return output

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--target',choices=TARGETS);parser.add_argument('--cc')
    modes=parser.add_mutually_exclusive_group();modes.add_argument('--native-only',action='store_true');modes.add_argument('--seal-only',action='store_true')
    args=parser.parse_args()
    if not args.seal_only:build_native(args.target,args.cc)
    if not args.native_only:print(seal())
if __name__=='__main__':main()
