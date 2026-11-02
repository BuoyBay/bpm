# bpm

A Git-first Buoy package manager, written in Buoy with a small native host library. The client includes
minimum-version resolution, exact commit pins, fork overrides, locked recovery,
vendoring, workspaces, immutable publication and package inspection. It feeds the
compiler's existing `.buipkg` system and preserves source imports.

## Build and verify

Requires Buoy 0.72.0 or compatible, Git and tar; HTTPS registries also require
curl. Linux host builds use a native Buoy build driver and a C11 compiler.
The driver needs the ProcessKit package when it is compiled, plus GNU coreutils
(`cp`, `mktemp`, `sha256sum`, `uname`). For a direct source build, clone the
[ProcessKit checkout](https://github.com/BuoyBay/processkit) beside this repository
as `../processkit`. Its public native source supplies BPM's private process host;
seal its manifest-root package to bootstrap the builder. The assembled integration layout also
works, using its existing package and logical source paths.
Use `BPM_PACKAGER` for the SDK executable that accepts `pkg seal`; on installations
with the local Linux compile-only wrapper, set `BPM_PACKAGER=/usr/bin/buoy` before
these commands.
The installed client does not require Python or a shell. It executes tools using
argument arrays and uses native hashing, temporary directories, symlink checks,
writer claims and process liveness checks.

```sh
mkdir -p packages dist/bpm-native-builder
"${BPM_PACKAGER:-buoy}" pkg seal ../processkit -o packages/processkit-0.1.0.buipkg
buoy tools/build_bpm.bui -o dist/bpm-native-builder/build-bpm
./dist/bpm-native-builder/build-bpm
./dist/bpm-native-client/bpm --version
./dist/bpm-native-client/bpm --help
```

The native builder supports x86_64/ARM64 Linux hosts and rejects cross-target
requests. It checks all six desktop targets, builds/seals `bpmhost`, and compiles
in a temporary project with isolated compiler HOME/XDG/AppData directories. The
native library must accompany the executable; retain compiler-delivered runtime
files when distributing `dist/bpm-native-client/`. The builder writes `bpm.runtime.json` beside
the executable with the exact filenames and SHA-256 hashes to copy together
(`bpm.exe.runtime.json` for Windows). See [native host details](native/README.md).

Choose compiler tools with `--compiler FILE`, `--packager FILE`, and `--cc FILE`
(or `BPM_BUOY`/`BUOY`, `BPM_PACKAGER`, and `CC`). Each value is one executable
path; use repeated `--compiler-arg ARG`, `--packager-arg ARG`, and `--cc-arg ARG`
for literal extra arguments. `--root DIR` selects source inputs and `--output FILE`
selects the output relative to that root. Sources default to `src/main.bui` and
`src/native/bridge.c`, with legacy integration paths accepted. `--processkit DIR`
(or `BPM_PROCESSKIT`) selects a different ProcessKit checkout or its exact
`native/bridge.c` file; otherwise sibling/public workspace locations take precedence
over `libraries/processkit` in an integration tree. The native path never invokes Python
or a command shell. Source and tool hashes, ELF architecture, compiler diagnostics,
and deployed runtime identity are checked before publishing the runtime receipt.

The retained Python 3.9+ builders provide the unported macOS/Windows and
cross-compilation fallback. Run those legacy builders from the assembled
integration layout; their client has the same runtime behavior.

The Windows backend is implemented and an x86_64 executable cross-builds
from the assembled integration root:

```sh
python3 tools/build_bpm.py --target x86_64-windows --cc x86_64-w64-mingw32-gcc --output dist/bpm.exe
```

Build the Windows distribution on a working Linux/Mac sealing host using the
cross-target builder above, then copy every file in `bpm.exe.runtime.json` together.
The native Windows builder currently reaches the upstream sealing failure and
is not a working bootstrap path with the recorded Buoy 0.72.0 toolchain.
Windows uses native Unicode/reparse-point
handling and process job ownership; the validated workflows and compiler limits
are recorded below.
Linux and an ARM Mac execute the integration suite. Windows x64 has native archive
installation, offline recovery and consumer execution evidence, with native Buoy
sealer and Unicode archive-inspection limitations. See the exact
[native platform acceptance matrix](PLATFORMS.md) before choosing a workflow.

`BPM_BUOY` selects the compiler executable. `BPM_PACKAGER` selects the executable
for `pkg seal`/`pkg info`. Both default to `buoy`; values are executable paths or
names, not shell command strings. On this Linux development machine, the
`/usr/local/bin/buoy` wrapper inserts compile flags before `pkg` subcommands.
Keep it for compilation, and set `BPM_PACKAGER=/usr/bin/buoy` for packaging.
`bpm doctor` checks the compiler, Git, sealing/inspection and SHA-256 tools.

## Project manifest

Run commands from the directory containing `bpm.json`. `bpm init` creates a new
manifest without overwriting an existing one. This example URL is illustrative:

```json
{
  "schema": 1,
  "buoy": "0.72.0",
  "dependencies": {
    "greetings": {
      "git": "https://github.com/example/greetings.git",
      "version": "1.0.0"
    }
  },
  "replace": {
    "greetings": { "path": "../greetings-fork" }
  },
  "allowHosts": ["github.com"]
}
```

`buoy` is an optional strict SemVer minimum for the compiler. `allowHosts` is an
optional root-owned acquisition policy covering transitive Git and HTTPS registry
sources; an explicit empty array denies network hosts. Its entries are exact
hostnames. It constrains acquisition, not publisher authenticity.

Each package directory has compiler `manifest.json` (schema 1, identifier `name`,
strict SemVer `version`) and the existing `modules/`, `declares/`, `resources/`,
`link/` and/or `runtime/` layout. Optional `bpm.json` declares its dependencies.
Dependency keys match compiler package names, ignoring case. Registry coordinates
and repository paths have separate identities from those names.

Supported source shapes:

```json
{
  "gitMinimum": { "git": "https://host/org/repo.git", "version": "1.2.0" },
  "gitPin": { "git": "ssh://git@host/org/repo.git", "rev": "FULL_COMMIT_OR_REF", "subdir": "package" },
  "local": { "path": "../fork/package" },
  "archive": { "archive": "../artifacts/package.buipkg" },
  "published": { "registry": "https://registry.example", "package": "@team/package", "version": "1.2.0" }
}
```

These labels illustrate shapes, not actual compiler names. `subdir` is optional
and cannot contain `.`/`..` components. Root Git versions use `v<version>` tags;
subdirectory versions use `<subdir>/v<version>`. A Git source has exactly one of
`version` or `rev`. Full commits are exact pins. File registries use an existing
directory path instead of an HTTPS URL. Opaque archives pin existing bytes and
have no inferred Git provenance or transitive requirements.

Git acquisition disables ambient Git configuration, hooks, checkout filters
and HTTP redirects. Renamed repository URLs must be updated explicitly rather
than following a redirect outside the configured host policy.
Use explicit HTTPS/SSH URLs without embedded HTTPS credentials; authenticated
Git is supported through SSH. Dependency scripts never run. Only the root's
`replace` directives apply, including replacements of transitive dependencies.
A replacement uses the same source shapes and preserves the compiler name.
Local paths are development snapshots, not live compiler redirects.

## Commands

```sh
bpm init
bpm add greetings --git https://host/org/greetings.git --version 1.0.0
bpm add greetings --git https://host/org/greetings.git --rev FULL_COMMIT --subdir package
bpm replace greetings --path ../fork
bpm unreplace greetings
bpm update greetings 1.2.0
bpm remove greetings
bpm resolve
bpm fetch
bpm install
bpm sync
bpm ci
bpm vendor
bpm verify
bpm list
bpm why greetings
bpm check main.bui
bpm build main.bui -o app
bpm repair
```

- `add`, `remove`, `replace`, `unreplace` and `update` edit the manifest, resolve
  and install for standalone projects. Workspace edits update guarded manifests
  and locks, then require an explicit `bpm sync`; they never start a partial
  multi-directory installation. `update` takes an explicit new minimum rather
  than choosing the newest release. Failed selection or lockfile publication
  restores prior metadata. Selection commits before standalone installation starts;
  an installation failure retains that selection for `sync` to retry. Interrupted
  selection edits retain snapshots under the root `.bpm/write-lock`; `repair`
  restores prior metadata when selection had not committed.
  If rollback itself fails, backups and the guard are retained and their location
  is reported. `resolve` changes only selection, snapshots and locks.
- Git/registry minima select the greatest **live** requested minimum for each
  shared source identity. Resolution recalculates requirements from selected
  versions until stable, including downgrading a requirement discarded by an
  upgraded parent. Obsolete missing/conflicting requirements are deferred until
  the live graph stabilizes. Newer available releases are ignored. Versions from different
  majors or incompatible exact origins fail instead of nesting duplicate packages.
  Pre-releases require an explicit request. There are no caret/tilde/upper-bound
  ranges, peers or optional dependency graphs. Dependency cycles and oscillating
  conditional graphs are rejected. Limits are 4,096 packages and 256 selection
  rounds; the algorithm is Go-inspired, not Go's exact versioned-graph MVS.
- `resolve` fetches requested revisions and records full Git commits, package
  identities, selected requirements, source/input SHA-256, exact archive SHA-256,
  replacements and compiler/sealer versions in `bpm.lock`. Previously locked
  release tags moving to a different commit fail. Pin a reviewed commit explicitly
  if intentionally accepting rewritten release history. Branch refs refresh only
  during explicit resolution.
- `fetch` restores missing artifacts from exact locked origins without changing
  selection. `install` creates a lock when absent; with a lock it follows that
  lock and restores missing artifacts. A stale lock always fails.
- `sync`, `ci` and `vendor` are offline: they use cached archives, vendored
  `packages/`, locked source snapshots, or local archive/registry bytes. They
  never fetch Git/HTTPS sources. `ci` requires the existing lock. `vendor` uses
  the compiler's normal `packages/` layout; commit its archives with both manifests
  and lock. `.bpm/` is disposable when exact archives are vendored.
- `verify` checks the manifest/workspace fingerprints, installed archive hashes
  and duplicate managed identities. From a workspace root it also checks members.
  `list` prints JSON; `why` prints root-to-dependency paths. `check`/`build` verify
  before invoking the compiler and forwarding its arguments. Their output starts
  with verification messages, so it is not a raw compiler-JSON stream.

Member locks must contain exactly that member's closure, even when an older lock
omits an explicit install array. Source restoration verifies every snapshotted file, requires the recorded sealer
version and compares the reconstructed archive hash. Payload timestamps are fixed
in a private staging directory before sealing, so ZIP metadata does not depend on
when restoration occurs. Live local source edits require `resolve` and `sync`.

## Workspaces

```json
{
  "schema": 1,
  "dependencies": {},
  "workspaces": ["apps/first", "apps/second", "libraries/greetings"]
}
```

Membership is explicit; globs and nested workspaces are rejected. Each member has
its own `bpm.json`; package members also have compiler `manifest.json`. Package
members become root-owned local replacements for their matching dependency names.
Overrides belong at the workspace root. Resolve there to produce one shared
selection and member locks. Each member installs only its own dependency closure;
the root does not inject every member's dependencies into every program.
Changing a member manifest makes the workspace fingerprints stale. Independent
package releases still need portable Git/registry requirements; local path
requirements cannot be published.

## Install ownership and recovery

One selected archive is installed per case-insensitive compiler package name.
An ownership index records files bpm may replace/remove. Unrelated manual archives
survive. Foreign archives claiming a managed name, modified managed files, and
same-identity duplicates fail. An exact canonical vendored archive can be adopted
on a fresh clone. `packages` symlinks within the project/workspace survive.
An external shared destination requires the local environment variable
`BPM_ALLOW_PACKAGE_DIR` set to that exact trusted absolute directory. A repository
manifest cannot opt into external writes. Buoy's global package store and its
subdirectories are always rejected, even with that opt-in. `HOME` must be set so
the client can validate this boundary.

Project/member writer guards serialize metadata mutation. Root/member lockfiles
are staged together; synchronous failures restore earlier bytes and persistent
snapshots let `repair` recover metadata after process interruption. A
separate guard on the actual package destination protects explicitly trusted
shared `packages` directories across projects. Cache files and state directory
components cannot be symlinks; immutable artifacts are published with atomic
replacement rather than following an existing cache entry.
Installation backs up affected files and records a journal before replacement;
exceptions roll back files and ownership metadata. `repair` recovers an
interrupted install and clears guards only after the recorded writer has exited.
From a workspace root it also recovers member guards, checking every member for
live writers before clearing any guard. Prepared shared-directory journals are
recovered by their owning project/member.
Finished journals and guards are renamed into private `.bpm-gc-*` directories
before deletion. Repair collects these retired directories without treating their
partially deleted contents as an active transaction. This covers process kills,
not filesystem power-loss durability.
It also removes a dead filesystem publication claim owned by this package/project.
Legacy claims without ownership metadata require manual inspection.
Unknown owners or externally changed files require manual inspection. Workspace
members are separate install transactions; this is not one atomic commit across
all member directories, nor a guarantee against power loss without filesystem
flushes. Avoid separate unmanaged writers targeting the same package directory.

## Pack and publish

In a package directory, add publication settings before packing:

```json
{
  "schema": 1,
  "dependencies": {},
  "files": ["modules", "resources", "README.md", "LICENSE"],
  "publish": { "registry": "../registry", "package": "@team/greetings" }
}
```

Create the filesystem registry directory first. See the runnable example for the
complete commit/pack/publish sequence.

```sh
bpm pack --dry-run
bpm pack
bpm publish
```

`files` is an optional allowlist of literal files/directories. Defaults select
compiler payload directories, README.md, LICENSE and LICENSE.md. Both manifests
are always selected. The dry run creates no project files and reports source
paths/sizes/hashes plus outer bundle members. Declared payload omissions fail in
both dry and actual packing. Hidden components, caches, dependency directories,
private-key extensions, symlinks, traversal and case-colliding paths are rejected
when selected. Limits are 20,000 selected files and 1 GiB of input. This is a
package boundary, not a general secret scanner.

`pack` produces `<name>-<version>.bpm.tar`: ordinary `.buipkg`, bpm metadata,
requirements and selected documentation. Buoy's sealer omits root bpm metadata,
so the envelope retains it. A local receipt pins the exact packed bundle.
Receipt paths and their parent directories cannot be symlinks; packing validates
the receipt destination before creating a bundle.
Publishing requires tracked, clean selected package inputs and that unchanged
bundle/receipt. It uploads the already packed compiler artifact, without
re-sealing or dependency hooks, and records commit/sealer provenance. Replacing
an existing release is an error. Root replacements/local path requirements are
not publishable. Removing `.bpm/` also removes pack receipts: pack again in a clean
release workspace before publishing rather than importing an unreviewed tarball.
Published registry dependencies must name HTTPS origins or absolute filesystem
registry directories; development-relative registry paths are rejected before
packing/publication because consumers cannot resolve them relative to the publisher.

Registries may be filesystem directories or an HTTPS service implementing the
[registry protocol](REGISTRY.md). `@scope/name` is a registry coordinate, not an
import alias. Filesystem access control is provided by OS permissions; HTTPS
ownership/permissions belong to the service. `BPM_REGISTRY_TOKEN` supplies a
URL-safe bearer token through a temporary curl configuration, not command arguments.
The client does not follow registry redirects or fall back to another origin.
Checksums and recorded commits establish integrity/provenance claims, not an
authenticated build attestation. Signing/trusted-CI infrastructure is separate.

Git acquisition discards inherited template, SSH command/helper, protocol and
TLS-verification override variables; file/ext transports and template installation
are disabled. Tar option environment variables are cleared for listing, extraction
and packing. Select trusted Git/tar executables on PATH. Normal SSH authentication,
certificate roots and proxy configuration remain host responsibilities.

## Compiler limits and validation scope

Imports remain unchanged when switching forks. Package-qualified, aliased,
selective, bare self-named and whole-package forms are exercised. Sibling imports,
quoted paths and `--module-dir` keep compiler precedence. Side-by-side package/type
versions need upstream identity support.

**Lock verification is not compiler isolation.** Buoy still discovers ambient
global/manual packages and auto-injects declarations/resources. An explicit
compiler/LSP package map is proposed in [BUOY_REQUESTS.md](BUOY_REQUESTS.md).
Native library/resource behavior still follows Buoy's existing target slices;
bpm does not build native dependencies automatically.

The integration suite uses real Git, sealing and compiler checking/building, with
local transport fixtures for Git/HTTPS protocol tests. It tests the checked-in
walkthrough, resolution, moved tags, offline recovery, publishing, tampering,
workspace closures, rollback and interrupted-install repair. A separate redirect regression uses real Git and local HTTPS servers with a
trusted test CA. These tests do not establish public-network interoperability or
arbitrary host/toolchain combinations. Linux and ARM64 macOS execute the suite;
Windows x64 executes the bounded archive-installation and consumer workflows in
the [platform acceptance matrix](PLATFORMS.md).

The [reference registry service](registry/README.md) is a separate executable;
operating a public service, signed provenance, source-only vendor export and
arbitrary version ranges remain outside the client contract. Windows Git fetches
validate the selected payload but lack the POSIX per-file fetch-time disk limit;
filesystem quotas are a separate deployment policy.

## Package documentation

Put Markdown guides, API reference pages, and local images under
`resources/buoy-docs/`, starting with `index.md`. Studio indexes this content
alongside the upstream Buoy documentation and supplies its shared theme,
breadcrumbs, heading outline, search, syntax highlighting, and code-copy controls.
Use semantic front matter for titles, summaries, categories, ordering, keywords,
and API symbols; package identity and version come from `manifest.json`.

The default package file selection includes these resources. Explicit `files`
allowlists must include `resources` or `resources/buoy-docs`; track them in Git
before publishing. Documentation follows the same immutable archive, integrity,
vendor, and recovery lifecycle as code. See the
[authoring and website export guide](https://github.com/BuoyBay/devtools/blob/main/tools/documentation/README.md) and
[exact format](https://github.com/BuoyBay/devtools/blob/main/tools/documentation/FORMAT.md). The greetings walkthrough includes a
small documentation tree. Static exports reuse Studio's reader for a future BPM
website and do not require a second content format.
