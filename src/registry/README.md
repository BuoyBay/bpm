# bpm reference registry

This is an implemented, locally tested registry service written in Buoy, with a
small Linux native storage helper. It implements [registry protocol 1](../REGISTRY.md)
for authenticated GET and immutable conditional PUT. It has not been deployed as
a public service.

**The server binds every interface.** Buoy's `HTTPServer` cannot select a bind
address. Configure host/network access restrictions before starting it; placing a
proxy in front does not make the backend listener private. TLS and authentication
remain required on the backend. There is no plaintext or loopback-only switch.

## Build

The current builder supports **Linux x86_64 on the recorded toolchain layout**.
It requires Python 3, a C11 compiler, Buoy 0.72.0 or compatible, the installed
`HTTPServer` and `crypto` packages, and the native libraries described below.
Python orchestrates the build and tests; the running registry is Buoy/C.

```sh
python3 src/registry/build.py --output dist/bpm-registry
```

`BUOY` selects the compiler executable, `CC` selects the C compiler executable,
and `BUOY_PACKAGES` selects the directory containing exactly one
`HTTPServer-*.buipkg` and one `crypto-*.buipkg`. The default package directory is
`/usr/share/buoy/packages`. The builder stages a separate compiler project so
unrelated repository application-resource manifests cannot affect the build.
It checks all six desktop target triples and rejects compiler errors or warnings.
Those checks do not establish native support beyond Linux x86_64.

Keep the executable and its emitted runtime libraries together, including
`libbpm_registry_storage.so`. Copying only the executable is insufficient.

### Local toolchain link workaround

On the recorded Buoy 0.72.0 Linux installation, an ordinary HTTPServer consumer
passes checking but does not automatically link its TLS runtime. Supplying the
real bundled TLS archives then exposes symbols absent from the bundled Linux
linker stubs. BUI-301
contains a minimal trigger, exact failures, hashes and a removal/retest rule.

The builder currently uses these native paths:

- `/usr/lib/buoy/libbuoy_tls-x86_64-linux.a`, `libssl-x86_64-linux.a` and
  `libcrypto-x86_64-linux.a`;
- `/usr/lib/buoy/stubs/x86_64-unknown-linux-gnu`;
- the host's `/usr/lib/x86_64-linux-gnu/libc.so.6`, `libstdc++.so.6` and
  `libc_nonshared.a`.

It privately copies the stubs, substitutes copies of the two actual host shared
libraries, and links the real archives plus `libc_nonshared.a`. It never modifies
the installed toolchain and does not supply fake TLS/runtime implementations.
The resulting executable depends on that host ABI; this is a native build recipe,
not a portable Linux distribution or cross-compilation guarantee. A different
installation needs a verified native build recipe. Keep the normal local Buoy
compiler wrapper where it supplies required CRT/stub path fixes.

## Run

Create a service-owned store with an absolute, symlink-free path. Every directory
component must be a real directory. The service takes an exclusive advisory lock
on `.registry.lock`; a second instance using the same store fails startup. Other
applications must not write into the store.

For a local test, use a certificate trusted explicitly by the client. The following
example generates temporary test credentials; substitute operator-managed
certificates and secret delivery for a deployed service.

```sh
(
    umask 077
    work=$(mktemp -d)
    mkdir "$work/store"
    openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 1 \
        -subj /CN=localhost -addext subjectAltName=DNS:localhost \
        -keyout "$work/key.pem" -out "$work/cert.pem"
    export BPM_REGISTRY_SERVER_TOKEN="$(openssl rand -hex 32)"
    # Retain this shell's token/certificate privately for the test client.
    ./dist/bpm-registry --root "$work/store" --scope @team --port 8443 \
        --cert "$work/cert.pem" --key "$work/key.pem" --max-bytes 33554432
)
```

The foreground process runs until stopped by the operator. It has no admin API,
account/login service, token issuance endpoint or token hot reload. Supply the
same secret to a client as `BPM_REGISTRY_TOKEN`; supply the test CA file through
`CURL_CA_BUNDLE`. Token values belong in process environment/secret delivery, not
command arguments or committed project files.

