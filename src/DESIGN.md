# bpm package system proposal

bpm should provide Git-first dependencies and reproducible vendoring, with an
inspectable publication workflow and optional registries. Stable Buoy imports
refer to package identities; Git commits identify immutable source snapshots.
The [implementation](README.md) exercises today's compiler boundary. This
document records the architecture and longer-term system design.

## Implementation status

Version 0.3.0 implements Git revision/minimum-version resolution, strict SemVer,
root overrides, moved-tag rejection, hash-addressed source/artifact caches,
lock-only recovery, archive adoption, offline artifact vendoring, explicit
workspaces with separate member closures, journaled installation/recovery,
inspection and immutable filesystem/HTTPS-client publication. The
[walkthrough](examples/hello/README.md) is exercised by integration tests.

The remaining proposals below include canonical multi-package module identities,
compiler/LSP isolation, side-by-side types, source-only vendor export,
authenticated build attestations and production registry infrastructure.
The client now uses a portable native argv/filesystem backend, including Windows
Unicode paths and process ownership. A Linux executable and Windows x64 executable
build locally. ARM64 macOS executes the integration suite; Windows x64 executes
the bounded archive-installation and consumer workflows in [PLATFORMS.md](PLATFORMS.md).
The client/server boundary is specified in [REGISTRY.md](REGISTRY.md). Compiler
checks cover all three desktop target families; native Windows source sealing
and original-compiler Unicode archive inspection remain upstream limitations.

## Identities and imports

Keep four concepts separate:

1. A logical dependency identity: canonical repository/module path, including an
   optional repository subdirectory and incompatible-major suffix.
2. An origin: Git URL and full commit, registry origin plus release digest, or an
   explicitly adopted local archive digest.
3. A compiler package identity: the existing `manifest.json.name`.
4. A file module: the `.bui` module inside that package.

An optional `@scope/name` is a publishing/discovery coordinate. Scope ownership
does not reserve a second compiler namespace. Bind it to an explicit registry
origin; a private lookup must never fall back to a public registry on failure.

Forks replace acquisition for the original logical dependency. They preserve its
compiler package identity and source imports. Root overrides apply to the whole
graph; dependencies cannot impose overrides on consumers.

Hash-addressed storage is useful:

```text
.bpm/git/<repository-and-subdirectory-sha256>/<full-commit>/
.bpm/artifacts/<archive-sha256>.buipkg
```

A checkout directory is not an import namespace. Updating a lockfile should not
rewrite imports in application or dependency sources. Import rewriting belongs
only in an explicit, reviewed API migration tool. Concurrent versions require
compiler support for distinct package/type identities, not merely different
directory names or local `As` qualifiers.

On today's compiler, select one artifact per case-insensitive package name and
install it into project `packages/`. Keep `.buoy/packages` and `.buoy/cache.db`
under compiler ownership. Preserve quoted imports, sibling precedence and existing
manual package workflows. Do not flatten dependency repositories into a shared
`--module-dir` search path.

## Manifest and lock evolution

Keep bpm metadata in `bpm.json`, separate from compiler `manifest.json`. Unknown
schema versions fail. Version 0.2 uses dependencies keyed by compiler name with
Git revision/minimum sources, local paths, archives or registries; the mature schema should add canonical logical
identity and publication metadata without redefining existing keys silently.

One module can export multiple compiler packages that release together. Independently
released packages use separate workspace members and manifests. Workspaces should
have explicit membership, one root lock, and predictable root-relative overrides;
glob membership can follow once exclusion and nested-workspace rules are defined.

A workspace lock can cover every member while installation remains specific to
each member's dependency closure. Avoid installing the union into every member:
auto-injected FFI declarations and unconditional resource delivery would expose
unrequested dependencies. Generate a package map or install destination per
member, with one root selection of shared logical dependency versions.

Commit a lock for both applications and libraries. It records the complete graph,
root-input fingerprint, resolver/schema versions, logical and compiler identities,
source URL/subdirectory/full commit, selected versions and edges, replacements,
archive SHA-256, source/input hashes, compiler/sealer versions, registry origin,
native target slices, and eventually provenance verification results. Hashes and
versions are strings. Compiler `xxh64` digests remain cache keys, not signatures.

