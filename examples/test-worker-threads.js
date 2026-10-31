// worker_threads self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const {
    Worker, isMainThread, parentPort, workerData, threadId,
    MessageChannel, MessagePort, receiveMessageOnPort,
} = require('worker_threads');

// This file is also the script of the workers it starts
function workerMain() {
    if (workerData.mode === 'echo') {
        parentPort.on('message', (message) => {
            if (message === 'stop') parentPort.close();
            else parentPort.postMessage({ echo: message, threadId });
        });
    } else if (workerData.mode === 'count') {
        const counter = new Int32Array(workerData.shared);
        for (let i = 0; i < workerData.times; i++) Atomics.add(counter, 0, 1);
    }
}

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

// Run an eval worker; resolve with its messages, error and exit code
function runWorker(code, options = {}) {
    return new Promise((resolve) => {
        const worker = new Worker(code, { eval: true, ...options });
        const result = { messages: [], error: null, events: [] };
        worker.on('online', () => result.events.push('online'));
        worker.on('message', (m) => result.messages.push(m));
        worker.on('error', (e) => { result.error = e; result.events.push('error'); });
        worker.on('exit', (code) => { result.code = code; result.events.push('exit'); resolve(result); });
    });
}

async function run() {
    console.log('main thread');
    check('isMainThread', isMainThread, true);
    check('threadId', threadId, 0);
    check('parentPort', parentPort, null);
    check('workerData', workerData, null);

    console.log('Worker');
    {
        const worker = new Worker(__filename, { workerData: { mode: 'echo' } });
        const id = worker.threadId;
        const replies = [];
        await new Promise((resolve) => {
            worker.on('message', (m) => {
                replies.push(m);
                if (replies.length === 3) worker.postMessage('stop');
            });
            worker.on('exit', resolve);
            worker.postMessage('a');
            worker.postMessage({ nested: [1, { two: 2 }] });
            worker.postMessage(new Map([['k', new Set([1, 2])]]));
        });
        check('messages arrive in order', replies.map((r) => JSON.stringify(r.echo)).join(' '), '"a" {"nested":[1,{"two":2}]} {}');
        check('Map and Set are cloned', replies[2].echo.get('k').has(2), true);
        check('worker threadId', replies[0].threadId === id && id > 0, true);
        check('threadId after exit', worker.threadId, -1);
    }
    {
        const result = await runWorker(`
            const { parentPort, workerData, isMainThread } = require('worker_threads');
            parentPort.postMessage([isMainThread, workerData.date instanceof Date, workerData.big, workerData.bytes.length]);`,
            { workerData: { date: new Date(0), big: 10n, bytes: new Uint8Array([1, 2, 3]) } });
        check('workerData is cloned', JSON.stringify(result.messages[0], (k, v) => typeof v === 'bigint' ? `${v}n` : v),
            '[false,true,"10n",3]');
        check('event order', result.events.join(), 'online,exit');
        check('exit code', result.code, 0);
    }
    {
        const result = await runWorker('throw new TypeError("bad worker")');
        check('uncaught error', `${result.error instanceof Error} ${result.error.name}: ${result.error.message}`, 'true TypeError: bad worker');
        check('error exit code', result.code, 1);
        const late = await runWorker('setTimeout(() => { const e = new Error("late"); e.code = "E_LATE"; throw e; }, 5); setInterval(() => {}, 1000)');
        check('error in a callback', `${late.error.message} ${late.error.code} ${late.code}`, 'late E_LATE 1');
        const missing = await runWorker('./no-such-worker-file.js', { eval: false });
        check('missing script', `${missing.error.code} ${missing.code}`, 'MODULE_NOT_FOUND 1');
    }
    {
        const result = await runWorker('require("worker_threads").parentPort.postMessage("before"); process.exit(7); console.log("never");');
        check('process.exit() ends only the worker', `${result.messages} ${result.code}`, 'before 7');
    }
    {
        const worker = new Worker('require("worker_threads").parentPort.postMessage("busy"); for (;;) {}', { eval: true });
        await new Promise((resolve) => worker.once('message', resolve));
        check('terminate() stops a busy worker', await worker.terminate(), 1);
        check('terminate() after exit', await worker.terminate(), undefined);
    }
    {
        const result = await runWorker('console.log; require("worker_threads").parentPort.postMessage(process.argv.slice(2))', { argv: ['x', 1] });
        check('argv option', result.messages[0].join(), 'x,1');
    }
    {
        const result = await runWorker(`
            const { Worker, parentPort } = require('worker_threads');
            const inner = new Worker('require("worker_threads").parentPort.postMessage("from inner")', { eval: true });
            inner.on('message', (m) => parentPort.postMessage(m));`);
        check('nested workers', result.messages[0], 'from inner');
    }
    check('relative path must start with ./', thrown(() => new Worker('worker.js')),
        "TypeError ERR_WORKER_PATH: The worker script or module filename must be an absolute path or a relative path starting with './' or '../'. Received \"worker.js\"");

    console.log('SharedArrayBuffer');
    {
        const shared = new SharedArrayBuffer(4);
        const counter = new Int32Array(shared);
        const workers = [1, 2, 3, 4].map(() => new Worker(__filename, { workerData: { mode: 'count', shared, times: 10000 } }));
        await Promise.all(workers.map((w) => new Promise((resolve) => w.on('exit', resolve))));
        check('four workers share one counter', Atomics.load(counter, 0), 40000);
        const view = new Int32Array(new SharedArrayBuffer(8));
        await runWorker('new Int32Array(require("worker_threads").workerData)[1] = 99', { workerData: view.buffer });
        check('main sees writes from a worker', view[1], 99);
    }

    console.log('MessageChannel');
    {
        const { port1, port2 } = new MessageChannel();
        check('ports are MessagePorts', port1 instanceof MessagePort && port2 instanceof MessagePort, true);
        port1.postMessage({ hello: 'world' });
        check('receiveMessageOnPort', JSON.stringify(receiveMessageOnPort(port2)), '{"message":{"hello":"world"}}');
        check('receiveMessageOnPort when empty', receiveMessageOnPort(port2), undefined);

        const buffer = new ArrayBuffer(8);
        new Uint8Array(buffer)[0] = 42;
        port1.postMessage(buffer, [buffer]);
        check('transferred ArrayBuffer is detached', buffer.byteLength, 0);
        const moved = receiveMessageOnPort(port2).message;
        check('transferred ArrayBuffer arrives', `${moved.byteLength} ${new Uint8Array(moved)[0]}`, '8 42');

        check('functions cannot be cloned', thrown(() => port1.postMessage(() => 1)), 'DataCloneError 25: () => 1 could not be cloned.');
        check('ports must be transferred', thrown(() => port1.postMessage(port2)).split(':')[0], 'DataCloneError 25');
        check('source port in transfer list', thrown(() => port1.postMessage(1, [port1])),
            'DataCloneError 25: Transfer list contains source port');

        const got = new Promise((resolve) => { port2.onmessage = (event) => resolve(event.data); });
        port1.postMessage('via onmessage');
        check('onmessage gets an event', await got, 'via onmessage');
        const closed = new Promise((resolve) => port2.on('close', resolve));
        port1.close();
        await closed;
        check('closing one port closes the other', true, true);
    }
    {
        // Hand one end of a channel to a worker and talk to it directly
        const { port1, port2 } = new MessageChannel();
        const worker = new Worker(`
            const { parentPort } = require('worker_threads');
            parentPort.once('message', ({ port }) => {
                port.on('message', (m) => { port.postMessage(m * 2); port.close(); });
            });`, { eval: true });
        worker.postMessage({ port: port2 }, [port2]);
        const reply = new Promise((resolve) => port1.once('message', resolve));
        port1.postMessage(21);
        check('transferred port works in the worker', await reply, 42);
        await new Promise((resolve) => worker.on('exit', resolve));
    }

    console.log(`\n${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

if (isMainThread) {
    run().catch((err) => {
        console.error(err);
        process.exit(1);
    });
} else {
    workerMain();
}
