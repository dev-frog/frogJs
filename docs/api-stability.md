# API Stability

What FrogJS promises from 1.0.0 on, so you can upgrade without reading every line of the changelog.

## Versioning

FrogJS follows [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`.

| Release | Can contain | Your code |
|---------|-------------|-----------|
| **Patch** (1.0.1) | Bug and security fixes | Keeps working |
| **Minor** (1.1.0) | New APIs, new options, deprecations | Keeps working |
| **Major** (2.0.0) | Breaking changes, listed in the changelog with how to migrate | May need changes |

## What is stable

Everything in the [API reference](api-reference.md) and [TypeScript declarations](../types/):

- Built-in modules, their exports, classes and methods
- Arguments, return values, events and their order
- Error classes and `code` values (`ERR_INVALID_ARG_TYPE`, `ENOENT`, ...)
- Command-line options and exit codes
- Where a Node.js API exists, its behavior in the cases the API reference doesn't describe as different

`make check-types` and `make check-docs` tie the declarations, the reference and the runtime together, so a change to any of them is visible in review.

## What is not stable

These can change in any release:

- **Error messages.** Match `err.code`, not `err.message`.
- **Output for people**: the format of uncaught error reports, the REPL's prompts and messages, and `--help`. (`util.inspect` output follows Node.js and may change with it.)
- **Anything starting with `_`**, and symbols you find by inspecting objects: `socket._handle`, `timeout._id`, ...
- **Stack traces**: their frames, and `frogjs:` script names.
- **Timing and scheduling** beyond what is documented. Order is promised only where the reference states it.
- **Performance** numbers, and memory use.
- **The C++ code** under `src/` and `include/`: FrogJS has no native addon API.
- **Bundled versions**: which V8 and libuv are used (see below).

## Fixing a bug can change behavior

A fix can break code that relied on the bug. If behavior contradicts the documentation or Node.js, that is a bug, and it can be fixed in a patch release. Fixes that could reasonably affect working code are called out in the changelog. Security fixes can change behavior in a patch release when there is no other way.

## Deprecation

An API isn't removed without warning:

1. It is marked deprecated in the reference and declarations, in a minor release, with what to use instead.
2. It stays and keeps working for the rest of that major version.
3. It can be removed in the next major version, which is listed in the changelog.

## JavaScript and V8

JavaScript language features come from V8, which FrogJS links dynamically from your system. New language features can appear in any release. A language change V8 itself makes isn't a FrogJS breaking change. The minimum V8 and libuv versions can rise only in a minor or major release, noted in the changelog.

## Relationship to Node.js

Node.js is the reference for behavior, not a promise of completeness: FrogJS implements a subset. Known differences are listed in the [API reference](api-reference.md). A difference that isn't listed is a bug.

## History

The 1.0 stability criterion asked for 6 months without API changes. That hasn't happened yet: 1.0.0 itself made breaking changes, deliberately, so they wouldn't be needed afterwards. Stability starts counting from this release. See the [changelog](../CHANGELOG.md).
