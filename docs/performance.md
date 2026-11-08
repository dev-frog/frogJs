# Performance

`make bench` runs each benchmark in a fresh process, a few times, and keeps the best time. The ratio is FrogJS's time divided by Node.js's, so below 1 means FrogJS is faster.

**Goal:** within 2x of Node.js for equivalent operations. **Result: 19 of 23 benchmarks meet it**, and 4 don't.

Measured 2026-11-09 on one Apple Silicon Mac: FrogJS 1.0.0 with V8 13.5.212.10 and libuv 1.52.1; Node.js v24.3.0. These are small benchmarks from one machine with no controlled load, so treat differences of a few tenths as noise. V8 versions differ too, which also matters for the JavaScript-heavy ones.

| Benchmark | FrogJS (ms) | Node.js (ms) | Ratio | What it does |
|-----------|-------------|--------------|-------|--------------|
| `startup` | 14.8 | 20.3 | 0.73 | Process start to exit of an empty script |
| `js-loop` | 145.4 | 145.8 | 1.00 | Plain JavaScript, the same V8 work: the control |
| `buffer-alloc` | 5.9 | 9.0 | 0.66 | `Buffer.alloc(64)` |
| `buffer-from-string` | 20.2 | 14.9 | 1.36 | `Buffer.from(string)` |
| `buffer-tostring` | 19.1 | 17.4 | 1.10 | `buf.toString()` |
| `buffer-hex-base64` | 62.1 | 27.5 | **2.25** | Hex and base64, both ways, 1 KB |
| `buffer-concat` | 42.7 | 45.8 | 0.93 | `Buffer.concat()` of 4 KB |
| `buffer-read-write` | 9.6 | 15.3 | 0.63 | `readUInt32LE`, `writeUInt32LE` |
| `events-emit` | 74.9 | 86.0 | 0.87 | `emit()` to two listeners |
| `util-inspect` | 114.6 | 95.7 | 1.20 | `util.inspect()` of a nested object |
| `path-ops` | 174.9 | 108.3 | 1.61 | `join`, `basename`, `resolve` |
| `sha256-small` | 98.1 | 34.0 | **2.88** | New hash, 5 bytes, hex digest |
| `sha256-large` | 45.4 | 22.7 | 2.00 | 64 MB through SHA-256 |
| `pbkdf2` | 58.9 | 31.8 | 1.85 | PBKDF2-SHA256, 200,000 iterations |
| `fs-sync` | 924.5 | 963.8 | 0.96 | Write, read, stat, sync |
| `fs-async` | 576.6 | 937.3 | 0.62 | Write, read, `fs.promises` |
| `fs-stream` | 84.4 | 75.8 | 1.11 | Read 256 MB through `createReadStream` |
| `stream-pipe` | 49.5 | 17.6 | **2.82** | Readable to Transform to Writable, 200,000 chunks |
| `timers` | 90.6 | 43.9 | **2.07** | 200,000 `setTimeout`s |
| `tcp-throughput` | 144.1 | 152.4 | 0.95 | 1 GB over loopback |
| `tcp-connections` | 102.6 | 127.7 | 0.80 | 3,000 connections |
| `http-requests` | 174.7 | 98.0 | 1.78 | 3,000 requests, 10 at a time |
| `http-body` | 150.3 | 93.3 | 1.61 | 100 POSTs of 1 MB |

The benchmarks are in `benchmark/bench.js`, and the same file runs under both runtimes.

## Over the goal

`stream-pipe` and `sha256-small` are clearly over, at about 2.9x, and `buffer-hex-base64` at about 2.2x. `timers` and `sha256-large` sit right at 2x and land on either side of it from one run to the next, so count them as borderline.

| Benchmark | Why | What could help |
|-----------|-----|-----------------|
| `stream-pipe` | The stream classes are a JavaScript port that keeps more state per object than Node.js's bit-field version | Profile, and move the state to bit fields |
| `sha256-small` | Each hash constructs a full `Transform` stream and a native object. Node.js creates its stream state lazily | Do the same. Large hashes aren't affected by this |
| `buffer-hex-base64` | Node.js uses SIMD through simdutf; FrogJS has scalar lookup tables | SIMD |
| `timers` | One libuv timer per timeout. Node.js keeps one per distinct delay, with a list of timeouts on each | The same lists. Only a burst of many timers notices |
| `sha256-large` | Hardware instructions are used on arm64, as in OpenSSL, but FrogJS has a virtual call per 64-byte block, and OpenSSL's loop takes several blocks per call | Pass whole runs of blocks to the compress function |

## What was done for 1.0

Compared with the 0.9 code, measured while working on it:

| Change | Effect |
|--------|--------|
| `-O2` for the whole build | Was unoptimized, except for hashes |
| Recycling ArrayBuffer allocator | `Buffer.concat` from 73x slower than Node.js to about even, TCP from 10x to faster than Node.js. V8's allocator is slow when V8 is built with its sandbox, as Homebrew's is |
| `uv_try_write` before queuing a copy | Part of the TCP result |
| Buffer pool for small allocations, direct UTF-8 writes | `Buffer.from(string)` from 11x to about 1.4x |
| Integer reads and writes on the bytes, no `DataView` | From 9x to faster than Node.js |
| One microtask per burst of stream ticks | `stream-pipe` from 4.5x to 2.9x |
| ARMv8 SHA-2 instructions | SHA-256 from 8x to 2.2x, PBKDF2 from 3.4x to 1.8x |
| Lookup tables and direct decoding for hex and base64 | From 9x to 2.2x |
| `path` in JavaScript | From 3x to 1.7x: a native call each time cost more than the work |

## Without the sandboxed allocator

Homebrew's V8 is built with its sandbox, which is why allocation mattered so much. Another build of V8 would have different numbers: that hasn't been measured.
