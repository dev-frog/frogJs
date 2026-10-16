// Stream module self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked
// (chunks are compared with String(chunk), since Node.js turns strings into Buffers).
const { Readable, Writable, Duplex, Transform, PassThrough, pipeline, finished } = require('stream');
const stream = require('stream');
const http = require('http');

let passed = 0;
let failed = 0;

function check(name, condition, detail) {
    if (condition) {
        passed++;
        console.log(`  ✓ ${name}`);
    } else {
        failed++;
        console.error(`  ✗ ${name}${detail !== undefined ? ' - got: ' + JSON.stringify(detail) : ''}`);
    }
}

const tick = () => new Promise((resolve) => setTimeout(resolve, 1));

// A Readable that pushes the given chunks, one per _read() call
function source(chunks, options) {
    const queue = chunks.slice();
    return new Readable(Object.assign({
        read() {
            this.push(queue.length ? queue.shift() : null);
        },
    }, options));
}

// A Writable that records what it receives
function sink(options) {
    const w = new Writable(Object.assign({
        write(chunk, encoding, cb) {
            w.received.push(String(chunk));
            cb();
        },
    }, options));
    w.received = [];
    return w;
}

function collect(readable) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        readable.on('data', (c) => chunks.push(String(c)));
        readable.on('end', () => resolve(chunks));
        readable.on('error', reject);
    });
}

async function testReadable() {
    console.log('Readable');
    const events = [];
    const r = source(['a', 'b', 'c']);
    r.on('data', (c) => events.push('data:' + c));
    r.on('end', () => events.push('end'));
    r.on('close', () => events.push('close'));
    await tick();
    check("flowing mode emits 'data', 'end', 'close'", events.join() === 'data:a,data:b,data:c,end,close', events.join());
    check('readableEnded and destroyed after end', r.readableEnded && r.destroyed);

    const paused = source(['x']);
    check('starts paused (readableFlowing is null)', paused.readableFlowing === null);
    paused.on('data', () => {});
    check("'data' listener switches to flowing", paused.readableFlowing === true);
    paused.pause();
    check('pause()', paused.isPaused() && paused.readableFlowing === false);

    const pulled = [];
    const rr = source(['one', 'two']);
    rr.on('readable', function () {
        let chunk;
        while ((chunk = this.read()) !== null) pulled.push(String(chunk));
    });
    await new Promise((resolve) => rr.on('end', resolve));
    check("paused mode with 'readable' and read()", pulled.join('') === 'onetwo', pulled);

    const sized = new Readable({ read() {} });
    sized.push('hello world');
    sized.push(null);
    const first = sized.read(5);
    const rest = sized.read();
    check('read(n) returns n units', String(first) === 'hello' && String(rest) === ' world', [String(first), String(rest)]);

    const joined = new Readable({ read() {} });
    joined.push('ab');
    joined.push('cd');
    joined.push('ef');
    check('read(n) across chunks', String(joined.read(3)) === 'abc' && String(joined.read(3)) === 'def');

    const objects = new Readable({ objectMode: true, read() {} });
    objects.push({ n: 1 });
    objects.push({ n: 2 });
    objects.push(null);
    const got = await collect(Object.assign(objects, {}));
    check('objectMode', got.length === 2, got);

    const buffered = new Readable({ objectMode: true, read() {} });
    buffered.push(1);
    buffered.push(2);
    buffered.push(null);
    const values = [];
    for await (const v of buffered) values.push(v);
    check('for await...of', values.join() === '1,2', values);

    const early = source(['1', '2', '3', '4']);
    for await (const c of early) {
        if (String(c) === '2') break;
    }
    check('breaking out of for await destroys the stream', early.destroyed);

    const fromArray = await collect(Readable.from(['x', 'y', 'z']));
    check('Readable.from(array)', fromArray.join() === 'x,y,z', fromArray);

    async function* gen() {
        yield 'g1';
        await tick();
        yield 'g2';
    }
    const fromGen = await collect(Readable.from(gen()));
    check('Readable.from(async generator)', fromGen.join() === 'g1,g2', fromGen);

    const hwm = new Readable({ highWaterMark: 4, read() {} });
    const ok1 = hwm.push('ab');
    const ok2 = hwm.push('cd');
    check('push() returns false at highWaterMark', ok1 === true && ok2 === false, [ok1, ok2]);

    const unshifted = new Readable({ read() {} });
    unshifted.push('world');
    unshifted.unshift('hello ');
    check('unshift()', String(unshifted.read()) === 'hello world');

    // A 3-byte character split across two Buffers
    const euro = Buffer.from('€');
    const split = new Readable({ read() {} });
    split.setEncoding('utf8');
    split.push(euro.slice(0, 1));
    split.push(euro.slice(1));
    split.push(null);
    const decoded = await collect(split);
    check('setEncoding decodes UTF-8 split across chunks', decoded.join('') === '€', decoded);

    const hex = new Readable({ read() {} });
    hex.setEncoding('hex');
    hex.push(Buffer.from('AB'));
    hex.push(null);
    check("setEncoding('hex')", (await collect(hex)).join('') === '4142');

    const bad = new Readable({ read() {} });
    let badErr = null;
    bad.on('error', (err) => badErr = err);
    bad.push(42);
    await tick();
    check('pushing a non-string/Buffer errors', badErr && badErr.code === 'ERR_INVALID_ARG_TYPE', badErr && badErr.code);

    const errored = new Readable({ read() {} });
    const errEvents = [];
    errored.on('error', (err) => errEvents.push('error:' + err.message));
    errored.on('close', () => errEvents.push('close'));
    errored.destroy(new Error('kaput'));
    await tick();
    check("destroy(err) emits 'error' then 'close'", errEvents.join() === 'error:kaput,close', errEvents.join());

    const pushAfterEnd = new Readable({ read() {} });
    let eofErr = null;
    pushAfterEnd.on('error', (err) => eofErr = err);
    pushAfterEnd.push(null);
    pushAfterEnd.push('late');
    await tick();
    check('push() after EOF errors', eofErr && eofErr.code === 'ERR_STREAM_PUSH_AFTER_EOF');

    const notImpl = new Readable();
    let implErr = null;
    notImpl.on('error', (err) => implErr = err);
    notImpl.read();
    await tick();
    check('missing _read() errors', implErr && implErr.code === 'ERR_METHOD_NOT_IMPLEMENTED');
}

