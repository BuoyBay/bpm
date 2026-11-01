# Contributing to bpm

This repository owns the **BPM** Git-first package manager under `src/`
and its build entrypoints. Preserve exact lock/recovery behavior, source-host
policy, package identity checks and literal process argument handling.

## Development conventions

Use the installed Buoy language reference and verify changed `.bui` files with
the compiler. Prefer native Buoy/BuoyScript for new automation. Preserve public
command names, environment-variable contracts and explicit platform limitations.

## Build context

Read the [BPM guide](src/README.md), including its platform limitations.
Linux native builds run from the assembled integration workspace with the
ProcessKit bootstrap archive, a C11 compiler, Git/tar and the selected toolchain:

```sh
mkdir -p dist/bpm-native-builder
buoy tools/build_bpm.bui -o dist/bpm-native-builder/build-bpm
./dist/bpm-native-builder/build-bpm
./dist/bpm-native-client/bpm --version
```

The native builder currently supports Linux hosts and rejects cross-target
requests. Other-host/cross workflows need separate native tooling and platform
validation. Keep all runtime
files in the builder's deployment receipt with the client. A source syntax check
is insufficient for changes to acquisition, native-host isolation or recovery.

## Verification and pull requests

For each edited Buoy entrypoint, in its documented staged dependency context:

```sh
buoy path/to/entrypoint.bui --check --diagnostic-format json
```

Require exit status zero and an empty `errors` array. Read warnings. Check each
affected conditional platform branch with `--target`; compiler checks do not prove
native runtime behavior on another OS. Build/link and exercise the changed API or
workflow on its supported host as well.

Test suites, fixtures and detailed reports are private/ignored for now. Maintainers
run those suites before merge or publication; a fresh checkout is not expected to
contain them. Include your compiler/tool versions, host/target, exact build and
reproduction commands, expected behavior and observed output in the pull request.
Do not commit private tests, generated builds, caches, credentials or machine
configuration. Keep the patch focused and note any unverified platforms or runtime
prerequisites. Inventory checks alone are not build or test acceptance.

## License of contributions

Original contributions are accepted under the existing license for the files you
change: `MIT OR Apache-2.0` for first-party code and `CC-BY-4.0` for documentation
prose. Code samples follow their code's license. No CLA or DCO signing step is
required. Keep your own authorship and copyright attribution.

Read [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md). Preserve upstream license,
copyright, attribution and provenance files for third-party material, and identify
its source in the pull request. Submit only material you have the right to license.
Reusable packages and tools keep their permissive licensing boundary; changes to
Studio's AGPL application code belong in its separate repository.
