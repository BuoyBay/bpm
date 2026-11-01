# Run a package, switch to a fork, and publish

Run the following from this BPM checkout on Linux, with its sibling ProcessKit
checkout available as described in the client README. Work happens in a
temporary copy so the checked-in example stays unchanged. Requires the tools
listed in the [bpm README](../../README.md).

```sh
mkdir -p packages dist/bpm-native-builder
"${BPM_PACKAGER:-buoy}" pkg seal ../processkit -o packages/processkit-0.1.0.buipkg
buoy tools/build_bpm.bui -o dist/bpm-native-builder/build-bpm
./dist/bpm-native-builder/build-bpm
export BPM_EXAMPLE_EXE="$(pwd)/dist/bpm-native-client/bpm"
export BPM_EXAMPLE_WORK="$(mktemp -d)"
cp -R src/examples/hello "$BPM_EXAMPLE_WORK/hello"
cd "$BPM_EXAMPLE_WORK/hello/app"
```

On the current Linux development machine, set `BPM_PACKAGER=/usr/bin/buoy` to
bypass its compile-only wrapper for package operations. Other installations use
the normal `buoy` executable by default.

## Install and run

```sh
"$BPM_EXAMPLE_EXE" install
"$BPM_EXAMPLE_EXE" check main.bui
"$BPM_EXAMPLE_EXE" build main.bui -o hello
./hello
```

Expected output from the application: `Hello from the original package`.

## Replace with a fork

```sh
"$BPM_EXAMPLE_EXE" replace greetings --path ../fork
"$BPM_EXAMPLE_EXE" build main.bui -o hello
./hello
"$BPM_EXAMPLE_EXE" why greetings
```

The application now prints `Hello from the local fork`. Its import statement is
unchanged. The manifest and lock record the root replacement explicitly.

## Vendor and restore offline

```sh
"$BPM_EXAMPLE_EXE" vendor
rm -rf .bpm
"$BPM_EXAMPLE_EXE" ci
"$BPM_EXAMPLE_EXE" verify
```

The cache is disposable. The lock plus `packages/` contains the exact archive
needed by the compiler; `ci` does not fetch from the network. Commit those files
in a real project.

## Pack and publish the original package

Use the original package, which has no local replacements or local dependency
requirements. A filesystem registry needs no account or network service.

```sh
cd ../greetings
mkdir -p ../registry
cp bpm.publish.json bpm.json
git init -q
git add manifest.json bpm.json modules resources README.md
git -c user.name='Example Author' -c user.email=example@example.invalid commit -qm 'Example release'
"$BPM_EXAMPLE_EXE" pack --dry-run
"$BPM_EXAMPLE_EXE" pack
tar -tf greetings-1.0.0.bpm.tar
"$BPM_EXAMPLE_EXE" publish
```

The registry contains immutable `@demo/greetings/1.0.0/` release files. Repeating
`publish` fails. Modifying the packed bundle or its source inputs also fails.
The coordinate `@demo/greetings` controls publication; the compiler still sees
the package name `greetings`.

## Consume the published release

```sh
mkdir ../consumer
cp ../app/main.bui ../consumer/main.bui
cd ../consumer
"$BPM_EXAMPLE_EXE" init
"$BPM_EXAMPLE_EXE" add greetings --registry ../registry --package @demo/greetings --version 1.0.0
"$BPM_EXAMPLE_EXE" build main.bui -o hello
./hello
```

Expected application output: `Hello from the original package`. The consumer
does not depend on the publisher's source directory or need to re-seal it.

Your scratch copy remains under `$BPM_EXAMPLE_WORK` for inspection. Remove that
directory when finished.
