# teez-cli

CLI frontend for [teez](https://github.com/Coditary/teez): `teez run <path>`.

## Build

Requires [teez-core](https://github.com/Coditary/teez-core) as a sibling directory, via `-DTEEZ_CORE_DIR=...`, or fetched automatically from GitHub when missing (`TEEZ_FETCH_CORE_IF_MISSING=ON`, default).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

```bash
./build/teez run .
./build/teez run /path/to/tests
```

When `teez-worker` is present at configure time, the worker is **linked into** `teez` (in-process for `teez run`, plus `teez worker` as a hidden subcommand). No separate `teez-worker` install or `worker_bin` config is required. Builds without an embedded worker fall back to a sibling `teez-worker` binary or `worker_bin` in `teez.config.lua`.
