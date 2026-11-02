# BPM platform requirements

BPM is a native command-line client. Compiler checking, native linking and actual
execution establish different properties; validate the selected toolchain on the
host that will run the client.

| Platform | Build path | Requirements and boundaries |
| --- | --- | --- |
| Linux x86_64/ARM64 | Native `tools/build_bpm.bui` driver | C11 compiler, matching Buoy SDK, ProcessKit source, Git/tar/coreutils; curl for HTTPS registries |
| macOS | `tools/build_bpm.py` compatibility builder | Matching Buoy SDK and Apple command-line tools; copy deployed dylibs with the executable |
| Windows x64 | Compatibility/cross-target builder | Matching C/C++ runtime and native bridge; Buoy 0.72.0 package-command limitations apply |

See the [build guide](README.md) for commands and explicit toolchain overrides.

## macOS deployment

The compatibility builder copies non-system Mach-O dependencies, removes
build-machine search paths, adds executable/loader-relative paths and refreshes
ad-hoc signatures. Copy the runtime inventory's files together. The packer
disables copyfile metadata so tar members do not acquire AppleDouble additions.
Exercise the copied deployment with the SDK/runtime configuration intended for
its destination machine.

## Windows package commands

The client preserves the compiler-generated UAC/DPI settings while enabling
UTF-8 filesystem handling. Cleanup treats junctions as leaves and removes Git's
read-only attributes without traversing reparse targets.

Buoy 0.72.0's native Windows `pkg seal` can fail even on ASCII source paths.
Use compatible, already sealed ZIP Store `.buipkg` archives for installation, or
select a working Linux/macOS packager for source sealing. Source replacement,
`pack`, publication and `doctor` require a working package CLI.

That SDK's original `pkg info` also has a Unicode archive-path limitation.
Select a compatible toolchain explicitly with BPM's compiler/packager overrides;
BPM does not patch the installed compiler automatically.

Windows Git payloads are checked after fetch. This backend does not implement
POSIX `RLIMIT_FSIZE` during Git acquisition; apply appropriate storage quotas
for the deployment.

The [reference registry](registry/README.md) is a separate Linux service with its
own TLS, filesystem and deployment requirements.
