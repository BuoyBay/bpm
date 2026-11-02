# Buoy improvements for bpm

These are concrete proposals for discussion with Buoy's creator. They have not
been submitted. They preserve existing source import spellings while giving the
compiler, LSP and package manager a shared, explicit dependency contract.

## Explicit package input map

Captured as enhancement discussion draft BUI-201,
with an isolated runnable probe and recorded declaration/resource delivery evidence.

Propose `--package-map <json-file>` and matching LSP project configuration. The
flag name/schema are suggestions, not available compiler options.

The map should identify a project root and an exact set of package inputs, each
with its compiler identity, immutable artifact path/digest, or an explicit local
source root for development. In this mode, do not scan unrelated project/global
packages, inject their FFI declarations, link their libraries or copy resources.
Missing inputs and duplicate identities should fail before code generation.

The map should reference bpm's resolved output without requiring Buoy to implement
Git, registry authentication, SemVer or the entire bpm lock format. The LSP must
consume the same generated map as compiler builds. Normal compiler invocation
without this flag can retain existing automatic discovery.

Acceptance fixture: an empty map cannot import a globally installed package and
does not receive its auto-injected FFI declarations/resources. A mapped package
still supports all existing import forms and native/resource delivery. The current
test `test_global_packages_are_ambient_even_with_empty_lock` demonstrates why a
manager-side lock alone cannot provide this behavior.

## Live source packages with consistent import behavior

Captured as enhancement discussion draft BUI-203.
Its controls reproduce the documented distinction between debug `Package` fallback
and package-qualified `Import`; they do not label that distinction a compiler defect.

Allow an explicit mapped source root to satisfy both package-qualified `Import`
and whole-package `Package`, including transitive imports, without sealing after
every edit. Tie cache invalidation and LSP updates to that source root. This
enables a local fork/workspace edit loop with stable imports in debug and release
configurations when explicitly requested.

The existing documented development fallback only handles `Package` with
`--debug` when no sealed archive exists. A mapped override should be explicit,
visible in diagnostics and never silently selected by a dependency's manifest.

Acceptance fixture: editing a mapped package is reflected immediately by compiler
and LSP; removing the map restores sealed-package behavior. Root-owned overrides
must not change application import statements.

## Stable package and type identities

Define module identity as package identity plus module path, and qualify public
types by owning package as well. Expose dependency aliases in the package map if
two incompatible versions are supported. Local import `As` aliases alone cannot
provide this: they change the qualifier visible in a file, not ownership of a
class or ABI.

The fixture `test_shared_module_basenames_and_local_shadowing` already confirms
two sealed packages can contain `Client.bui` under distinct package-qualified
imports. Preserve that behavior. The installed `package-import.md` documents
remaining collisions for same-named public classes/enums. Avoid rejecting all
same-basename package modules as a manager workaround.

Acceptance fixtures should distinguish same-named classes from two packages,
cross-package types in public signatures, generics, extensions and native library
names. Either support distinct identities consistently or produce a precise error
before promising side-by-side versions. The first bpm release can keep one version
per package identity while this is designed.

## Machine readable package operations and metadata

The retained-metadata portion is captured separately as enhancement discussion
draft BUI-202.
Structured command output remains a separate proposal, rather than a demonstrated defect.

Add structured JSON output for seal/info/verify, with stable schema and diagnostic
codes. Include the final artifact digest, exact members, package identity, target
slices and integrity results. Provide cryptographic artifact verification in
addition to the existing `xxh64` cache digest. bpm currently parses the human
`pkg info` name row to detect foreign archive conflicts.

Allow sealed packages to retain designated dependency metadata and license/readme
files without auto-importing or delivering them as runtime resources. A root
`bpm.json` currently disappears when a package is sealed; the integration suite
therefore verifies an outer release envelope carrying it alongside `.buipkg`.
Keep manager dependency schema independent from compiler manifest schema.

Acceptance fixture: pack inspection matches actual archive members and metadata
survives round-trip inspection. Unchanged inputs should produce stable artifacts
for one pinned sealer version. The integration work found that ZIP payload entries
retain input mtimes; bpm currently normalizes its private stage timestamps to
reproduce exact archive bytes during locked recovery. Record which metadata is normalized/generated.

## Portable argument based processes

An upstream API could accept an executable and an array of arguments,
working directory, explicit environment additions/removals, cancellation/timeout,
separate stdout/stderr and streamed binary file output. It should not invoke a
shell or reinterpret arguments on any target.

This removes platform-specific quoting from Git/compiler/tar invocation and
supports safe streaming acquisition with bounded memory. The installed Shell API
uses `/bin/sh` or `cmd.exe`, merges output streams and returns text from Execute.
bpm 0.3.0 now uses a private native host for argument-based processes and portable
filesystem operations, including explicit child-only environment deletion. Its
process implementation reuses the local ProcessKit bridge with private symbols;
upstream changes are not required for this client migration.

This is not an upstream-only blocker: the local
[processkit package](https://github.com/BuoyBay/processkit/blob/main/README.md) already supplies
argument arrays, working directories, child environment overrides and binary
stdout/stderr. The bpm host now uses it, removes POSIX shell utility dependencies and has Linux
and Windows x64 native slices. Native Windows lifecycle testing and broader
macOS execution remain validation work. Git, tar and curl are external executables
with literal arguments, not shell command strings.
Do not file the absence of this functionality in bpm as a compiler defect.

Acceptance fixtures include spaces, apostrophes, Unicode, literal `$()`/backticks,
empty arguments, large binary output, independent exit status and timeout cleanup
on macOS, Linux and Windows. Add filesystem primitives for symlink-aware inquiries
and canonical paths if these are not already exposed by a newer release.

## Cross platform archive delivery

Validate deflate `.buipkg` extraction on Windows and compare with the existing
store-only workaround documented in `containers/windows/README.md`. This is an
existing repository report, not a Windows runtime result of the bpm tests.
Publishers should not need different undocumented ZIP encodings for each OS.

Acceptance fixture: one sealed source-only archive installs/imports on all three
desktop platforms, with equivalent verified payloads. Native variant packaging
must explicitly record target triples, ABI and compiler compatibility rather
than treating a matching version string as sufficient.

## Evidence and priority

The explicit package map and live mapped sources have the highest immediate
upstream value. The native process backend is implemented locally; a built-in
process API is an optional upstream enhancement. Package-qualified types require broader language
design. Structured package operations and retained metadata can simplify bpm
without making the compiler a network package manager.

## Cleanup error-handler code generation

BUI-322 records a
minimal nested Return-in-Finally source that checks cleanly on six targets but
crashes Linux code generation. An independent agent repeated the crash and a
passing outcome-variable control. The expectation is a source diagnostic for
unsupported flow or compilation without a crash; no Return-in-Finally semantics
or compiler-source cause is assumed. bpm uses a separate outer error handler, so
this defect does not block the implemented client. Retest the exact trigger
before removing the workaround.