| Option | Contract |
| --- | --- |
| `--root DIR` | Existing absolute store; symlink components are rejected. |
| `--scope @team` | This token can read/write packages only in this exact scope. |
| `--scope unscoped` | Explicitly select the unscoped namespace instead. |
| `--port N` | TCP port 1–65535; default 8443, bound on all interfaces. |
| `--cert FILE`, `--key FILE` | Required PEM certificate and unencrypted private key. |
| `--max-bytes N` | Per-request/file limit; default 32 MiB, configurable 1–256 MiB. |

`BPM_REGISTRY_SERVER_TOKEN` is required at startup and must contain 16–1024 ASCII
characters from `A–Z a–z 0–9 - . _ ~`. All matched GET/PUT routes require that bearer
token; wrong/missing credentials receive 401. One instance has one token with
read/write access throughout its configured scope. This is a single-tenant model,
not separate reader/publisher accounts or per-package ownership.

Buoy's documented TLS implementation loads only the first certificate in a PEM
chain. The local tests use a directly trusted self-signed certificate. Do not
assume a normal intermediate-chain deployment works: validate the actual client
trust chain, or use a correctly configured TLS frontend and independently trusted
backend connection with network restrictions. No TLS-verification bypass is
needed or used by the tests.

## Publication and recovery behavior

Only `package.buipkg`, `bpm.json`, `release.bpm.tar` and `release.json` are served
under valid scoped/unscoped package coordinates and strict SemVer paths. PUT
requires `If-None-Match: *` (otherwise 428). Complete data is written to a private
file, flushed, then atomically linked into place without replacing an existing
entry. Success is 201; an existing entry receives 412. The store uses directory
file descriptors and `O_NOFOLLOW` during traversal and reads/writes, including
when paths change between operations.

Upload the archive, bpm metadata and release bundle before `release.json`.
The final record is accepted only when its schema, identity/version, dependency
shape, archive SHA-256 and bundle SHA-256 validate against uploaded data. The URL
version must match, and release dependencies must equal the canonical dependency
object in `bpm.json`. Local path/archive dependencies are rejected. This checks
release consistency; the service does not resolve dependency graphs, inspect
archive/module semantics, authenticate claimed Git provenance or sign releases.
The bpm client still performs its own package identity, source-policy and checksum
validation.

`release.json` is the commit marker. Authenticated GET of a completed payload file
is allowed before that marker exists, so the client can compare/reuse identical
files after an interrupted publish. A missing commit marker means there is no
release to install. The service never replaces an uploaded file; differing
orphan payloads require operator inspection. No automatic orphan collection,
yanking, deprecation, deletion or release-repair endpoint is implemented.

The helper flushes a newly written file and its containing directory. This is not
a full power-loss durability claim for newly created directory trees or for the
whole multi-request publication sequence. A crash may leave hidden `.upload-*`
files. Random exclusive temporary names prevent a reused process ID from blocking
the next upload. Inspect/clean uncommitted state while the service is stopped; never edit a
committed version to republish it. A request can fail after bytes became visible,
so resolve an uncertain result by reading the committed state before retrying.

## Limits and verification

Metadata is capped at 1 MiB; this reference service's artifact/bundle limit is
smaller than the protocol client's 1 GiB download ceiling. Request bodies and
responses are buffered in memory, and each connection has its own HTTPServer
thread. The application mutex serializes storage/commit operations; it does not
bound simultaneous connections or pre-handler body buffering. Operators need
connection/rate/storage quotas and resource limits before exposing a service.
There is no load, denial-of-service or multi-node deployment certification.

The server is intended for a private, service-owned filesystem. An unrelated local
writer with access to that filesystem is outside the trust boundary. Advisory
locking serializes cooperating service instances; it cannot stop direct writes.
The service is single-process and has no distributed locking or object-store
backend. Its storage helper and native execution have only been verified on Linux
x86_64. Native macOS/Windows serving is not implemented.

Run the Linux integration tests after building the current bpm client:

```sh
python3 tools/build_bpm.py --output dist/bpm
BPM_REGISTRY_TEST_CLIENT="$(pwd)/dist/bpm" \
```

The tests build the real server and use real TLS. They cover authentication/scope,
conditional creation under competing PUTs, digest and dependency-metadata commit
validation, symlink rejection, oversized request rejection, and actual bpm
pack/publish/install/verify with immutable publication retry. Token-containing
client/server diagnostics are rejected. Test certificates are temporary and
explicitly trusted; public-network infrastructure and real certificate renewal
are separate deployment validation.
