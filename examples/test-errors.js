// Uncaught errors, rejections and exit codes, run in child processes. Exits with
// code 1 if a check fails. Also runs under Node.js, except for the messages
// that aren't the same.
const { spawnSync } = require('child_process');
const { check, section, run } = require('./lib/check');

function script(code) {
  return spawnSync(process.execPath, ['-e', code], { encoding: 'utf8', timeout: 20000 });
}

function check_(name, code, { status = 0, stdout, stderr }) {
  const result = script(code);
  check(`${name}: exit code`, result.status, status);
  if (stdout !== undefined) check(`${name}: stdout`, result.stdout, stdout);
  if (stderr !== undefined) check(`${name}: stderr`, stderr.test(result.stderr), true);
}

run(() => {
  section('Uncaught exceptions end the process');
  check_('main script', "throw new Error('boom')", { status: 1, stderr: /Error: boom/ });
  check_('timer', "setTimeout(() => { throw new Error('boom') }, 1); setTimeout(() => console.log('late'), 100)", { status: 1, stdout: '', stderr: /Error: boom/ });
  check_('interval', "setInterval(() => { throw new Error('boom') }, 1)", { status: 1, stderr: /boom/ });
  check_('immediate', "setImmediate(() => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('nextTick', "process.nextTick(() => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('queueMicrotask', "queueMicrotask(() => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('fs callback', "require('fs').readFile('/nope', () => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('event listener', "const e = new (require('events'))(); setTimeout(() => e.emit('x'), 1); e.on('x', () => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('unhandled error event', "new (require('events'))().emit('error', new Error('boom'))", { status: 1, stderr: /boom/ });
  check_('socket error without a listener', "require('net').connect(1, '127.0.0.1')", { status: 1, stderr: /ECONNREFUSED/ });
  check_('net server callback', "const n = require('net'); const s = n.createServer(() => { throw new Error('boom') }); s.listen(0, '127.0.0.1', () => n.connect(s.address().port, '127.0.0.1'))", { status: 1, stderr: /boom/ });
  check_('non-Error value', "setTimeout(() => { throw 'text' }, 1)", { status: 1, stderr: /text/ });
  check_('child process callback', "require('child_process').exec('true', () => { throw new Error('boom') })", { status: 1, stderr: /boom/ });
  check_('crypto callback', "require('crypto').randomBytes(1, () => { throw new Error('boom') })", { status: 1, stderr: /boom/ });

  section('uncaughtException');
  check_('handled', "process.on('uncaughtException', (err, origin) => console.log(err.message, origin)); setTimeout(() => { throw new Error('boom') }, 1); setTimeout(() => console.log('alive'), 10)", { stdout: 'boom uncaughtException\nalive\n' });
  check_('in the main script', "process.on('uncaughtException', (err) => console.log(err.message)); setTimeout(() => console.log('alive'), 1); throw new Error('boom')", { stdout: 'boom\nalive\n' });
  check_('listener throws', "process.on('uncaughtException', () => { throw new Error('again') }); throw new Error('boom')", { status: 7, stderr: /again/ });

  section('Unhandled rejections');
  check_('Error', "Promise.reject(new Error('nope')); setTimeout(() => console.log('late'), 100)", { status: 1, stdout: '', stderr: /nope/ });
  check_('non-Error', "Promise.reject(5)", { status: 1, stderr: /ERR_UNHANDLED_REJECTION/ });
  check_('async function', "(async () => { throw new Error('nope') })()", { status: 1, stderr: /nope/ });
  check_('handled in time', "const p = Promise.reject(new Error('nope')); p.catch(() => console.log('caught'))", { stdout: 'caught\n' });
  check_('handled in the same tick', "const p = Promise.reject(1); queueMicrotask(() => p.catch(() => console.log('caught')))", { stdout: 'caught\n' });
  check_('unhandledRejection listener', "process.on('unhandledRejection', (r, p) => console.log('saw', r, p instanceof Promise)); Promise.reject(5); setTimeout(() => console.log('alive'), 5)", { stdout: 'saw 5 true\nalive\n' });
  check_('listener throws', "process.on('unhandledRejection', () => { throw new Error('again') }); Promise.reject(1)", { status: 1, stderr: /again/ });
  check_('goes to uncaughtException', "process.on('uncaughtException', (e) => console.log('got', e.message)); Promise.reject(new Error('nope'))", { stdout: 'got nope\n' });

  section('Exit');
  check_('exit code', 'process.exit(3)', { status: 3 });
  check_('no code', 'process.exit()', { status: 0 });
  check_('exitCode', 'process.exitCode = 4', { status: 4 });
  check_('exit() uses exitCode', 'process.exitCode = 5; process.exit()', { status: 5 });
  check_('exit(code) replaces exitCode', 'process.exitCode = 5; process.exit(6)', { status: 6 });
  check_('exit from a timer', 'setTimeout(() => process.exit(8), 1)', { status: 8 });
  check_('exit stops the script', "process.exit(0); console.log('no')", { stdout: '' });
  check_('exit event with code', "process.on('exit', (c) => console.log('exit', c)); process.exit(2)", { status: 2, stdout: 'exit 2\n' });
  check_('exit event at the end', "process.on('exit', (c) => console.log('exit', c, process.exitCode))", { stdout: 'exit 0 undefined\n'.replace(' undefined', process.versions.frogjs ? ' 0' : ' undefined') });
  check_('exit event after an error', "process.on('exit', (c) => console.log('exit', c)); throw new Error('x')", { status: 1, stdout: 'exit 1\n' });
  check_('listener sets the code', "process.on('exit', () => { process.exitCode = 9 })", { status: 9 });
  check_('exit runs once', "process.on('exit', () => { console.log('once'); process.exit(3) })", { status: 3, stdout: 'once\n' });
  check_('beforeExit', "let n = 0; process.on('beforeExit', () => { console.log('before', n); if (n++ === 0) setTimeout(() => console.log('more'), 1) })", { stdout: 'before 0\nmore\nbefore 1\n' });
  check_('no beforeExit on exit()', "process.on('beforeExit', () => console.log('no')); process.exit()", { stdout: '' });
  check_('an unref timer does not hold the process', "setTimeout(() => console.log('no'), 100000).unref(); console.log('end')", { stdout: 'end\n' });
  check_('an unref server does not hold the process', "const s = require('net').createServer(); s.listen(0, '127.0.0.1'); s.unref()", { status: 0 });
  check_('an unref interval and immediate', "setInterval(() => {}, 1).unref(); setImmediate(() => console.log('no')).unref()", { status: 0 });

  section('Things that must not crash');
  check_('invalid native receiver', "try { Buffer.prototype.toString.call({}) } catch (e) { console.log(e.name) }", { stdout: 'TypeError\n' });
  check_('deep recursion', "function f() { f() } try { f() } catch (e) { console.log(e.name) }", { stdout: 'RangeError\n' });
  check_('huge string', "try { let s = 'x'; for (;;) s += s } catch (e) { console.log(e.name) }", { stdout: 'RangeError\n' });
  check_('huge Buffer allocation', "try { Buffer.alloc(2 ** 52) } catch (e) { console.log(e.name) }", { stdout: 'RangeError\n' });
  check_('throwing getter in console.log', "try { console.log({ get a() { throw new Error('g') } }) } catch (e) { console.log('caught') }", {});
  check_('throwing inspect', "try { console.log({ [Symbol.for('nodejs.util.inspect.custom')]() { throw new Error('i') } }) } catch (e) { console.log('caught') }", { stdout: 'caught\n' });
  check_('toString throws in an uncaught object', "throw { toString() { throw new Error('t') } }", { status: 1 });
  check_('a replaced global', "globalThis.Buffer = 1; globalThis.fs = 2; console.log(require('fs').existsSync('/'), require('crypto').randomBytes(1).length)", { stdout: 'true 1\n' });
  check_('worker recursion', "const { Worker } = require('worker_threads'); const w = new Worker('function f() { f() } try { f() } catch (e) { require(\"worker_threads\").parentPort.postMessage(e.name) }', { eval: true }); w.on('message', (m) => console.log(m))", { stdout: 'RangeError\n' });
  check_('NUL in spawn arguments', "for (const f of [() => require('child_process').spawnSync('a\\0b'), () => require('child_process').spawnSync('ls', ['a\\0b']), () => require('child_process').spawnSync('ls', { env: { A: 'a\\0b' } })]) { try { f(); console.log('no') } catch (e) { console.log(e.code) } }", { stdout: 'ERR_INVALID_ARG_VALUE\nERR_INVALID_ARG_VALUE\nERR_INVALID_ARG_VALUE\n' });
});
