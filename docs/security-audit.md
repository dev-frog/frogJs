# Security Audit: FrogJS 1.0.0

**Date:** 2026-11-09. **Type:** internal review of the source, with testing and tools.

> **This isn't an independent audit.** It was done as part of building the release, by the same effort that wrote much of the code it reviews, so it shares that work's blind spots. For a deployment where a vulnerability would be costly, get an independent review. Absence of findings here isn't evidence of absence.

## Scope

The [threat model](../SECURITY.md#threat-model) is trusted code on untrusted data.

| Area | How it was reviewed |
|------|---------------------|
| HTTP parser and server | Read in full; hostile input tests; fuzzed |
| `Buffer`, encodings | Rewritten; read; tests against Node.js; fuzzed |
| `fs`, `net`, timers, process, module loader | Rewritten; read; tests against Node.js |
| `crypto` native boundary | Read: arguments, memory, randomness, comparison. **The hash algorithms themselves were only tested**, against Node.js/OpenSSL output, not reviewed for side channels |
| `child_process` | Argument handling read; tested |
| `worker_threads` | Lifetimes and shutdown path read; tested under sanitizers. **Not a full review of its locking**, and ThreadSanitizer wasn't run |
| REPL, line editor | History file handling read; tested on a terminal |
| V8, libuv | Out of scope |

## Tools

| Tool | Result |
|------|--------|
| AddressSanitizer + UndefinedBehaviorSanitizer on all tests (`make test-asan`) | Clean |
| Fuzzing on the sanitizer build (`make fuzz`): about 30,000 connections with mutated HTTP, about 18,000 rounds of random Buffer calls | No crash |
| macOS `leaks` at exit (`make leakcheck`) | 0 leaks |
| `-Wall -Wextra -Werror`, ESLint | Clean |
| Parser limits and smuggling tests (`test-http-limits.js`) | Pass |

## Not done

ThreadSanitizer; coverage-guided fuzzing (the fuzzer is mutation-based and doesn't use coverage feedback, and is much shallower than libFuzzer or AFL would be); static analysis such as clang-tidy or CodeQL; any testing on Linux; review of timing side channels in hashes; an independent review.

## Findings

All of these were fixed in 1.0.0.

### High

| # | Finding | Impact | Fix |
|---|---------|--------|-----|
| 1 | The HTTP server buffered request bodies without limit if the handler didn't read them | A client could exhaust server memory | The socket pauses when the body's buffer is full (`IncomingMessage._read` resumes it) |
| 2 | The HTTP server queued pipelined requests without limit while answering the first | The same | Reading pauses once the queue reaches `maxHeaderSize` |
| 3 | `net` sockets had no backpressure: `write()` queued without limit | A slow client could exhaust the memory of a server sending to it | Sockets became Duplex streams; `write()` returns `false`, and HTTP passes that on |
| 4 | `Buffer.alloc(-1)`, `new Buffer(-5)` and `Buffer.alloc(1e18)` threw an uncaught C++ exception | Size from input aborted the process | Validated (`ERR_OUT_OF_RANGE`), and a failed allocation is a `RangeError` |
| 5 | Buffer methods called on another object (`buf.toString.call({})`) read internal fields that don't exist | Memory error | Buffer is a `Uint8Array` subclass; the receiver is checked |
| 6 | A `SharedArrayBuffer` allocated in a worker was freed through that worker's allocator, already deleted when the worker had exited. Found by reading the code; no crash was reproduced | Use after free | One allocator per process, never destroyed |

### Medium

| # | Finding | Impact | Fix |
|---|---------|--------|-----|
| 7 | Null bytes in `spawn` arguments, `cwd` and environment were silently truncated at the C boundary | A checked value isn't the one used | `ERR_INVALID_ARG_VALUE` |
| 8 | The same for `net.connect` hosts (`evil.example\0.good.example`) | Bypass of host allowlists | Rejected |
| 9 | The same for file paths in `fs` | Bypass of extension checks | Rejected |
| 10 | No time limit on a request body: a trickle held a connection forever | Connection exhaustion | `server.requestTimeout`, 300 s by default |
| 11 | `writeHead()`'s status message allowed control characters other than CR and LF | Malformed responses | Same rule as header values |
| 12 | Control characters and NUL were accepted in request URLs | Confuses code and proxies behind FrogJS | `400` |
| 13 | `Transfer-Encoding` was accepted on HTTP/1.0 requests | Request smuggling surface behind a proxy | `400` |
| 14 | `setTimeout(fn, -1)` became a delay of about 584 million years, and `fs.writeFile` leaked its descriptor on every call | A hang from input; descriptor exhaustion | Delays normalized; the operation closes what it opens |
| 15 | Uncaught errors in callbacks were only printed, and unhandled rejections ignored entirely | A program carries on in an unknown state, and a failed security check inside a promise goes unnoticed | They end the process unless a listener takes them, as in Node.js |
| 16 | A string longer than V8's limit aborted the process (`ToLocalChecked`) in `toString()` | Large input aborted the process | `ERR_STRING_TOO_LONG` |

### Low

| # | Finding | Impact | Fix |
|---|---------|--------|-----|
| 17 | The REPL history file was briefly created with default permissions before being set private | History can contain secrets | Created as `0600` |

## Reviewed, no change needed

- **Smuggling basics:** `Content-Length` with `Transfer-Encoding`, differing or malformed lengths, `Transfer-Encoding` not ending in `chunked`, bare CR and LF, spaces before the colon, obsolete folding: all rejected. Tests now pin this.
- **Prototype pollution through headers:** header objects have a null prototype.
- **Header size:** limited to 16 KB, including the request line and the number of headers.
- **Response splitting:** `setHeader` and client request paths, methods and headers validate their input.
- **Randomness:** `uv_random`, the operating system's CSPRNG. `timingSafeEqual` doesn't exit early.
- **`SIGPIPE`:** sockets and pipes get `EPIPE` rather than the signal.
- **Uninitialized memory:** `allocUnsafe` is zero-filled, unlike Node.js's. The native path that skips zeroing fills the block at once.

## Accepted risks

- **Secrets aren't wiped from memory.** Keys and passwords stay in JavaScript strings and freed blocks until overwritten. JavaScript gives no way to prevent copies.
- **A handler that throws gets the client a `500`**, and the server goes on, unlike the fatal default elsewhere. This is deliberate and documented, but state shared with that handler may be inconsistent.
- **MD5 and SHA-1 are available**, for compatibility.
- **HTTP handlers decide about body sizes.** The server holds back what isn't read, but has no maximum body size of its own.
- **No TLS**: traffic is in the clear unless a proxy terminates it.
- **Hash implementations are original code.** They match Node.js/OpenSSL on test vectors, but haven't had the scrutiny OpenSSL has. Software paths may not be constant-time; that matters for HMAC keys in rare settings.

## Recommendations

1. Get an independent review before relying on FrogJS for anything high-value.
2. Add ThreadSanitizer for `worker_threads`, and coverage-guided fuzzing for the HTTP parser.
3. Run the sanitizers and leak checks on Linux, if it becomes supported.
