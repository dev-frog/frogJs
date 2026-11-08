# Contributing to FrogJS

Thanks for helping! This guide covers setting up, the checks every change has to pass, and how to add to the runtime. How the code is written is in the [style guide](style-guide.md); how the runtime fits together is in the [architecture document](architecture.md).

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Setting Up](#setting-up)
- [Development Workflow](#development-workflow)
- [Checks](#checks)
- [Tests](#tests)
- [Adding to FrogJS](#adding-to-frogjs)
- [Pull Requests](#pull-requests)

## Code of Conduct

Be respectful and constructive, assume good intent, and focus on what's best for the project and the people using it.

## Setting Up

You need:
- A C++20 compiler (Clang or GCC)
- V8 and libuv
- Node.js 20+ and npm, for the linters, the TypeScript checks, and comparing behavior with Node.js
- Optional: clang-format, for `make format`

```bash
# macOS
brew install v8 libuv node clang-format

git clone https://github.com/dev-frog/frogJs.git
cd frogJs
make            # build/frogjs
npm install     # ESLint and TypeScript (make lint / make check-types run it if needed)
make check      # lint, tests and type checks: everything CI runs
```

The Makefile finds Homebrew's V8 and libuv. Elsewhere, pass the install prefixes: `make V8_PATH=/usr/local LIBUV_PATH=/usr`. The V8 sandbox defines are detected from the library. The build is optimized (`-O2`); `make DEBUG=1` builds for a debugger. The compiler records header dependencies, so `make` rebuilds what a changed header affects.

## Development Workflow

1. Find or open an issue, and say you're working on it.
2. Branch from `main`: `git checkout -b feature/short-name` (or `fix/...`, `docs/...`).
3. Make the change, with its test and documentation (see [Adding to FrogJS](#adding-to-frogjs)).
4. Run `make check`, and `make format` to format the C++ lines you changed.
5. Commit with a [conventional commit](style-guide.md#commits) message and open a pull request.

## Checks

| Command | What it checks |
|---------|----------------|
| `make test` | Every self-test in `examples/` (see [Tests](#tests)), `examples/test-cli.sh` and `examples/test-repl.py` |
| `make lint` | ESLint on the examples, the tools and the JavaScript embedded in `src/`; every C++ file compiled with `-Werror` |
| `make check-types` | `types/test` type-checks under `--strict` (and rejects the mistakes in `types/test/errors.ts`); the compiled test runs in FrogJS; `tools/check-types.js` finds declarations that don't exist at run time |
| `make format-check` | clang-format on the C++ lines changed since `main` (`make format` fixes them) |
| `make check-docs` | Every declared export appears in `docs/api-reference.md` |
| `make check` | lint + test + check-types + check-docs |
| `make test-asan` | The tests under AddressSanitizer and UndefinedBehaviorSanitizer |
| `make leakcheck` | Native leaks at exit (macOS `leaks`) |
| `make coverage COVERAGE_MIN=90` | Line coverage of the C++ and the embedded JavaScript; add `--show-missing` to `tools/coverage.py` to see which lines |
| `make fuzz` | Random input for the HTTP parser and Buffer. `FUZZ_SECONDS` sets the time per target, `FUZZ_SEED` repeats a run. It opens many connections, so run it last: ports stay in `TIME_WAIT` for a while |
| `make bench` | Benchmarks against Node.js |

CI (`.github/workflows/build.yml`) builds with `make WERROR=1` on macOS, then runs all of these except the benchmarks.

## Tests

The tests are FrogJS scripts that check themselves: `examples/test-*.js` print a line per check, end with `N passed, M failed`, and exit with code 1 if one fails. `make test` runs them, and also fails a test that doesn't reach its last line.

Where FrogJS implements a Node.js API, **the same test file must pass under Node.js** too: that is how expected values, error messages and event orders are verified. (`test-http.js` is the exception: it also checks FrogJS-specific behavior, such as answering `500` when a handler throws.)

```bash
./build/frogjs examples/test-child-process.js
node examples/test-child-process.js     # must agree
```

A new test goes in `examples/test-<area>.js` with the helpers of `examples/lib/check.js` (`check`, `throws`, `rejects`, `run`). Add it to `TESTS` in the Makefile and in `tools/coverage.py`, and to the examples list in `README.md`. Test the edges and the failures too: wrong argument types, error codes, empty input, and what happens after close or exit.

Changes to native code that manages memory or threads need `make test-asan` and `make leakcheck`.

## Adding to FrogJS

### A function in an existing module

1. Implement it: in the module's embedded JavaScript if it can be written in JavaScript, with a native helper in the same `.cpp` file for what can't.
2. Match Node.js: same arguments, defaults, return value, error classes, codes and messages.
3. Add checks to the module's self-test; run it under FrogJS and Node.js.
4. Declare it in `types/<module>.d.ts` and use it in `types/test/usage.ts`.
5. Document it in `docs/api-reference.md` and `FEATURES.md`.

### A new built-in module

1. Create `src/bindings/<name>.cpp` with `void Setup<Name>(Isolate*, Local<Context>)`. It runs the embedded JavaScript with `frogjs::RunJsModule()` and sets the result as the global `<name>`. Modules it needs (`events`, `stream`, ...) are read from the global object and passed in.
2. Add the file to `SRCS` in the Makefile, declare and call `Setup<Name>` in `src/core/runtime.cpp` (after the modules it depends on), and add the name to the built-in list in `SetupModules()` in `src/bindings/modules.cpp`.
3. Keep per-isolate state `thread_local`, use `frogjs::Loop()`, call JavaScript from the event loop through `frogjs::MakeCallback()`, and release what the thread still holds with `frogjs::AddThreadCleanup()`, so the module works in workers and doesn't leak (see [architecture](architecture.md#memory-management)).
4. Add `examples/test-<name>.js`, `types/<name>.d.ts` (referenced from `types/index.d.ts`, plus its global in `types/globals.d.ts` and `eslint.config.js`), and the documentation.

### Versions

`include/frogjs/version.h` holds the version shown by `process.version` and `--version`, and `package.json` holds the same version. A release also updates `CHANGELOG.md`, `ROADMAP.md`, and the version strings in `README.md`, `FEATURES.md` and `docs/api-reference.md`. What may change in which version is in the [API stability policy](api-stability.md).

## Pull Requests

- **Title**: a conventional commit subject (`feat(http): support trailers`).
- **Description**: what changed and why, how you tested it (and that Node.js agrees, where that applies), and anything that behaves differently from before.
- **Checks**: CI must pass. A maintainer reviews; please address comments with new commits, and squash them if asked.
- **Scope**: one change per pull request. Formatting changes to code you didn't otherwise touch go in a separate PR.

## Questions

Open a GitHub issue for bugs and feature requests, and a discussion for questions. Contributors are listed in [CONTRIBUTORS.md](../CONTRIBUTORS.md).

By contributing you agree that your contributions are licensed under the MIT License.