async function testWritable() {
    console.log('Writable');
    const w = sink();
    const events = [];
    w.on('finish', () => events.push('finish'));
    w.on('close', () => events.push('close'));
    w.write('a');
    w.write('b', () => events.push('write-cb'));
    w.end('c', () => events.push('end-cb'));
    await tick();
    check('writes arrive in order', w.received.join() === 'a,b,c', w.received);
    check("end() then 'finish', 'close'", events.join() === 'write-cb,end-cb,finish,close', events.join());
    check('writableFinished', w.writableFinished && w.writableEnded);

    // Backpressure: write() returns false at the high water mark, then 'drain'
    const pending = [];
    const slow = new Writable({
        highWaterMark: 3,
        write(chunk, encoding, cb) {
            pending.push(cb);
        },
    });
    const r1 = slow.write('ab');
    const r2 = slow.write('cd');
    check('write() returns false at highWaterMark', r1 === true && r2 === false, [r1, r2]);
    check('writableNeedDrain', slow.writableNeedDrain === true && slow.writableLength === 4);
    let drained = false;
    slow.on('drain', () => drained = true);
    pending.shift()();
    await tick();
    check("no 'drain' while data is still queued", !drained);
    pending.shift()();
    await tick();
    check("'drain' once the queue empties", drained);

    const afterEnd = sink();
    afterEnd.end();
    let writeErr = null;
    let cbErr = null;
    afterEnd.on('error', (err) => writeErr = err);
    afterEnd.write('x', (err) => cbErr = err);
    await tick();
    check('write after end errors', writeErr && writeErr.code === 'ERR_STREAM_WRITE_AFTER_END' && cbErr === writeErr);

    const failing = new Writable({
        write(chunk, encoding, cb) {
            cb(new Error('disk full'));
        },
    });
    let failErr = null;
    failing.on('error', (err) => failErr = err);
    failing.write('x');
    await tick();
    check("_write error emits 'error' and destroys", failErr && failErr.message === 'disk full' && failing.destroyed);

    const batches = [];
    const corked = new Writable({
        writev(chunks, cb) {
            batches.push(chunks.map((c) => String(c.chunk)).join('+'));
            cb();
        },
    });
    corked.cork();
    corked.write('1');
    corked.write('2');
    corked.write('3');
    corked.uncork();
    await tick();
    check('cork()/uncork() batches into _writev()', batches.join() === '1+2+3', batches);

    const finalOrder = [];
    const withFinal = new Writable({
        write(chunk, encoding, cb) {
            finalOrder.push('write');
            cb();
        },
        final(cb) {
            finalOrder.push('final');
            setTimeout(cb, 1);
        },
    });
    withFinal.on('finish', () => finalOrder.push('finish'));
    withFinal.end('x');
    await new Promise((resolve) => withFinal.on('close', resolve));
    check("_final() runs before 'finish'", finalOrder.join() === 'write,final,finish', finalOrder.join());

    const objects = sink({ objectMode: true });
    objects.write({ a: 1 });
    check('objectMode accepts objects', objects.writableLength <= 1);

    let nullErr = null;
    try {
        sink().write(null);
    } catch (err) {
        nullErr = err;
    }
    check('write(null) throws', nullErr && nullErr.code === 'ERR_STREAM_NULL_VALUES');
}

