// Long-running allocation must not grow the process: each section does work that
// used to leak or could leak, and compares memory before and after. Exits with
// code 1 if a check fails. The limits are a fraction of what each section
// allocates, so a real leak fails while allocator noise doesn't. They are meant
// for FrogJS: other runtimes have other noise.
const crypto = require('crypto');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');
const { Worker } = require('worker_threads');
const { check, section, sleep, run } = require('./lib/check');

const MB = 1024 * 1024;

async function settle() {
  // Give the GC a reason and a chance to run
  for (let i = 0; i < 8; i++) {
    if (global.gc) global.gc();
    else new Array(200000).fill(0).map((_, n) => ({ n }));
    await sleep(2);
  }
  return process.memoryUsage().rss;
}

// Warm up once (first use compiles code and grows pools), then measure the growth
// of `rounds` more runs. A real leak grows with the rounds; noise doesn't.
async function growth(name, limitMB, work) {
  await work();
  const before = await settle();
  await work();
  await work();
  await work();
  const after = await settle();
  const grown = (after - before) / MB;
  console.log(`       ${name}: ${grown.toFixed(1)} MB`);
  check(`${name} stays under ${limitMB} MB`, grown < limitMB, true);
}

run(async () => {
  section('Buffers');
  await growth('Buffer.alloc', 300, async () => {
    for (let i = 0; i < 500; i++) Buffer.alloc(MB);
  });
  await growth('Buffer.from', 150, async () => {
    const text = 'x'.repeat(MB);
    for (let i = 0; i < 200; i++) Buffer.from(text);
  });
  await growth('slices', 200, async () => {
    for (let i = 0; i < 300; i++) Buffer.alloc(MB).slice(1, 2);
  });
  await growth('concat', 150, async () => {
    const part = Buffer.alloc(MB / 2);
    for (let i = 0; i < 200; i++) Buffer.concat([part, part]);
  });

  section('A slice keeps its memory alive');
  const slices = [];
  for (let i = 0; i < 50; i++) slices.push(Buffer.alloc(1024, i).slice(0, 1));
  await settle();
  check('still readable after GC', slices.map((s) => s[0]), Array.from({ length: 50 }, (_, i) => i));

  section('Crypto');
  await growth('randomBytes', 200, async () => {
    for (let i = 0; i < 300; i++) crypto.randomBytes(MB);
  });
  await growth('hash objects', 60, async () => {
    for (let i = 0; i < 20000; i++) crypto.createHash('sha256').update('x').digest();
  });

  section('Timers');
  await growth('timers', 60, async () => {
    await new Promise((resolve) => {
      let left = 20000;
      for (let i = 0; i < 20000; i++) {
        const t = setTimeout(() => {
          if (--left === 0) resolve();
        }, 1);
        if (i % 2) {
          clearTimeout(t);
          left--;
        }
      }
    });
  });

  section('Files');
  const file = path.join(os.tmpdir(), `frogjs-memory-${process.pid}`);
  fs.writeFileSync(file, Buffer.alloc(MB));
  await growth('readFile', 200, async () => {
    await Promise.all(Array.from({ length: 200 }, () => fs.promises.readFile(file)));
  });
  await growth('readFileSync', 200, async () => {
    for (let i = 0; i < 200; i++) fs.readFileSync(file);
  });
  await growth('read streams', 100, async () => {
    for (let i = 0; i < 40; i++) {
      await new Promise((resolve, reject) => {
        const s = fs.createReadStream(file);
        s.on('data', () => {});
        s.on('close', resolve);
        s.on('error', reject);
      });
    }
  });
  const fdBefore = fs.openSync(file);
  fs.closeSync(fdBefore);
  await Promise.all(Array.from({ length: 500 }, () => fs.promises.writeFile(file, 'x')));
  const fdAfter = fs.openSync(file);
  fs.closeSync(fdAfter);
  check('no file descriptor leak', fdAfter, fdBefore);
  fs.unlinkSync(file);

  section('Sockets');
  const server = net.createServer((socket) => socket.pipe(socket));
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  await growth('connections', 100, async () => {
    for (let round = 0; round < 10; round++) {
      await Promise.all(Array.from({ length: 50 }, () => new Promise((resolve) => {
        const socket = net.connect(port, '127.0.0.1', () => socket.end(Buffer.alloc(16384)));
        socket.on('data', () => {});
        socket.on('close', resolve);
      })));
    }
  });
  server.close();

  section('Workers');
  await growth('workers', 150, async () => {
    for (let i = 0; i < 10; i++) {
      await new Promise((resolve, reject) => {
        const worker = new Worker('require("worker_threads").parentPort.postMessage(Buffer.alloc(1024))', { eval: true });
        worker.on('message', () => {});
        worker.on('error', reject);
        worker.on('exit', resolve);
      });
    }
  });
});
