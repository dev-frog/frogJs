// process and global objects self-test: exits with code 1 if a check fails.
// Also runs under Node.js.
const os = require('os');
const EventEmitter = require('events');
const { spawnSync } = require('child_process');
const { check, throws, section, once, run } = require('./lib/check');

const child = (code, options = {}) => spawnSync(process.execPath, ['-e', code], { encoding: 'utf8', timeout: 20000, ...options });

run(async () => {
  section('Information');
  check('argv', [Array.isArray(process.argv), typeof process.argv[0]], [true, 'string']);
  check('execArgv', Array.isArray(process.execArgv), true);
  check('pid', process.pid > 0, true);
  check('ppid', process.ppid > 0, true);
  check('platform', process.platform, os.platform());
  check('arch', process.arch, os.arch());
  check('version', /^v\d+\.\d+\.\d+/.test(process.version), true);
  check('versions', [typeof process.versions.v8, typeof process.versions.uv], ['string', 'string']);
  check('execPath', require('path').isAbsolute(process.execPath), true);
  check('title', typeof process.title, 'string');
  check('release', typeof process.release.name, 'string');
  check('env', typeof process.env, 'object');
  check('cwd', process.cwd(), require('path').resolve('.'));

  section('Time and memory');
  const time = process.hrtime();
  check('hrtime', [Array.isArray(time), time.length, Number.isInteger(time[0])], [true, 2, true]);
  const diff = process.hrtime(time);
  check('hrtime diff', diff[0] >= 0 && diff[1] >= 0, true);
  const crossing = process.hrtime([time[0], 999999999]);
  check('hrtime borrow', crossing[1] >= 0, true);
  check('hrtime.bigint', typeof process.hrtime.bigint(), 'bigint');
  throws('hrtime type', () => process.hrtime(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('hrtime length', () => process.hrtime([1]), { code: 'ERR_OUT_OF_RANGE' });
  check('uptime', process.uptime() > 0, true);
  const usage = process.memoryUsage();
  check('memoryUsage', [usage.rss > 0, usage.heapTotal > 0, usage.heapUsed > 0, typeof usage.external], [true, true, true, 'number']);
  check('memoryUsage.rss', process.memoryUsage.rss() > 0, true);
  const cpu = process.cpuUsage();
  check('cpuUsage', [typeof cpu.user, typeof cpu.system], ['number', 'number']);
  check('performance.now', performance.now() > 0, true);
  check('performance.timeOrigin', performance.timeOrigin > 0, true);
  check('performance.toJSON', typeof performance.toJSON(), 'object');

  section('Directory and umask');
  const start = process.cwd();
  process.chdir('/');
  check('chdir', process.cwd(), '/');
  process.chdir(start);
  throws('chdir to nothing', () => process.chdir('/no/such/dir'), { code: 'ENOENT' });
  throws('chdir type', () => process.chdir(1), { code: 'ERR_INVALID_ARG_TYPE' });
  const mask = process.umask();
  check('umask reads', typeof mask, 'number');
  check('umask sets', process.umask(0o22), mask);
  check('umask string', process.umask('22'), 0o22);
  process.umask(mask);

  section('EventEmitter');
  check('is an EventEmitter', process instanceof EventEmitter, true);
  check('warning event', await new Promise((resolve) => {
    process.once('warning', (w) => resolve([w.name, w.message, w.code]));
    process.emitWarning('careful', 'MyWarning', 'W01');
  }), ['MyWarning', 'careful', 'W01']);
  check('warning options', await new Promise((resolve) => {
    process.once('warning', (w) => resolve([w.name, w.code]));
    process.emitWarning('careful', { type: 'T', code: 'C' });
  }), ['T', 'C']);
  check('warning Error', await new Promise((resolve) => {
    process.once('warning', (w) => resolve(w.message));
    process.emitWarning(new Error('as error'));
  }), 'as error');
  check('warning printed', /careful/.test(child("process.emitWarning('careful')").stderr), true);

  section('Signals');
  check('listener runs', child("process.on('SIGUSR2', (name, n) => { console.log(name, typeof n); process.exit(0) }); setInterval(() => {}, 1000); process.kill(process.pid, 'SIGUSR2')").stdout, 'SIGUSR2 number\n');
  check('a listener alone keeps nothing alive', child("process.on('SIGUSR2', () => {})").status, 0);
  check('removing restores the default', child("const f = () => {}; process.on('SIGTERM', f); process.removeListener('SIGTERM', f); process.kill(process.pid, 'SIGTERM'); setTimeout(() => {}, 5000)").signal, 'SIGTERM');
  check('second listener', child("const a = () => console.log('a'), b = () => { console.log('b'); process.exit() }; process.on('SIGUSR1', a); process.on('SIGUSR1', b); process.removeListener('SIGUSR1', a); setInterval(() => {}, 1000); process.kill(process.pid, 'SIGUSR1')").stdout, 'b\n');
  check('kill(pid, 0) tests', process.kill(process.pid, 0), true);
  check('kill by number', process.kill(process.pid, 0), true);
  throws('kill bad pid type', () => process.kill('x'), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('kill unknown signal', () => process.kill(process.pid, 'SIGNOPE'), { code: 'ERR_UNKNOWN_SIGNAL' });
  throws('kill a missing process', () => process.kill(2 ** 30, 'SIGTERM'), { code: 'ESRCH' });
  check('default kill signal', child('process.kill(process.pid); setTimeout(() => {}, 5000)').signal, 'SIGTERM');

  section('stdio');
  check('stdout write', child("process.stdout.write('a'); process.stdout.write(Buffer.from('b')); process.stderr.write('')").stdout, 'ab');
  check('stderr write', child("process.stderr.write('e')").stderr, 'e');
  if (process.versions.frogjs) {
    // Node.js refuses to close stdout
    check('stdout end callback', child("process.stdout.end('x', () => console.log('done'))").stdout, 'xdone\n');
  }
  check('stdin data', child("let d = ''; process.stdin.on('data', (c) => d += c); process.stdin.on('end', () => console.log(d.toUpperCase()))", { input: 'piped' }).stdout, 'PIPED\n');
  check('stdin encoding', child("process.stdin.setEncoding('utf8'); process.stdin.on('data', (c) => console.log(typeof c))", { input: 'x' }).stdout, 'string\n');
  check('stdin pipe', child('process.stdin.pipe(process.stdout)', { input: 'through' }).stdout, 'through');
  check('stdin is the same object', process.stdin === process.stdin, true);
  check('stdin empty', child("process.stdin.on('end', () => console.log('end')); process.stdin.resume()", { input: '' }).stdout, 'end\n');
  check('stdin.isTTY', child('console.log(!process.stdin.isTTY)', { input: '' }).stdout, 'true\n');
  check('unused stdin does not hold the process', child('process.stdin; console.log(1)', { input: 'x' }).status, 0);

  section('Globals');
  check('global', global === globalThis, true);
  check('structuredClone', structuredClone({ a: new Date(0), b: new Map([[1, 2]]), c: [1n] }), { a: new Date(0), b: new Map([[1, 2]]), c: [1n] });
  const cycle = {};
  cycle.self = cycle;
  const cloned = structuredClone(cycle);
  check('structuredClone cycles', cloned.self === cloned, true);
  const ab = new ArrayBuffer(4);
  const moved = structuredClone(ab, { transfer: [ab] });
  check('structuredClone transfer', [moved.byteLength, ab.byteLength], [4, 0]);
  throws('structuredClone a function', () => structuredClone(() => {}), { name: 'DataCloneError' });
  throws('structuredClone nothing', () => structuredClone(), { name: 'TypeError' });

  section('TextEncoder and TextDecoder');
  check('encode', Array.from(new TextEncoder().encode('€')), [226, 130, 172]);
  check('encode gives Uint8Array', new TextEncoder().encode('a') instanceof Uint8Array, true);
  check('encode nothing', new TextEncoder().encode().length, 0);
  check('encoding', new TextEncoder().encoding, 'utf-8');
  check('decode', new TextDecoder().decode(new Uint8Array([226, 130, 172])), '€');
  check('decode ArrayBuffer', new TextDecoder().decode(new Uint8Array([97]).buffer), 'a');
  check('decode nothing', new TextDecoder().decode(), '');
  check('BOM removed', new TextDecoder().decode(new Uint8Array([0xef, 0xbb, 0xbf, 97])), 'a');
  check('BOM kept', new TextDecoder('utf-8', { ignoreBOM: true }).decode(new Uint8Array([0xef, 0xbb, 0xbf, 97])), '﻿a');
  check('replacement', new TextDecoder().decode(new Uint8Array([255])), '�');
  throws('fatal', () => new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array([255])), { name: 'TypeError', code: 'ERR_ENCODING_INVALID_ENCODED_DATA' });
  check('fatal accepts a real U+FFFD', new TextDecoder('utf-8', { fatal: true }).decode(Buffer.from('�')), '�');
  if (process.versions.frogjs) {
    throws('other encodings', () => new TextDecoder('latin1'), { code: 'ERR_ENCODING_NOT_SUPPORTED' });
  }

  section('stream of events');
  const emitter = new EventEmitter();
  setTimeout(() => emitter.emit('x', 1), 1);
  check('helper', await once(emitter, 'x'), [1]);
});
