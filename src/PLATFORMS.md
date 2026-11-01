# Native platform acceptance

These results describe actual execution of bpm 0.3.0 with Buoy 0.72.0, not just
cross-target compiler checks. They do not certify arbitrary hosts or deployments.

| Host | Confirmed native behavior | Remaining boundary |
| --- | --- | --- |
| Linux x86_64 | Core resolver/install/publication, native helper, process contention, larger graph, TLS download faults, documentation lifecycle, real TLS registry and consumer execution | Public deployment, certificate chains, broad load and power-loss behavior |
| macOS ARM64 | Native build, core 49 tests, portable client/stress/documentation tests, native helper, copied-bundle health checks, Git argument boundaries and tar member parity | Intel Mac execution and other macOS/toolchain versions |
| Windows 11 x64 | Client initialization, locked archive installation, verification/offline CI, native consumer checking/building/execution, SHA-256, guard recovery, reparse/global-store rejection and real HTTPS Git transport | Native Buoy package sealing fails; original `pkg info` mishandles Unicode archive paths |

The final Linux run after audit fixes ran 113 tests with three Windows-only skips.
The Mac run completed 85 tests with five platform skips, including the audit and
environment regressions; Linux-only path-policy, audit25, build-input mutation,
strace cleanup, shared-install and registry suites
are outside that run. Exact source/binary hashes, native commands, diagnostics,
outputs and final focused reruns are recorded in:

- Linux and cross-platform audit history (private maintainer material)
- Mac native capture
- Mac focused captures
- Final Mac build and tests
- Final Mac collected inventory
- Final acceptance record and Linux log
- Windows native capture

## macOS distribution

The isolated builder copies non-system Mach-O dependencies, removes build-machine
runtime search paths, adds executable/loader-relative paths and refreshes ad-hoc
signatures. The runtime inventory lists the files to copy together. Native tests
run the copied bundle with a clean DYLD environment after the private build has
gone away. The packer disables macOS copyfile metadata so tar members match the
reviewed file list without AppleDouble additions.

## Windows boundaries and workarounds

The client activation manifest enables UTF-8 for its CRT filesystem operations
while preserving the compiler-generated UAC and DPI settings. Windows cleanup
uses no-follow handles, clears Git's read-only file attributes and removes
junctions as leaves; native ABI tests cover the victim directory remaining intact.

Use already sealed, ZIP Store `.buipkg` artifacts for the validated installation
workflow. The native Windows compiler's source-only `pkg seal` fails even with
ASCII paths, leaving an empty ZIP. Source dependency resealing, local source
replacement, `pack`, publication and `doctor` therefore remain blocked with that package CLI;
use a working Linux/Mac sealer rather than rewriting locked artifact bytes.
See BUI-320.

The original Windows `pkg info` additionally misreads Unicode archive paths;
see BUI-321.
The capture includes an explicit test-only UTF-8 activation change to a private,
unsigned compiler copy, leaving the installed compiler hash unchanged. With that
copy selected explicitly, Unicode archive installation and native consumer
execution pass. This is a local diagnostic/workaround, not automatic compiler
patching or a distributed modified toolchain; ASCII sealing still fails.

The Windows lab already applies the documented Buoy stack-reserve/runtime-path
workarounds. The native capture records that toolchain's exact hash. Windows Git
payloads are checked after fetch, but a POSIX `RLIMIT_FSIZE` fetch-time limit has
no Windows equivalent in this backend; filesystem quotas remain deployment policy.

## Repeating validation

Use macos_validate.py (maintainer-local) and
windows_validate.py (maintainer-local) with the existing authorized lab hosts.
They stage private projects and preserve evidence; do not replace shared toolchains
or another team's workspace. The registry reference executable remains a separate
Linux service with its own [deployment limits](registry/README.md).

Test suites and fixtures are maintainer-local and are not part of this public repository.
