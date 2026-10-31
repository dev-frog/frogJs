// child_process self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const cp = require('child_process');
const util = require('util');

let passed = 0;
let failed = 0;

function check(name, actual, expected) {
    if (actual === expected) {
        passed++;
        console.log(`  ✓ ${name}`);
    } else {
        failed++;
        console.error(`  ✗ ${name}\n      expected: ${JSON.stringify(expected)}\n      got:      ${JSON.stringify(actual)}`);
    }
}

// "Name code: message" of the error fn throws, or "no error"
function thrown(fn) {
    try {
        fn();
        return 'no error';
    } catch (err) {
        return `${err.name} ${err.code}: ${err.message}`;
    }
}

// Resolve with the events of a child: [exit code, signal, stdout, stderr]
function collect(child) {
    return new Promise((resolve) => {
        let stdout = '';
        let stderr = '';
        if (child.stdout) child.stdout.on('data', (d) => { stdout += d; });
        if (child.stderr) child.stderr.on('data', (d) => { stderr += d; });
        child.on('close', (code, signal) => resolve({ code, signal, stdout, stderr }));
    });
}

async function run() {
    console.log('spawn');
    {
        const child = cp.spawn('sh', ['-c', 'echo out; echo err >&2; exit 3']);
        const events = [];
        child.on('spawn', () => events.push('spawn'));
        child.on('exit', (code, signal) => events.push(`exit ${code} ${signal}`));
        const result = await collect(child);
        events.push('close');
        check('pid is a number', typeof child.pid, 'number');
        check('stdout', result.stdout, 'out\n');
        check('stderr', result.stderr, 'err\n');
        check('exit code', result.code, 3);
        check('events in order', events.join(', '), 'spawn, exit 3 null, close');
        check('exitCode', child.exitCode, 3);
        check('spawnargs', child.spawnargs.join(' '), 'sh -c echo out; echo err >&2; exit 3');
    }
    {
        const child = cp.spawn('cat');
        let bytes = 0;
        child.stdout.on('data', (d) => { bytes += d.length; });
        const chunk = Buffer.alloc(64 * 1024, 'a');
        for (let i = 0; i < 32; i++) child.stdin.write(chunk);
        child.stdin.end();
        await collect(child);
        check('stdin -> cat -> stdout (2 MB)', bytes, 2 * 1024 * 1024);
    }
    {
        const producer = cp.spawn('printf', ['a\\nb\\nc\\n']);
        const counter = cp.spawn('wc', ['-l']);
        producer.stdout.pipe(counter.stdin);
        const result = await collect(counter);
        check('pipe one child into another', result.stdout.trim(), '3');
    }
    {
        const child = cp.spawn('sleep', ['10']);
        const exited = new Promise((resolve) => child.on('exit', (code, signal) => resolve([code, signal])));
        check('kill() returns true', child.kill('SIGKILL'), true);
        const [code, signal] = await exited;
        check('killed child exit', `${code} ${signal} ${child.killed} ${child.signalCode}`, 'null SIGKILL true SIGKILL');
        check('kill() after exit', child.kill(), false);
    }
    {
        const child = cp.spawn('no-such-command-frogjs', ['x']);
        const error = await new Promise((resolve) => child.on('error', resolve));
        const code = await new Promise((resolve) => child.on('close', resolve));
        check('ENOENT error', `${error.message} ${error.code} ${error.errno} ${error.path}`,
            'spawn no-such-command-frogjs ENOENT ENOENT -2 no-such-command-frogjs');
        check('ENOENT spawnargs', error.spawnargs.join(), 'x');
        check('ENOENT close code', code, -2);
        check('ENOENT pid', child.pid, undefined);
    }
    {
        const result = await collect(cp.spawn('echo $((6 * 7))', { shell: true }));
        check('shell option', result.stdout, '42\n');
        const env = await collect(cp.spawn('sh', ['-c', 'echo $FROG'], { env: { FROG: 'ribbit', PATH: process.env.PATH } }));
        check('env option', env.stdout, 'ribbit\n');
        const cwd = await collect(cp.spawn('pwd', { cwd: '/' }));
        check('cwd option', cwd.stdout, '/\n');
        const ignored = cp.spawn('echo', ['x'], { stdio: 'ignore' });
        check('stdio ignore', `${ignored.stdout} ${ignored.stdio.length}`, 'null 3');
        check('stdio ignore close', (await collect(ignored)).code, 0);
        const timed = await collect(cp.spawn('sleep', ['5'], { timeout: 50 }));
        check('timeout option', timed.signal, 'SIGTERM');
    }

    console.log('exec / execFile');
    {
        const [err, stdout, stderr] = await new Promise((resolve) =>
            cp.exec('echo hi; echo there >&2', (...args) => resolve(args)));
        check('exec output', `${err} ${JSON.stringify(stdout)} ${JSON.stringify(stderr)}`, 'null "hi\\n" "there\\n"');
        const [failure] = await new Promise((resolve) => cp.exec('echo oops >&2; exit 2', (...args) => resolve(args)));
        check('exec failure', `${failure.message}|${failure.code}|${failure.killed}|${failure.signal}|${failure.cmd}`,
            'Command failed: echo oops >&2; exit 2\noops\n|2|false|null|echo oops >&2; exit 2');
        const [timeout] = await new Promise((resolve) => cp.exec('sleep 5', { timeout: 50 }, (...args) => resolve(args)));
        check('exec timeout', `${timeout.killed} ${timeout.signal}`, 'true SIGTERM');
        const [big] = await new Promise((resolve) => cp.exec('yes | head -c 5000', { maxBuffer: 100 }, (...args) => resolve(args)));
        check('exec maxBuffer', `${big.code}: ${big.message}`, 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER: stdout maxBuffer length exceeded');
        const [, buffer] = await new Promise((resolve) => cp.execFile('echo', ['a', 'b'], { encoding: 'buffer' }, (...args) => resolve(args)));
        check('execFile encoding buffer', Buffer.isBuffer(buffer) && buffer.toString(), 'a b\n');
        const { stdout: out, stderr: errOut } = await util.promisify(cp.exec)('echo P; echo Q >&2');
        check('promisify(exec)', `${out}${errOut}`, 'P\nQ\n');
        try {
            await util.promisify(cp.execFile)('sh', ['-c', 'echo X; exit 9']);
            check('promisify(execFile) rejects', false, true);
        } catch (e) {
            check('promisify(execFile) rejects', `${e.code} ${JSON.stringify(e.stdout)}`, '9 "X\\n"');
        }
    }

    console.log('execSync / execFileSync / spawnSync');
    {
        check('execSync returns a Buffer', Buffer.isBuffer(cp.execSync('echo hello')), true);
        check('execSync encoding', cp.execSync('echo hello', { encoding: 'utf8' }), 'hello\n');
        check('execSync input', cp.execSync('tr a-z A-Z', { input: 'frog' }).toString(), 'FROG');
        let err;
        try { cp.execSync('echo partial; echo boom >&2; exit 4', { stdio: 'pipe' }); } catch (e) { err = e; }
        check('execSync failure', `${err.message}|${err.status}|${err.signal}|${err.stdout}`,
            'Command failed: echo partial; echo boom >&2; exit 4\nboom\n|4|null|partial\n');
        check('execSync failure keys', Object.keys(err).join(), 'status,signal,output,pid,stdout,stderr');
        check('execFileSync', cp.execFileSync('echo', ['a', 'b']).toString(), 'a b\n');
        check('execFileSync ENOENT', thrown(() => cp.execFileSync('no-such-command-frogjs')),
            'Error ENOENT: spawnSync no-such-command-frogjs ENOENT');

        const result = cp.spawnSync('sh', ['-c', 'echo out; echo err >&2; exit 6']);
        check('spawnSync result', `${result.status} ${result.signal} ${result.stdout} ${result.stderr}`.trim(), '6 null out\n err');
        check('spawnSync output', result.output[0], null);
        const missing = cp.spawnSync('no-such-command-frogjs');
        check('spawnSync ENOENT', `${missing.error.code} ${missing.error.syscall} ${missing.status} ${missing.output} ${missing.pid}`,
            'ENOENT spawnSync no-such-command-frogjs null null 0');
        const timed = cp.spawnSync('sleep', ['5'], { timeout: 50 });
        check('spawnSync timeout', `${timed.error.code} ${timed.signal} ${timed.status}`, 'ETIMEDOUT SIGTERM null');
        const capped = cp.spawnSync('sh', ['-c', 'yes | head -c 10000'], { maxBuffer: 10 });
        check('spawnSync maxBuffer', capped.error.code, 'ENOBUFS');
        check('spawnSync encoding', cp.spawnSync('echo', ['x'], { encoding: 'utf8' }).stdout, 'x\n');
    }

    console.log('argument errors');
    check('empty file', thrown(() => cp.spawn('')),
        "TypeError ERR_INVALID_ARG_VALUE: The argument 'file' cannot be empty. Received ''");
    check('file not a string', thrown(() => cp.spawn(1)),
        'TypeError ERR_INVALID_ARG_TYPE: The "file" argument must be of type string. Received type number (1)');
    check('args not an array', thrown(() => cp.spawn('ls', 'x')),
        'TypeError ERR_INVALID_ARG_TYPE: The "args" argument must be of type object. Received type string (\'x\')');
    check('unknown signal', thrown(() => cp.spawn('true').kill('SIGFROG')),
        'TypeError ERR_UNKNOWN_SIGNAL: Unknown signal: SIGFROG');

    console.log(`\n${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

run().catch((err) => {
    console.error(err);
    process.exit(1);
});
