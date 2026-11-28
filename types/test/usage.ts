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
import url = require('url');
import querystring = require('querystring');
import { StringDecoder } from 'string_decoder';
import assert = require('assert');
import strictAssert = require('assert/strict');
import tty = require('tty');
import readline = require('readline');
import readlinePromises = require('readline/promises');
import timers = require('timers');
import timersPromises = require('timers/promises');
import fsPromises = require('fs/promises');
import streamPromises = require('stream/promises');
import posixPath = require('path/posix');
import { performance as perfHooksPerformance } from 'perf_hooks';
import { Buffer as BufferFromModule, atob as atobFromModule } from 'buffer';
import processFromModule = require('process');
import { builtinModules, createRequire, isBuiltin } from 'module';

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
    const timer: FrogJS.Timeout = setTimeout((a: number) => a, 1, 1);
    clearTimeout(timer.unref());
    clearImmediate(setImmediate(() => {}));
    check('process is an EventEmitter', process.on('exit', () => {}) === process && typeof process.nextTick === 'function');
    check('new globals', performance.now() > 0 && structuredClone([1])[0] === 1 && new TextDecoder().decode(new TextEncoder().encode('é')) === 'é' && global === globalThis);
    await new Promise<void>((resolve) => queueMicrotask(resolve));
    check('__filename', path.isAbsolute(__filename) && __dirname === path.dirname(__filename));
    check('module globals', globalThis.fs === fs && child_process.spawn === spawn);

    // Buffer
    const buf: Buffer = Buffer.from('frog');
    check('Buffer', buf.length === 4 && buf[0] === 102 && buf.readUInt8(1) === 114 && buf instanceof Uint8Array && buf.toString('hex') === '66726f67' && Buffer.byteLength('é') === 2 &&
        Buffer.isBuffer(Buffer.concat([buf, Buffer.alloc(1)])) && buf.slice(1, 2).toString() === 'r' &&
        Buffer.from(new Uint8Array([104, 105])).toString() === 'hi' && Buffer.from([1, 2]).length === 2);

    // fs: Error objects, Buffers unless an encoding is given, Stats methods
    const file = path.join(os.tmpdir(), `frogjs-types-${process.pid}.txt`);
    fs.writeFileSync(file, 'ribbit');
    const bytes: Buffer = fs.readFileSync(file);
    const text: string = fs.readFileSync(file, 'utf8');
    const stats: fs.Stats = fs.statSync(file);
    check('fs sync', Buffer.isBuffer(bytes) && text === 'ribbit' && stats.isFile() && stats.size === 6 && stats.mtime instanceof Date);
    await new Promise<void>((resolve) => fs.readFile(file, 'utf8', (err, data) => {
        check('fs.readFile', err === null && data === 'ribbit');
        resolve();
    }));
    await new Promise<void>((resolve) => fs.readFile('/no/such/file', (err) => {
        check('fs error', err instanceof Error && err.code === 'ENOENT' && err.syscall === 'open');
        resolve();
    }));
    try {
        fs.readFileSync('/no/such/file');
    } catch (err) {
        check('fs sync error', (err as fs.FsError).code === 'ENOENT');
    }
    check('fs.promises', (await fs.promises.readFile(file, 'utf8')) === 'ribbit');
    await new Promise<void>((resolve, reject) => {
        const out = fs.createWriteStream(file + '.copy');
        fs.createReadStream(file).pipe(out);
        out.on('close', resolve);
        out.on('error', reject);
    });
    check('fs streams', fs.readFileSync(file + '.copy', 'utf8') === 'ribbit');
    fs.rmSync(file + '.copy');
    const names: string[] = fs.readdirSync(os.tmpdir());
    check('fs.readdirSync', names.includes(path.basename(file)) && fs.existsSync(file));
    fs.unlinkSync(file);

    // path, os, util
    check('path', path.join('a', 'b') === 'a/b' && path.extname('x.ts') === '.ts' && path.basename('/a/b.js', '.js') === 'b' && path.sep === '/' &&
        path.relative('/a/b', '/a/c') === '../c' && path.parse('/a/b.c').name === 'b' && path.format({ dir: '/a', base: 'b' }) === '/a/b' && path.posix.sep === '/');
    const cpu: os.CpuInfo | undefined = os.cpus()[0];
    check('os', os.EOL === '\n' && typeof os.totalmem() === 'number' && (cpu === undefined || typeof cpu.times.idle === 'number'));
    check('util', util.format('%s=%d', 'x', 1) === 'x=1' && util.inspect({ a: [1] }, { depth: 0 }) === '{ a: [Array] }' &&
        util.isDeepStrictEqual([1], [1]) && typeof util.inspect.custom === 'symbol');
    const readFileAsync = util.promisify(fs.readFile);
    check('util.promisify', String(await readFileAsync(__filename)).length > 0);

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

    const echo = net.createServer((socket) => socket.on('data', (data) => socket.end(String(data).toUpperCase())));
    await new Promise<void>((resolve) => echo.listen(0, '127.0.0.1', resolve));
    const reply = await new Promise<string>((resolve) => {
        const socket = net.connect(echo.address().port, '127.0.0.1', () => socket.write('ping'));
        socket.setEncoding('utf8');
        socket.on('data', (data) => resolve(data));
    });
    check('net', reply === 'PING' && net.isIP('::1') === 6);
    echo.close();

    // 1.1: web globals
    const parsed = new URL('/a b?x=1#h', 'https://user@example.com:8080');
    check('URL', parsed.href === 'https://user@example.com:8080/a%20b?x=1#h' && parsed.searchParams.get('x') === '1' && URL.canParse('nope') === false);
    const params = new URLSearchParams([['a', '1'], ['b', '2']]);
    params.append('a', '4');
    check('URLSearchParams', params.getAll('a').join() === '1,4' && params.size === 3 && new URLSearchParams({ c: 'd' }).toString() === 'c=d');
    const target = new EventTarget();
    let seen: unknown;
    target.addEventListener('ping', (event) => { seen = (event as CustomEvent<number>).detail; }, { once: true });
    check('EventTarget', target.dispatchEvent(new CustomEvent('ping', { detail: 7 })) && seen === 7);
    const controller = new AbortController();
    controller.abort('why');
    check('AbortController', controller.signal.aborted && controller.signal.reason === 'why' && AbortSignal.abort().reason instanceof DOMException);
    check('DOMException', new DOMException('m', 'AbortError').code === DOMException.ABORT_ERR && atob(btoa('frog')) === 'frog');

    // 1.1: modules
    check('url module', url.URL === URL && url.fileURLToPath(url.pathToFileURL('/tmp/a b')) === '/tmp/a b' &&
        url.parse('http://a.com/p?q=1', true).query.q === '1' && url.resolve('http://a.com/b/c', '../d') === 'http://a.com/d' &&
        url.format({ protocol: 'http', host: 'a.com', pathname: '/x' }) === 'http://a.com/x' && url.domainToASCII('münchen.de') === 'xn--mnchen-3ya.de');
    check('querystring', querystring.parse('a=1&a=2&b=3').a?.length === 2 && querystring.stringify({ a: [1, 2], b: 'c d' }) === 'a=1&a=2&b=c%20d');
    const decoder = new StringDecoder('utf8');
    check('string_decoder', decoder.write(Buffer.from([0xc3])) === '' && decoder.end(Buffer.from([0xa9])) === 'é');
    assert(true);
    assert.strictEqual(1, 1);
    assert.deepStrictEqual({ a: [1] }, { a: [1] });
    strictAssert.equal('1', '1');
    assert.throws(() => { throw new TypeError('bad'); }, TypeError);
    await assert.rejects(Promise.reject(new Error('no')), /no/);
    let assertionError: unknown;
    try { strictAssert.deepEqual([1], [2]); } catch (err) { assertionError = err; }
    check('assert', assertionError instanceof assert.AssertionError && (assertionError as assert.AssertionError).code === 'ERR_ASSERTION' && assert.strict === strictAssert);
    check('tty', typeof tty.isatty(1) === 'boolean' && typeof tty.WriteStream === 'function');
    const lines: string[] = [];
    const rl = readline.createInterface({ input: Readable.from(['one\ntwo\n']), crlfDelay: Infinity });
    for await (const line of rl) lines.push(line);
    check('readline', lines.join() === 'one,two' && readline.promises === readlinePromises);
    const ask = readlinePromises.createInterface({ input: Readable.from(['yes\n']), output: new PassThrough(), terminal: false });
    check('readline/promises', (await ask.question('? ')) === 'yes');
    ask.close();
    check('timers', timers.setTimeout === setTimeout && timers.promises === timersPromises);
    check('timers/promises', (await timersPromises.setTimeout(1, 'v')) === 'v' && (await timersPromises.setImmediate(2)) === 2);
    await timersPromises.scheduler.wait(1);
    const aborted = new AbortController();
    const waiting = timersPromises.setTimeout(10000, undefined, { signal: aborted.signal });
    aborted.abort();
    check('timers/promises with a signal', await waiting.then(() => false, (err: Error) => err.name === 'AbortError'));
    check('aliases', fsPromises === fs.promises && streamPromises === stream.promises && posixPath === path &&
        perfHooksPerformance === performance && BufferFromModule === Buffer && atobFromModule === atob && processFromModule === process);
    check('module', builtinModules.includes('url') && isBuiltin('node:fs') && typeof createRequire(__filename)('path').join === 'function');
    check('process.versions', process.versions.node.length > 0 && process.version === 'v' + process.versions.node && process.versions.frogjs.length > 0);

    // 1.1: additions to util, crypto, events and fs
    const { values, positionals } = util.parseArgs({ args: ['-v', 'file'], options: { verbose: { type: 'boolean', short: 'v' } }, allowPositionals: true });
    check('util.parseArgs', values.verbose === true && positionals[0] === 'file');
    check('util.types', util.types.isPromise(Promise.resolve()) && util.types.isDate(new Date()) && !util.types.isRegExp({}));
    check('util.styleText', util.styleText('red', 'x', { validateStream: false }) === '\u001b[31mx\u001b[39m');
    const callbackified = util.callbackify(async (n: number) => n * 2);
    check('util.callbackify', await new Promise<boolean>((resolve) => callbackified(4, (err, value) => resolve(err === null && value === 8))));
    check('util.getSystemErrorName', util.getSystemErrorName(-2) === 'ENOENT' && util.debuglog('frogjs-types-test').enabled === false);
    check('crypto additions', crypto.randomFillSync(new Uint8Array(8)).length === 8 && crypto.webcrypto.randomUUID().length === 36);
    const signalled = new AbortController();
    const onceAborted = EventEmitter.once(new EventEmitter(), 'never', { signal: signalled.signal });
    signalled.abort();
    check('events.once with a signal', await onceAborted.then(() => false, (err: Error) => err.name === 'AbortError'));
    check('fs with a signal', (await fs.promises.readFile(__filename, { encoding: 'utf8', signal: new AbortController().signal })).length > 0);

    console.log(`types usage: ${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
