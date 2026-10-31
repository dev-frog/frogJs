// Uses every part of the FrogJS declarations. It must type-check under
// --strict, and it is also compiled and run by FrogJS (make check-types), so
// what the types promise is checked against what the runtime does.
import fs = require('fs');
import path = require('path');
import os = require('os');
import util = require('util');
import crypto = require('crypto');
import EventEmitter = require('events');
import http = require('http');
import net = require('net');
import { Readable, Writable, Transform, PassThrough, pipeline, finished } from 'stream';
import * as stream from 'stream';
import { spawn, exec, execSync, execFileSync, spawnSync, ChildProcess } from 'child_process';
import { Worker, MessageChannel, receiveMessageOnPort, isMainThread } from 'worker_threads';

let passed = 0;
let failed = 0;
function check(name: string, ok: boolean): void {
    if (ok) {
        passed++;
    } else {
        failed++;
        console.error(`  ✗ ${name}`);
    }
}

async function main(): Promise<void> {
    // Globals
    const argv: string[] = process.argv;
    const home: string | undefined = process.env.HOME;
    check('process', argv.length > 0 && typeof home === 'string' && process.versions.v8.length > 0 && process.cwd() === path.resolve());
    check('process.stdout', process.stdout.fd === 1 && typeof process.stdout.isTTY === 'boolean' && process.stdout.write(''));
    const timer: number = setTimeout(() => {}, 1);
    clearTimeout(timer);
    check('__filename', path.isAbsolute(__filename) && __dirname === path.dirname(__filename));
    check('module globals', globalThis.fs === fs && child_process.spawn === spawn);

    // Buffer
    const buf: Buffer = Buffer.from('frog');
    check('Buffer', buf.length === 4 && buf.toString('hex') === '66726f67' && Buffer.byteLength('é') === 2 &&
        Buffer.isBuffer(Buffer.concat([buf, Buffer.alloc(1)])) && buf.slice(1, 2).toString() === 'r' &&
        Buffer.from(new Uint8Array([104, 105])).toString() === 'hi' && Buffer.from([1, 2]).length === 2);

    // fs: string errors, string contents, boolean isFile
    const file = path.join(os.tmpdir(), `frogjs-types-${process.pid}.txt`);
    fs.writeFileSync(file, 'ribbit');
    const text: string = fs.readFileSync(file);
    const stats: fs.Stats = fs.statSync(file);
    check('fs sync', text === 'ribbit' && stats.isFile === true && stats.size === 6 && stats.mtime instanceof Date);
    await new Promise<void>((resolve) => fs.readFile(file, (err, data) => {
        check('fs.readFile', err === null && data === 'ribbit');
        resolve();
    }));
    await new Promise<void>((resolve) => fs.readFile('/no/such/file', (err) => {
        check('fs error is a string', typeof err === 'string');
        resolve();
    }));
    try {
        fs.readFileSync('/no/such/file');
    } catch (err) {
        check('fs sync error is a string', typeof err === 'string');
    }
    const names: string[] = fs.readdirSync(os.tmpdir());
    check('fs.readdirSync', names.includes(path.basename(file)) && fs.existsSync(file));
    fs.unlinkSync(file);

    // path, os, util
    check('path', path.join('a', 'b') === 'a/b' && path.extname('x.ts') === '.ts' && path.basename('/a/b.js', '.js') === 'b' && path.sep === '/');
    const cpu: os.CpuInfo | undefined = os.cpus()[0];
    check('os', os.EOL === '\n' && typeof os.totalmem() === 'number' && (cpu === undefined || typeof cpu.times.idle === 'number'));
    check('util', util.format('%s=%d', 'x', 1) === 'x=1' && util.inspect({ a: [1] }, { depth: 0 }) === '{ a: [Array] }' &&
        util.isDeepStrictEqual([1], [1]) && typeof util.inspect.custom === 'symbol');
    const readFileAsync = util.promisify(fs.readFile);
    check('util.promisify', (await readFileAsync(__filename)).length > 0);

    // events
    class Frog extends EventEmitter {}
    const frog = new Frog();
    let croaks = 0;
    frog.on('croak', (n: number) => { croaks += n; }).once('croak', () => {});
    frog.emit('croak', 2);
    check('events', croaks === 2 && frog.listenerCount('croak') === 1);
    setTimeout(() => frog.emit('jump', 'high'), 1);
    const [height] = await EventEmitter.once(frog, 'jump');
    check('events.once', height === 'high');

    // stream
    const upper = new Transform({
        transform(chunk, _encoding, callback) {
            callback(null, String(chunk).toUpperCase());
        },
    });
    let out = '';
    const sink = new Writable({
        write(chunk, _encoding, callback) {
            out += chunk;
            callback();
        },
    });
    await new Promise<void>((resolve, reject) => {
        pipeline(Readable.from(['a', 'b']), upper, sink, (err) => (err ? reject(err) : resolve()));
    });
    check('stream.pipeline', out === 'AB');
    const chunks: string[] = [];
    for await (const chunk of Readable.from(['x', 'y'])) chunks.push(chunk);
    check('async iteration', chunks.join('') === 'xy');
    const pass = new PassThrough();
    finished(pass, (err) => check('stream.finished', !err));
    pass.resume();
    pass.end('done');
    await stream.promises.finished(pass);

    // crypto
    check('crypto.createHash', crypto.createHash('sha256').update('hello').digest('hex').startsWith('2cf24dba'));
    const digest: Buffer = crypto.createHmac('sha1', 'key').update(buf).digest();
    check('crypto.createHmac', digest.length === 20);
    check('crypto random', crypto.randomBytes(8).length === 8 && crypto.randomUUID().length === 36 &&
        crypto.randomInt(3) < 3 && crypto.getRandomValues(new Uint8Array(4)).length === 4);
    const key = await new Promise<Buffer>((resolve, reject) =>
        crypto.pbkdf2('pw', 'salt', 1, 16, 'sha256', (err, derived) => (err ? reject(err) : resolve(derived))));
    check('crypto.pbkdf2', key.length === 16 && crypto.timingSafeEqual(key, crypto.pbkdf2Sync('pw', 'salt', 1, 16, 'sha256')));

    // child_process
    const child = spawn('sh', ['-c', 'echo hi']);
    let childOut = '';
    child.stdout.on('data', (data) => { childOut += data; });
    const [code] = await new Promise<[number | null, string | null]>((resolve) => child.on('close', (c, s) => resolve([c, s])));
    check('child_process.spawn', childOut === 'hi\n' && code === 0 && child instanceof ChildProcess);
    const version: string = execSync('echo 1', { encoding: 'utf8' });
    const raw: Buffer = execFileSync('echo', ['2']);
    check('execSync', version === '1\n' && raw.toString() === '2\n' && spawnSync('true').status === 0);
    const { stdout } = await util.promisify(exec)('echo promised');
    check('promisify(exec)', stdout === 'promised\n');
    exec('exit 3', (err) => check('exec error', err !== null && err.code === 3));

    // worker_threads
    const worker = new Worker('require("worker_threads").parentPort.postMessage(21 * 2)', { eval: true });
    const answer = await new Promise<number>((resolve) => worker.once('message', resolve));
    const exitCode = await new Promise<number>((resolve) => worker.on('exit', resolve));
    check('Worker', isMainThread && answer === 42 && exitCode === 0);
    const { port1, port2 } = new MessageChannel();
    port1.postMessage({ shared: new SharedArrayBuffer(4) });
    const received = receiveMessageOnPort(port2);
    check('MessageChannel', received !== undefined && received.message.shared instanceof SharedArrayBuffer);
    port1.close();

    // http and net
    const server = http.createServer((req, res) => {
        res.setHeader('X-Frog', 'yes');
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end(`${req.method} ${req.url}`);
    });
    await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
    const address = server.address();
    const body = await new Promise<string>((resolve, reject) => {
        const req = http.get({ hostname: '127.0.0.1', port: address ? address.port : 0, path: '/pond' }, (res) => {
            let data = '';
            res.on('data', (chunk) => { data += chunk; });
            res.on('end', () => resolve(`${res.statusCode} ${res.headers['x-frog']} ${data}`));
        });
        req.on('error', reject);
    });
    check('http', body === '200 yes GET /pond');
    server.close();

    const echo = net.createServer((socket) => socket.on('data', (data) => socket.end(data.toUpperCase())));
    echo.listen(0, '127.0.0.1');
    const reply = await new Promise<string>((resolve) => {
        const socket = net.connect(echo.address().port, '127.0.0.1', () => socket.write('ping'));
        socket.on('data', (data) => resolve(data));
    });
    check('net', reply === 'PING');
    echo.close();

    console.log(`types usage: ${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
