# Support Policy

> **Status: draft for the maintainer to confirm.** A long-term support commitment is a promise of a person's time. These terms were written to be modest enough to keep; adjust them before relying on them, then remove this note.

FrogJS is maintained on a volunteer basis. There is no paid support, and the [MIT license](../LICENSE) comes with no warranty. This page says what you can expect anyway.

## Release lines

| Line | Status | Gets |
|------|--------|------|
| Latest minor of the current major (1.x) | **Active** | Features, bug fixes, security fixes |
| Latest minor of the previous major | **Long-term support** | Security fixes and critical bugs, for at least 12 months after the next major is released |
| Anything older | Unsupported | Upgrade: within a major version that is safe, see the [API stability policy](api-stability.md) |

Only the latest minor of a line gets fixes: there is no backporting to 1.0.x once 1.1.0 is out.

**Critical** means a crash, data corruption or memory error in normal use, without a reasonable workaround.

## Security fixes

Report vulnerabilities as described in [SECURITY.md](../SECURITY.md). The goal is an answer within 7 days, and a fix for a serious issue within 30 days, on supported lines. These are goals, not guarantees.

## Platforms

| Platform | Support | Meaning |
|----------|---------|---------|
| macOS on Apple Silicon | **Supported** | Built and tested in CI on every change, and released as a binary |
| macOS on Intel | Best effort | Should build; not tested. SHA-256 runs without hardware acceleration |
| Linux | Best effort | `install.sh` builds it with Homebrew's V8, but this hasn't been tested on Linux or in CI yet. Fixes are welcome, but it isn't a release blocker |
| Windows | Through WSL only | Natively, the REPL, signals and path handling assume POSIX. Under WSL it counts as Linux |

Results such as leak checks and benchmarks are measured on the supported platform and don't automatically hold elsewhere. Linux's leak check, for one, would need a different tool.

## Dependencies

FrogJS links V8 and libuv dynamically, so their security fixes reach you through your system: keep them updated (`brew upgrade v8 libuv`). FrogJS 1.0.0 was built and tested with V8 13.5 and libuv 1.52. It also compiles without warnings against V8 15.8, which is what Homebrew installs today, and CI runs the tests against it, but the AddressSanitizer, leak and benchmark results in these documents are from 13.5. A new V8 can require a new FrogJS release. `frogjs` is linked to the V8 it was built with: rebuild it (run `install.sh` again) after upgrading V8.

## Getting help

Open a [GitHub issue](https://github.com/dev-frog/frogJs/issues) for bugs and questions. Include the output of `frogjs -p process.versions`, your platform, and the smallest script that shows the problem.
