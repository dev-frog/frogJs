#!/bin/bash
# Native memory leaks at exit, from macOS's leaks tool. Exits with code 1 if a
# script leaks. Usage: tools/leakcheck.sh [path/to/frogjs]
FROGJS="${1:-build/frogjs}"
if ! command -v leaks > /dev/null; then
    echo "leakcheck needs the macOS leaks tool: on Linux use valgrind or LeakSanitizer" >&2
    exit 1
fi
TMP_DIR="$(mktemp -d)"
trap 'rm -r "$TMP_DIR"' EXIT

cat > "$TMP_DIR/workload.js" <<'JS'
const fs = require('fs');
const net = require('net');
const http = require('http');
const crypto = require('crypto');
const { Worker } = require('worker_threads');
const { execSync, spawn } = require('child_process');
const path = require('path');

async function main() {
  // Buffers, hashes
  for (let i = 0; i < 200; i++) Buffer.alloc(4096).slice(1).toString('hex');
  for (let i = 0; i < 200; i++) crypto.createHash('sha256').update('x').digest('hex');
  crypto.randomBytes(16);
  await new Promise((resolve) => crypto.pbkdf2('a', 'b', 10, 16, 'sha256', resolve));

  // Files
  const file = path.join(process.env.WORKDIR, 'file');
  await fs.promises.writeFile(file, 'hello');
  await fs.promises.readFile(file);
  fs.readFileSync(file);
  fs.statSync(file);
  await new Promise((resolve) => {
    const s = fs.createReadStream(file);
    s.on('data', () => {});
    s.on('close', resolve);
  });
  try { fs.readFileSync(file + '.missing'); } catch { /* the error path */ }

  // Timers
  await new Promise((resolve) => setTimeout(resolve, 1));
  clearInterval(setInterval(() => {}, 1000));
  setTimeout(() => {}, 100000).unref();
  await new Promise((resolve) => setImmediate(resolve));

  // Sockets, HTTP
  const server = http.createServer((req, res) => res.end('ok'));
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  await new Promise((resolve) => http.get({ port: server.address().port, host: '127.0.0.1' }, (res) => {
    res.resume();
    res.on('end', resolve);
  }));
  await new Promise((resolve) => server.close(resolve));
  const unrefed = net.createServer();
  unrefed.listen(0, '127.0.0.1');
  unrefed.unref();

  // Child processes, workers
  execSync('true');
  await new Promise((resolve) => spawn('true').on('close', resolve));
  await new Promise((resolve) => {
    const worker = new Worker('require("worker_threads").parentPort.postMessage(1); setTimeout(() => {}, 100000)', { eval: true });
    worker.on('message', () => worker.terminate());
    worker.on('exit', resolve);
  });
  structuredClone({ a: [1, 2, 3] });
  process.on('SIGUSR2', () => {});
  Promise.reject(new Error('handled')).catch(() => {});
}

main();
JS

status=0
export WORKDIR="$TMP_DIR"
for script in "$TMP_DIR/workload.js"; do
    output="$(MallocStackLogging=1 leaks --atExit -- "$FROGJS" "$script" 2>&1)"
    code=$?
    summary="$(echo "$output" | grep -E 'leaks? for [0-9]+ total leaked bytes' | tail -1)"
    echo "$(basename "$script"): ${summary:-no summary}"
    if [ "$code" -ne 0 ] || [ -z "$summary" ]; then
        echo "$output" | grep -v -E '^ +0x[0-9a-f]+ - ' | tail -60
        status=1
    fi
done
exit $status