Installation reads the lock. Resolution changes it deliberately. CI/frozen mode
must reject a missing/stale lock and must never resolve newer versions. Offline
mode must prohibit network access rather than merely hope the cache is complete.
Recovering a missing artifact must use the locked commit and sealer recipe and
verify the resulting artifact; it must never substitute a current tag or branch.

Adopting an existing archive can record its manifest name/version and SHA-256,
but cannot infer its original Git repository or prove publisher ownership. Keep
it as an explicit archive origin until a user supplies and verifies that mapping.
Do not invent repository requirements from short package names during `init`.

## Version selection

Use Go-inspired minimum-version selection: each dependency
requests a strict SemVer minimum, and the graph selects the greatest requested
minimum for each logical identity. Load requirements at selected versions until
the graph is stable. Do not select the newest available version merely because
it exists. [Go module resolution](https://go.dev/ref/mod#minimal-version-selection)
provides the architectural reference. The implementation uses a deterministic
fixed point over the selected versions' live requirements, rather than retaining
constraints from every visited version as Go's versioned graph does. It defers
errors for requirements that an upgraded parent may discard, detects repeating
states, and bounds resolution to 4,096 packages and 256 rounds.

Start without arbitrary npm ranges, peer dependencies, optional dependency graphs
or nested duplicate installations. Pre-releases require explicit requests.
Incompatible majors need different logical identities; on the current compiler
they also need distinct compiler package/type names. If exclusions are supported,
an excluded selected minimum should fail with the requirement chains rather than
invent an implicit upgrade. A targeted update raises an explicit minimum, then
reports all resulting graph changes.

Git tags resolve to full commits. Previously recorded release tags moving to a
new commit should fail unless explicitly accepted through a reviewed operation.
Unreleased commit pins remain first-class; a display pseudo-version must not
rewrite the compiler manifest. Define root and subdirectory tag conventions
before implementing tag discovery.

## Packaging and publishing

Borrow npm's pack inspection and explicit package boundary, and Cargo's
pre-publication validation. Packing must expose exact members, sizes and hashes
before publication; publishing uploads that reviewed artifact without re-packing
or running dependency scripts. These are adapted design choices, not a proposal
to reproduce every behavior of [npm pack](https://docs.npmjs.com/cli/v11/commands/npm-pack/)
or [Cargo publish](https://doc.rust-lang.org/cargo/reference/publishing.html).

Use allowlists into a clean stage before invoking `buoy pkg seal`. Validate
declared modules, native link/runtime slices, resources and licenses against the
selected files. Prevent traversal, escaping symlinks, case collisions and
credential/cache inclusion. Set explicit file-count, size and extraction limits.
Native dependency build recipes are reviewed, root-invoked operations; install,
fetch and publish do not run lifecycle hooks.

Support optional project-owned acquisition policies for approved Git hosts,
repository prefixes and registry origins. Enforce them for transitive dependencies
and replacements too. Report policy failures before fetching; acceptance should
be an explicit configuration edit or command option, never a hidden fallback.
These policies constrain acquisition and do not authenticate a publisher by
themselves. Version 0.2.1 implements exact host policies through root `allowHosts`.

The release envelope must bind dependency metadata and documentation to one or
more ordinary `.buipkg` artifacts. Version 0.2 uses a tar envelope
because Buoy's current sealer omits arbitrary root metadata. A future compiler
metadata extension could simplify this. Source release and binary target variants
need separately identified digests, with documented ABI/toolchain compatibility.

Registry releases are immutable at `(registry, scope/name, version)` and indexed
by content digest. Deprecation/yanking affects new resolutions without replacing
bytes needed by existing locks. Release channels are mutable pointers resolved
only during explicit updates. Ownership, visibility and permissions are separate
from import names. Cargo's [permanent version rule](https://doc.rust-lang.org/cargo/reference/publishing.html)
is a useful precedent for immutable releases.

Provenance binds repository, commit, subdirectory, source digest, artifact digest,
recipe, compiler/sealer version and authenticated builder identity. A checksum or
unverified source URL alone does not authenticate a publisher. Add trusted CI
publishing and signature verification after defining registry trust roots and
credential storage. Prefer short-lived scoped credentials; never store secrets
in manifests/locks. npm's [provenance publishing options](https://docs.npmjs.com/cli/v11/commands/npm-publish/)
are a reference for the desired publishing experience.

Workspace publication converts local links into released dependency requirements,
verifies availability or orders a release batch, and rejects unpublished local
replacements. A local development override may be explicitly snapshotted for
vendoring, while release publication requires portable dependency origins.

## Integrity and developer experience

Artifact vendoring is the default offline path: it preserves the exact installed
bytes without requiring a sealer or compiler version to recreate them. Add an
optional source-vendoring mode for source review and patching, alongside the
artifact/lock set. Its index must bind source paths and content hashes to selected
commits and artifact recipes. Edited vendor sources require an explicit refresh;
do not silently re-seal them during a frozen install. A source-only vendor set
still depends on the pinned sealer/native build environment for reconstruction.

Treat cached artifacts as immutable and verify them on consumption. Transactions
should publish a complete install generation plus its ownership metadata, with
crash recovery and locks on the actual shared destination. External shared
install targets need an exact local opt-in; compiler global stores are forbidden.
Workspace metadata edits and package installation are separate operations, with
synchronous rollback of staged lockfile publication. Preserve unmanaged
archives unless explicitly adopted; conflicting manifest identities are errors.

Compiler and LSP must use the same dependency graph. Diagnostics should explain
the requesting dependency chains, selected revision, effective replacement,
import identity and exact conflicting artifact. `bpm explain`, `outdated`,
`graph`, `why`, license inventory and machine-readable diagnostics should follow
the core resolver, not precede its correctness.

## Delivery gates

The client covers acquisition, minimum-version selection, overrides,
archive hashing, frozen recovery, vendoring, workspaces, compiler imports,
journaled installation and packing/publication. Registry publication follows the
release-envelope protocol; a hosted server remains independently operated.

Next gates are broader platform/network validation, canonical multi-package
module identities, native platform execution, compiler isolation and
authenticated build attestations. Side-by-side versions require package-qualified
type identity support in Buoy.

Keep compatibility fixtures for all supported imports, same-basename package
modules, local shadowing, global-package contamination, debug development fallback,
native library/resource delivery and compiler cache refresh. Exercise tampered
archives, moved tags, offline fresh clones, interrupted installs, untrusted paths
and dry-run/member parity before widening the implementation's guarantees.

## Consultation decisions

The Astra, Opus and Grok consultations informed this proposal. They converge on
Git acquisition, separate acquisition/import identities, existing `.buipkg`
installation, root replacements and avoiding dependency lifecycle scripts.
The following decisions resolve differences between their recommendations:

- Use strict minimum requirements for the Go-style resolver. Arbitrary
  caret/tilde/upper-bound constraints are not the same problem as Go MVS and would
  need separately specified selection/conflict rules. Version 0.2 supports minima and explicit revision pins.
- Verify archive SHA-256 now. Retain compiler `xxh64` for compatibility; delaying
  cryptographic integrity until a future audit command weakens locked installation.
- Vendor exact archives first, with optional source snapshots later. Source review
  is valuable, but source-only vendoring must not silently replace reproducible
  archive installation with toolchain-dependent re-sealing.
- Keep Git acquisition usable without a registry. Scoped registries can add
  discovery, ownership and immutable publication without becoming mandatory for
  Git dependencies. A future transparency service is additive, with privacy rules
  for private repositories and explicit trust roots.
- Do not use the documented debug `packages-src` fallback as a general local
  override: it does not satisfy package-qualified imports. Seal snapshots today;
  request explicit live source mapping from the compiler for the future edit loop.
- Preserve manual archives and fail on conflicting package identities. A warning
  alone would let the compiler choose an ambiguous dependency outside the lock.

Compiler probes govern compatibility decisions where documentation or consultations
differ. In particular, same-basename package modules already work, ambient global
packages remain visible, and root bpm metadata needs an outer release envelope.
