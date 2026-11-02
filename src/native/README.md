# bpm portable host

`bridge.c` supplies a private scalar ABI for bpm. The default Linux host path is
the native `tools/build_bpm.bui` driver documented in the [client README](../README.md).
It builds the host in a private temporary directory, seals its package, and
deploys the verified runtime beside the executable in `dist/bpm-native-client/`.
The retained Python fallback uses the generated integration layout. Run it from
`local/integration/` in the workspace, not from a standalone `src/` checkout.
It builds individual slices with `python3 tools/build_bpm_host.py`; cross-build Windows x64 with
`--target x86_64-windows --cc x86_64-w64-mingw32-gcc --native-only`, then
`--seal-only` to collect slices rebuilt from the current sources. The source
receipt covers the bridge, ProcessKit source and builder. Generated slices live
under ignored `build/`; the sealed package is `packages/bpmhost-0.1.0.buipkg`.

The native library uses UTF-8 inputs and wide Windows filesystem/process APIs.
All scalar integer ABI parameters/results are signed 64-bit. Buffer functions
`bph_realpath`, `bph_tempdir`, `bph_find_executable` and `bph_sha256_file` return the
UTF-8 byte count excluding the NUL terminator; failure returns `-1`. Buffer
capacity includes the NUL byte. Other operation status functions return `1` on
success and `0` on failure unless specified below. `bph_error()` returns a
thread-local diagnostic for the most recent host failure; paths and credentials
are not interpolated into it.

- `bph_pid()` returns the current process ID; `bph_alive(pid)` returns `1` alive,
  `0` known absent/exited, `-1` unknown/error. Recovery must fail closed on `-1`.
- `bph_millis()` returns a monotonic millisecond count; `bph_sleep(ms)` accepts
  0..60,000 milliseconds.
- `bph_mkdir_exclusive(path)` atomically claims a directory (`0700` on POSIX).
- `bph_no_symlink(path)` returns `1` for a normal or absent leaf, `0` for a
  symlink/Windows reparse point, `-1` for an inspection error. It detects dangling
  symlinks. The caller must validate parent components as well.
- `bph_realpath(path, buffer, capacity)` requires an existing directory. Windows
  output uses `/` separators, uppercase drive letters and no extended-path prefix.
- `bph_tempdir(parent, prefix, buffer, capacity)` uses 128 random bits from the OS
  and an exclusive directory claim; it does not create the parent directory.
- `bph_sha256_file` streams a regular input through a fixed-size SHA-256 buffer.
  It does not authenticate a publisher. Tests compare FIPS vectors, padding/block
  boundaries, million-a and random files with Python's independent hashlib.
- `bph_timestamp(path)` normalizes access/modified timestamps to UTC 2000-01-01.
- `bph_replace(source, destination)` atomically replaces within one filesystem.
  It rejects observed symlink leaves; it does not promise protection from a hostile
  local writer racing the inspection and operation.
- `bph_remove_tree(path)` is Windows-only recursive cleanup for caller-owned
  temporary paths. It removes read-only Git objects using no-follow handles, treats
  every reparse point as a leaf, and rejects drive/share roots, relative/device
  paths and reparse-point ancestors. Missing paths succeed idempotently. It is not
  a general user-file deletion API; callers retain ownership validation. POSIX
  returns unsupported and keeps the existing Buoy cleanup path.
- `bph_find_executable(name, buffer, capacity)` resolves explicit paths or PATH.
  Empty PATH entries do not imply the current directory. Windows accepts native
  `.exe` programs; `.cmd`/`.bat` are deliberately unsupported.

Process APIs are the existing ProcessKit scalar APIs with `bpk_` renamed to
`bph_proc_`; the Buoy module is not imported, avoiding an extra source entry point.
The builder checks exact source hooks before privately extending environment
merging to support deletion. `bph_proc_unset(handle, name)` differs from an empty
value and leaves the parent's environment untouched. Override/delete order is
preserved. `bph_proc_file_limit(handle, bytes)` sets POSIX hard and soft
`RLIMIT_FSIZE` in the command child before exec. Windows rejects a nonzero request;
it has no equivalent per-file job limit here. Download size caps and post-fetch
checks remain separate policies. The inherited ProcessKit process-tree ownership,
pipe draining, termination and confidentiality limits still apply.

Native compilation uses strict warnings. ELF64 machine, PE COFF machine and
Mach-O64 CPU identifiers are checked against the requested target before output
publication or source receipts and again when sealing. An incorrect cross compiler
cannot silently label a host binary as another architecture. The Windows MinGW build links its GCC
support statically and imports only OS DLLs (`bcrypt`, `KERNEL32`, `msvcrt`), not a
separately deployed pthread DLL. The package is repacked with ZIP Store while
preserving sealed member bytes/content digest for Buoy 0.72 Windows compatibility.
Native outputs, receipts and the final archive use atomic same-directory
replacement. The original ProcessKit source is not edited by this build.
Compilation uses private input copies and rejects changes to the originals before
publication. Native build receipts bind source and tool hashes to runtime/import
library bytes; sealing checks those bytes. Client builds seal a private package.
Individual file replacement is atomic; publication of a whole executable bundle
is not. An interrupted copy invalidates its inventory and requires rebuilding.

Native ABI execution is recorded on Linux x64, ARM64 macOS and Windows 11 x64;
see [platform scopes](../PLATFORMS.md) for expected skips and toolchain limits.
A remote native ABI
probe can set `BPM_HOST_TEST_LIBRARY` to a prebuilt matching DLL and run the test
file directly without a native compiler. Windows tests include read-only Unicode
trees, junction victim preservation and filesystem-root refusal; skipped tests
must not be presented as native proof.
