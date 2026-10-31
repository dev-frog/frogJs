# FrogJS Style Guide

How code in FrogJS is formatted and written. The short version: **make new code look like the code around it**. The tools below catch most of the rest:

| What | Tool | Command |
|------|------|---------|
| C++ formatting | clang-format (`.clang-format`) | `make format` (lines you changed), `make format-check` |
| C++ warnings | the compiler, `-Wall -Wextra -Werror` | `make lint-cpp` |
| JavaScript (examples, tools, and the JavaScript embedded in `src/`) | ESLint (`eslint.config.js`) | `make lint-js` (`npm run lint`) |
| TypeScript declarations | tsc + `tools/check-types.js` | `make check-types` |
| Editor basics (UTF-8, LF, final newline, indentation) | EditorConfig (`.editorconfig`) | your editor |

`make lint` runs both linters; CI runs `make lint`, `make test` and `make check-types` on every push.

---

## C++

### Formatting

`.clang-format` holds the rules: Google style with 4-space indentation, 120 columns, and `Type* name` (pointer to the left). Run `make format` before committing: it runs `git clang-format`, which formats only the lines that differ from `main`. Much of the older code predates the config. It isn't reformatted wholesale, because that would bury real changes in the history. To format a whole file on purpose, run `clang-format -i <file>` in a commit of its own.

clang-format and `git clang-format` come with LLVM: `brew install clang-format`, `apt install clang-format`, or `pip install clang-format`.

### Warnings

The build uses `-Wall -Wextra` and must stay warning-free: CI builds with `make WERROR=1`. When you mean to ignore a `V8_WARN_UNUSED_RESULT` value (because a `TryCatch` handles the failure), say so with `(void)` and a comment:

```cpp
TryCatch try_catch(isolate);
(void)callback->Call(context, context->Global(), 1, argv);  // a throw is reported below
if (try_catch.HasCaught()) frogjs::ReportException(isolate, &try_catch);
```

An unused parameter loses its name: `void AllocBuffer(uv_handle_t* /*handle*/, size_t size, uv_buf_t* buf)`.

### Naming

| Kind | Style | Example |
|------|-------|---------|
| Functions | `PascalCase` | `SetupCrypto`, `ReportException` |
| Types (classes, structs, enums) | `PascalCase` | `PortHandle`, `Encoding::kHex` |
| Local variables and parameters | `snake_case` | `exit_code`, `handle_scope` |
| Struct fields | `snake_case` | `worker->exit_code` |
| Class data members | `snake_case_` | `isolate_` |
| Constants | `kPascalCase` | `kHelpText`, `kHostObjectPort` |
| File-level variables | `g_` prefix | `g_port_template` |
| Thread-local state | `tls_` prefix, or `g_` + `thread_local` | `tls_loop` |

Some older files (`net.cpp`, `fs.cpp`, `modules.cpp`) use `camelCase` locals; keep a file consistent with itself rather than mixing the two.

### Organization

- Shared declarations go in `include/frogjs/*.h` (`#pragma once`), inside `namespace frogjs`. Each binding exposes one `void SetupX(Isolate*, Local<Context>)`, declared in `runtime.cpp`.
- File-local helpers go in an anonymous namespace (or are `static`).
- Includes: the file's own header first if it has one, then `<v8.h>`/`<uv.h>`, then the standard library, then `"frogjs/..."`. clang-format doesn't reorder them.
- State tied to an isolate (cached templates, emitters, the module cache) is `thread_local`, because every worker thread has its own isolate. Use `frogjs::Loop()`, never `uv_default_loop()`.
- A `Persistent` or `Global` that lives in a `thread_local` must be a `Persistent`: a `Global` resets itself when the thread exits, after its isolate is gone.

### V8 and libuv

- Every function that makes handles opens a `HandleScope`. Callbacks from libuv also enter the context: `Context::Scope context_scope(context);`.
- Prefer `MaybeLocal::ToLocal(&x)` and handle the failure; use `ToLocalChecked()` only where V8 can't fail (making a short string, getting a property of a plain object you made).
- Calls into JavaScript from a libuv callback go through a `TryCatch`, and `frogjs::ReportException()` reports what was thrown (in a worker it becomes the `Worker`'s `'error'` event).
- Throw real error objects with a Node.js `code`, not strings: `Exception::TypeError(...)` plus `err.code = 'ERR_...'`. (The `fs` bindings still throw strings: a known gap.)
- Every libuv handle is closed with `uv_close()`, and the memory it lives in is freed in the close callback, not before.

### Comments

Say *why*, not *what*: a comment explains a decision, a constraint or a non-obvious consequence ("V8 needs more stack than a default secondary thread gets on macOS"). Sentence case, no Doxygen blocks. Comment a function above its definition when its contract isn't obvious from its name and arguments.

---

## JavaScript embedded in C++

Most module logic (`events`, `stream`, `http`, `util`, `crypto`, `child_process`, `worker_threads`) is JavaScript in a C++ raw string, run once per context by `frogjs::RunJsModule()`:

```cpp
static const char* kThingSource = R"JS(
(function (native, EventEmitter) {
'use strict';

// ...

return { createThing, Thing };
})
)JS";
```

- The source is one `(function (...dependencies) { ... })` expression. Its arguments are the native helpers and the other modules it needs; it returns the module's exports. There is no `require()` inside.
- **2-space indentation**, single quotes, semicolons, `const`/`let` (no `var`), `===` (except `== null`). ESLint checks it in place (`make lint-js`) and reports errors at their line in the `.cpp` file.
- Errors match Node.js exactly: same class, `code` and message, e.g. `makeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "file" argument must be of type string. Received type number (1)')`. Check them against Node.js (see "Tests" in the [contributing guide](contributing.md)).
- Internal properties start with `_` or use a `Symbol`; anything without that is public API and goes in `types/` and `docs/api-reference.md`.

## JavaScript examples and tests

`examples/*.js` and `tools/*.js`: **4-space indentation**, otherwise the same rules (ESLint checks them). Self-tests follow the pattern of `examples/test-crypto.js`: `check(name, actual, expected)`, a summary line, and `process.exit(1)` if anything failed.

## TypeScript declarations

`types/*.d.ts`: 4-space indentation, one file per module, a `/** ... */` comment on anything whose behavior isn't obvious from its signature, and FrogJS's real behavior where it differs from Node.js. See [types/README.md](../types/README.md).

## Documentation

- Markdown, one sentence or list item per line where practical.
- Code samples must run as shown in FrogJS.
- Update `docs/api-reference.md` (and `types/`) with the API, `FEATURES.md` and `README.md` with what users see, `docs/architecture.md` with how things work, and `ROADMAP.md` when a version item is done.

## Commits

[Conventional commits](https://www.conventionalcommits.org/): `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, with an optional scope (`fix(http): ...`). The subject is in the imperative ("add", not "added"). The body says what changed and why, as a short list if there are several parts.
