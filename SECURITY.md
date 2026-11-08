# Security Policy

## Reporting a vulnerability

Please don't open a public issue. Use [GitHub's private vulnerability reporting](https://github.com/dev-frog/frogJs/security/advisories/new) and include the FrogJS version (`frogjs -p process.versions`), your platform, and a script or input that shows the problem.

The goal is an answer within 7 days and a fix for a serious issue within 30 days, on the [supported versions](docs/support-policy.md). You'll be credited in the advisory unless you prefer otherwise.

## Threat model

**FrogJS runs trusted code on untrusted data.** It works like Node.js: a script has the full rights of its user, with no sandbox and no permission model.

**In scope.** Input a program receives must not cause memory errors, code execution or crashes it can't handle:

- Network input: HTTP requests and responses, TCP data
- Data passed to APIs: strings, Buffers, paths, encodings, arguments of any type
- Files read by a program

Also in scope: a server exhausting memory because of what a client sends when the program behaves reasonably, and APIs that make injection easy (HTTP header injection, null bytes truncating a path or an argument).

**Out of scope.**

- Malicious JavaScript: a script can read your files and start processes by design. Untrusted code doesn't belong in FrogJS, and `worker_threads` is not a security boundary
- Malicious packages in `node_modules`: the same
- Bugs in V8 or libuv, which belong to those projects (report those too, so a minimum version can be raised)
- Memory exhaustion that a program causes itself, or resources a server application doesn't limit, such as buffering a whole request body it chose to read, or an unlimited number of connections
- Hashes that are weak by design: MD5 and SHA-1 exist for compatibility

## What protects you

- **Memory safety**: Buffers are `Uint8Array`s, so V8 checks bounds. CI runs every test under UndefinedBehaviorSanitizer and fuzzes the HTTP parser and Buffer. AddressSanitizer can't run against Homebrew's current V8, so it was run with V8 13.5 for this release and isn't part of CI (see the [security review](docs/security-audit.md#tools)).
- **HTTP**: strict parsing against request smuggling, header injection checks, size limits, timeouts and backpressure. `test-http-limits.js` tests each with hostile input.
- **Crypto**: random bytes come from the operating system; `timingSafeEqual` runs in constant time for a given length. Hashes are checked against Node.js. FrogJS has no ciphers or TLS: terminate TLS in a proxy.

## Audit

The review done for 1.0.0, what it covered and what it didn't, is in [docs/security-audit.md](docs/security-audit.md). It was an internal review, not an independent audit.

## Running a server

- Put a reverse proxy in front for TLS, and for limits on connections and request rates.
- Limit request bodies yourself: FrogJS holds back a body nobody reads, but a handler that collects one into memory decides how much that is.
- Set `server.headersTimeout`, `server.requestTimeout` and `server.keepAliveTimeout` to suit your traffic. `server.maxConnections` limits connections.
- Handle `'error'` on every socket and stream. An unhandled `'error'` event ends the process, which is correct, but better chosen than discovered.
- Don't build `child_process.exec()` commands from input. Use `execFile` or `spawn` with an argument list.
- Keep V8 and libuv updated.
