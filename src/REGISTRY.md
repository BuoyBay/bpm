# bpm registry protocol version 1

The client supports filesystem registries and HTTPS services. A registry is an
artifact store with immutable releases, not a compiler import namespace. This
repository implements the client, filesystem backend and a
[Buoy reference HTTPS registry](registry/README.md). The reference service has
local Linux integration coverage; no public registry has been deployed.

## Paths

For coordinate `@team/greetings`, version `1.2.0`, and registry base URL
`https://registry.example`, the client uses:

```text
/@team/greetings/1.2.0/package.buipkg
/@team/greetings/1.2.0/bpm.json
/@team/greetings/1.2.0/release.bpm.tar
/@team/greetings/1.2.0/release.json
```

Unscoped names omit the scope segment. Coordinates use lowercase ASCII letters,
digits, hyphens and underscores; scopes start with `@`. Versions use strict
SemVer. The compiler identity is the `name` in the sealed archive and release
metadata, independently of the scoped coordinate.

## Reading

GET `release.json`, then GET `package.buipkg`. The release record includes schema
1, compiler `name`, exact `version`, archive `sha256`, dependency source objects,
input hashes, `bundleSHA256`, sealer identity and recorded source commit. The
client validates requested identity, archive metadata and SHA-256 before using
the artifact. It does not extract the outer tarball during installation.

Metadata is limited to 1 MiB and artifacts to 1 GiB. The HTTPS client requires
TLS 1.2 or later, supplies connect/total timeouts, does not follow redirects and
never falls back to another registry. Filesystem reads reject symlink components.
These are client/protocol ceilings: a service can enforce lower upload limits.
The reference service defaults to 32 MiB per file/request and permits at most
256 MiB; metadata remains limited to 1 MiB.

An existing lock chooses the recorded version and digest, not the latest registry
content. Registry minima are explicit version paths; this release does not need
a search/version-list endpoint to select a newer available version.

## Publishing

Each HTTPS upload is a PUT with `If-None-Match: *`. The server **must** atomically
create the file only when absent and reject replacement, normally with HTTP 412.
Return a successful 2xx response only after storing the full file. The client
uploads package bytes, bpm metadata and bundle first; `release.json` is the final
commit record. Consumers must not infer a release from partial uploaded files.

For an interrupted upload sequence, identical existing payload files can be
verified and reused. Different existing payloads fail. Creating `release.json`
always uses conditional creation; publishing an existing release fails even if
its bytes are identical. Servers should provide operator-controlled cleanup of
uncommitted orphan payloads without allowing replacement of committed releases.

The filesystem backend stages a complete version directory, serializes competing
publishers with a version claim, and renames the prepared directory into place.
Readers only see a committed directory. Registry locations must already exist.
OS permissions determine who may publish into each scope. Claim directories
record the owning project and process. `bpm repair` in that project can remove a
dead claim for the configured version; it refuses a live writer or another
project's claim. Legacy claims without ownership records require manual inspection.

## Authentication and trust

If `BPM_REGISTRY_TOKEN` is set, requests include a bearer Authorization header.
Tokens use URL-safe ASCII characters. The temporary curl configuration is kept
under a private temporary directory and removed after the command. Token values
are absent from constructed command lines and ordinary client diagnostics.

An HTTPS service must enforce scope ownership, access permissions, private reads,
conditional creation and appropriate storage limits. Client-side scoped names
do not enforce registry ownership. The client does not implement login/account
management or issue credentials. Configure a service-issued scoped token through
the environment when using an existing HTTPS service.

The reference service requires a bearer token for all matched GET/PUT routes and
restricts one instance/token to one configured scope or the explicitly selected
unscoped namespace. It has no public-read mode, separate reader credentials or
account management. It requires TLS, but the underlying HTTPServer binds all
interfaces; enforce backend network restrictions explicitly. See its
[deployment and trust limits](registry/README.md).

The client disables the default curl configuration before loading its private
header configuration, so ambient `.curlrc` settings cannot enable redirects.

A SHA-256 checksum detects changes relative to a trusted lock or release record;
it does not independently authenticate the publisher. Commit/sealer fields are
provenance records, not verified signatures or trusted build attestations.
Signing, trusted CI publication and transparency logs need a separate trust
protocol. Never equate a same-named package from another registry automatically.

## Deployment checks

Exercise GET and conditional PUT against your configured endpoint. Confirm scope
and bearer-token enforcement, immutable version records, payload digests and
interrupted-upload recovery. Consumers discover a complete release through
`release.json`; payload availability alone is not a committed release.

Validate certificate trust, storage isolation, request limits and the intended
load/fault behavior in the deployment environment. Local protocol checks do not
establish production network or certificate configuration.
