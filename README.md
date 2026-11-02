# Install and lock Git packages

BPM acquires source packages, resolves declared dependencies and installs sealed
`.buipkg` archives without changing your imports. Lockfiles pin exact commits
and artifact hashes; recovery, vendoring and offline CI use those recorded inputs.

## Install a package

From a consumer project:

```sh
bpm init
bpm add algorithmkit --git https://github.com/BuoyBay/algorithmkit.git --rev refs/heads/main
bpm list
bpm verify
bpm build main.bui -o app
```

Use a full commit in `--rev` for an explicitly pinned source. Version selection
uses published version tags. Source manifests, subdirectories, replacements,
workspaces, registries and recovery commands are covered by the
[BPM guide](src/README.md). Private Git acquisition uses the documented SSH path;
HTTPS acquisition deliberately does not inherit ambient credential helpers.

## Build BPM

The Linux native builder needs Buoy, a C11 compiler, Git/tar/coreutils and a
ProcessKit source checkout. Provision its sealed bootstrap archive in `packages/`
so the compiler can build the driver, then run from this checkout:

```sh
mkdir -p dist/bpm-native-builder
buoy tools/build_bpm.bui -o dist/bpm-native-builder/build-bpm
./dist/bpm-native-builder/build-bpm --root "$PWD" --processkit ../processkit
./dist/bpm-native-client/bpm --version
```

The builder compiles the host bridge, seals it, checks all six targets and links
the Linux client. It rejects cross-target requests. Retain every runtime file in
the generated deployment receipt when moving the executable. Platform-specific
support and limits are documented in [PLATFORMS.md](src/PLATFORMS.md).

[Contributing](CONTRIBUTING.md) · [License](LICENSE)