async function testDuplexTransform() {
    console.log('Duplex / Transform');
    const written = [];
    const d = new Duplex({
        read() {},
        write(chunk, encoding, cb) {
            written.push(String(chunk));
            cb();
        },
    });
    d.push('out');
    d.push(null);
    d.end('in');
    const out = await collect(d);
    check('Duplex has independent sides', out.join() === 'out' && written.join() === 'in');
    check('Duplex instanceof Readable and Writable', d instanceof Readable && d instanceof Writable);
    check('Readable is not instanceof Writable', !(new Readable() instanceof Writable));

    const upper = new Transform({
        transform(chunk, encoding, cb) {
            cb(null, String(chunk).toUpperCase());
        },
        flush(cb) {
            this.push('!');
            cb();
        },
    });
    const resultP = collect(upper);
    upper.write('hello ');
    upper.end('world');
    const result = await resultP;
    check('Transform with flush', result.join('') === 'HELLO WORLD!', result.join(''));

    const pass = new PassThrough();
    const passP = collect(pass);
    pass.end('through');
    check('PassThrough', (await passP).join('') === 'through');

    const objT = new Transform({
        objectMode: true,
        transform(obj, encoding, cb) {
            this.push(obj.n);
            this.push(obj.n * 10);
            cb();
        },
    });
    const objP = new Promise((resolve) => {
        const seen = [];
        objT.on('data', (v) => seen.push(v));
        objT.on('end', () => resolve(seen));
    });
    objT.write({ n: 1 });
    objT.end({ n: 2 });
    check('Transform can push several outputs per input', (await objP).join() === '1,10,2,20');
}

async function testPipe() {
    console.log('pipe / pipeline / finished');
    const w = sink();
    source(['p', 'i', 'p', 'e']).pipe(w);
    await new Promise((resolve) => w.on('finish', resolve));
    check('pipe() moves all data and ends the destination', w.received.join('') === 'pipe');

    // Backpressure through pipe: the source waits while the destination is full
    let maxQueued = 0;
    const produced = Array.from({ length: 50 }, (_, i) => 'chunk' + i);
    const slowSink = new Writable({
        highWaterMark: 10,
        write(chunk, encoding, cb) {
            maxQueued = Math.max(maxQueued, this.writableLength);
            setTimeout(cb, 0);
        },
    });
    const src = source(produced, { highWaterMark: 10 });
    src.pipe(slowSink);
    await new Promise((resolve) => slowSink.on('finish', resolve));
    check('pipe() respects backpressure', maxQueued <= 20, maxQueued);

    const dest = sink();
    const r = new Readable({ read() {} });
    r.pipe(dest, { end: false });
    r.push('data');
    r.push(null);
    await tick();
    await tick();
    check('pipe({ end: false }) keeps the destination open', !dest.writableEnded && dest.received.join() === 'data');

    const unpiped = sink();
    const ur = new Readable({ read() {} });
    let unpipeEvent = false;
    unpiped.on('unpipe', () => unpipeEvent = true);
    ur.pipe(unpiped);
    ur.unpipe(unpiped);
    check("unpipe() emits 'unpipe' and pauses", unpipeEvent && ur.isPaused());

    const plOut = sink();
    const upper = new Transform({
        transform(chunk, encoding, cb) {
            cb(null, String(chunk).toUpperCase());
        },
    });
    const plErr = await new Promise((resolve) => pipeline(source(['a', 'b']), upper, plOut, resolve));
    check('pipeline() success', plErr === undefined && plOut.received.join('') === 'AB', plErr);

    const failing = new Transform({
        transform(chunk, encoding, cb) {
            cb(new Error('transform failed'));
        },
    });
    const plSrc = source(['a', 'b', 'c']);
    const plDest = sink();
    const failErr = await new Promise((resolve) => pipeline(plSrc, failing, plDest, resolve));
    check('pipeline() reports the first error', failErr && failErr.message === 'transform failed', failErr && failErr.message);
    check('pipeline() destroys every stream on error', plSrc.destroyed && failing.destroyed && plDest.destroyed);

    const iterOut = sink();
    await stream.promises.pipeline(['from', 'iterable'], iterOut);
    check('promises.pipeline() with an iterable source', iterOut.received.join(' ') === 'from iterable');

    const fr = source(['f']);
    fr.resume();
    const finErr = await new Promise((resolve) => finished(fr, resolve));
    check('finished() on a readable', finErr === undefined || finErr === null, finErr);

    const premature = new Readable({ read() {} });
    const prematureP = new Promise((resolve) => finished(premature, resolve));
    premature.destroy();
    const premErr = await prematureP;
    check('finished() reports premature close', premErr && premErr.code === 'ERR_STREAM_PREMATURE_CLOSE', premErr && premErr.code);

    const fw = sink();
    const fwP = stream.promises.finished(fw);
    fw.end('done');
    await fwP;
    check('promises.finished() on a writable', fw.writableFinished);
}

async function testHttp() {
    console.log('HTTP integration');
    const server = http.createServer((req, res) => {
        if (req.url === '/echo') {
            req.pipe(res);  // request body streams straight into the response
        } else {
            const upper = new Transform({
                transform(chunk, encoding, cb) {
                    cb(null, String(chunk).toUpperCase());
                },
            });
            pipeline(req, upper, res, () => {});
        }
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;

    async function post(path, body) {
        return new Promise((resolve, reject) => {
            const req = http.request({ port, hostname: '127.0.0.1', path, method: 'POST' }, async (res) => {
                check('IncomingMessage is a Readable', res instanceof Readable);
                let data = '';
                for await (const chunk of res) data += chunk;
                resolve(data);
            });
            req.on('error', reject);
            req.write(body.slice(0, 3));
            req.end(body.slice(3));
        });
    }

    check('req.pipe(res) echoes the body', (await post('/echo', 'stream me')) === 'stream me');
    check('pipeline(req, transform, res)', (await post('/upper', 'shout')) === 'SHOUT');
    check('http objects are EventEmitters', server instanceof require('events'));
    await new Promise((resolve) => server.close(resolve));
}

(async () => {
    await testReadable();
    await testWritable();
    await testDuplexTransform();
    await testPipe();
    await testHttp();
})()
    .catch((err) => {
        failed++;
        console.error('Unexpected error:', err && err.stack || err);
    })
    .then(() => {
        console.log(`\n${passed} passed, ${failed} failed`);
        if (failed > 0) process.exit(1);
    });
