// Events module self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const EventEmitter = require('events');

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

function throws(fn) {
    try {
        fn();
        return null;
    } catch (err) {
        return err;
    }
}

async function run() {
    console.log('EventEmitter');
    check('require returns the class', typeof EventEmitter === 'function' && EventEmitter.EventEmitter === EventEmitter);

    let e = new EventEmitter();
    const calls = [];
    e.on('x', (a, b) => calls.push(['first', a, b]));
    e.on('x', function () { calls.push(['second', this === e]); });
    check('emit returns true with listeners', e.emit('x', 1, 2) === true);
    check('listeners run in order with args and this', JSON.stringify(calls) === '[["first",1,2],["second",true]]', calls);
    check('emit returns false without listeners', e.emit('nothing') === false);
    check('on() is chainable', e.on('y', () => {}) === e);

    e = new EventEmitter();
    let count = 0;
    e.once('o', () => count++);
    e.emit('o');
    e.emit('o');
    check('once fires a single time', count === 1, count);
    check('once listener is removed', e.listenerCount('o') === 0);

    e = new EventEmitter();
    const order = [];
    e.on('p', () => order.push('b'));
    e.prependListener('p', () => order.push('a'));
    e.prependOnceListener('p', () => order.push('first'));
    e.emit('p');
    e.emit('p');
    check('prependListener / prependOnceListener', order.join() === 'first,a,b,a,b', order.join());

    e = new EventEmitter();
    const f = () => {};
    const g = () => {};
    e.on('r', f);
    e.on('r', g);
    e.on('r', f);
    e.removeListener('r', f);
    check('removeListener removes the last match', e.listeners('r')[0] === f && e.listeners('r')[1] === g && e.listenerCount('r') === 2);
    e.off('r', f);
    e.off('r', g);
    check('off removes listeners', e.listenerCount('r') === 0);
    check('eventNames drops empty events', e.eventNames().length === 0, e.eventNames());

    e = new EventEmitter();
    const once = () => {};
    e.once('w', once);
    check('listeners() unwraps once', e.listeners('w')[0] === once);
    check('rawListeners() keeps the wrapper', e.rawListeners('w')[0] !== once && e.rawListeners('w')[0].listener === once);
    e.removeListener('w', once);
    check('removeListener works for once listeners', e.listenerCount('w') === 0);

    e = new EventEmitter();
    const seen = [];
    e.on('m', () => {
        seen.push(1);
        e.on('m', () => seen.push('added'));
    });
    e.on('m', () => seen.push(2));
    e.emit('m');
    check('listeners added during emit wait for the next emit', seen.join() === '1,2', seen.join());

    e = new EventEmitter();
    const removedDuring = [];
    const second = () => removedDuring.push('second');
    e.on('m', () => {
        removedDuring.push('first');
        e.removeListener('m', second);
    });
    e.on('m', second);
    e.emit('m');
    check('listeners removed during emit still run this time', removedDuring.join() === 'first,second', removedDuring.join());

    console.log('Special events');
    e = new EventEmitter();
    const meta = [];
    e.on('newListener', (name) => meta.push('new:' + name));
    e.on('removeListener', (name) => meta.push('removed:' + name));
    const h = () => {};
    e.on('q', h);
    e.removeListener('q', h);
    check("'newListener' and 'removeListener'", meta.join() === 'new:removeListener,new:q,removed:q', meta.join());

    e = new EventEmitter();
    const removedAll = [];
    e.on('a', () => {});
    e.on('b', () => {});
    e.on('removeListener', (name) => removedAll.push(name));
    e.removeAllListeners();
    check("removeAllListeners() emits 'removeListener'", removedAll.join() === 'a,b', removedAll.join());
    check('removeAllListeners() clears everything', e.eventNames().length === 0);

    e = new EventEmitter();
    e.on('a', () => {});
    e.on('b', () => {});
    e.removeAllListeners('a');
    check('removeAllListeners(name)', e.listenerCount('a') === 0 && e.listenerCount('b') === 1);

    console.log('Errors');
    e = new EventEmitter();
    const boom = new Error('boom');
    check("unhandled 'error' throws the error", throws(() => e.emit('error', boom)) === boom);
    const nonError = throws(() => e.emit('error', 'text'));
    check("unhandled non-Error 'error' throws ERR_UNHANDLED_ERROR", nonError && nonError.code === 'ERR_UNHANDLED_ERROR', nonError && nonError.message);
    let handled = null;
    e.on('error', (err) => handled = err);
    e.emit('error', boom);
    check("handled 'error' doesn't throw", handled === boom);

    e = new EventEmitter();
    let monitored = null;
    e.on(EventEmitter.errorMonitor, (err) => monitored = err);
    throws(() => e.emit('error', boom));
    check('errorMonitor sees errors first', monitored === boom);

    e = new EventEmitter();
    e.on('t', () => { throw new Error('listener threw'); });
    const thrown = throws(() => e.emit('t'));
    check('listener exceptions propagate', thrown && thrown.message === 'listener threw');

    const badListener = throws(() => e.on('x', 'not a function'));
    check('non-function listener is a TypeError', badListener instanceof TypeError && badListener.code === 'ERR_INVALID_ARG_TYPE');

    console.log('Max listeners');
    e = new EventEmitter();
    check('default max listeners is 10', e.getMaxListeners() === 10 && EventEmitter.defaultMaxListeners === 10);
    e.setMaxListeners(1);
    check('setMaxListeners', e.getMaxListeners() === 1);
    check('negative max listeners is a RangeError', throws(() => e.setMaxListeners(-1)) instanceof RangeError);
    e.setMaxListeners(0);
    for (let i = 0; i < 20; i++) e.on('many', () => {});
    check('setMaxListeners(0) allows unlimited listeners', e.listenerCount('many') === 20);

    console.log('Inheritance');
    class Child extends EventEmitter {
        constructor() {
            super();
            this.value = 42;
        }
    }
    const child = new Child();
    let got = null;
    child.on('ping', function (v) { got = this.value + v; });
    child.emit('ping', 1);
    check('class extends EventEmitter', got === 43 && child instanceof EventEmitter);

    function Legacy() {
        EventEmitter.call(this);
    }
    Object.setPrototypeOf(Legacy.prototype, EventEmitter.prototype);
    const legacy = new Legacy();
    let legacyGot = false;
    legacy.on('ping', () => legacyGot = true);
    legacy.emit('ping');
    check('EventEmitter.call(this) style', legacyGot);

    const bare = Object.create(EventEmitter.prototype);
    let bareGot = false;
    bare.on('ping', () => bareGot = true);
    bare.emit('ping');
    check('works without calling the constructor', bareGot);

    const sym = Symbol('event');
    e = new EventEmitter();
    let symGot = false;
    e.on(sym, () => symGot = true);
    e.emit(sym);
    check('symbol event names', symGot && e.eventNames()[0] === sym);

    console.log('Promise helpers');
    e = new EventEmitter();
    setTimeout(() => e.emit('ready', 'a', 'b'), 1);
    const args = await EventEmitter.once(e, 'ready');
    check('events.once resolves with the arguments', JSON.stringify(args) === '["a","b"]', args);
    check('events.once cleans up', e.listenerCount('ready') === 0 && e.listenerCount('error') === 0);

    e = new EventEmitter();
    setTimeout(() => e.emit('error', boom), 1);
    const rejected = await EventEmitter.once(e, 'ready').then(() => null, (err) => err);
    check("events.once rejects on 'error'", rejected === boom);

    e = new EventEmitter();
    setTimeout(() => {
        e.emit('tick', 1);
        e.emit('tick', 2);
        e.emit('tick', 3);
    }, 1);
    const ticks = [];
    for await (const [n] of EventEmitter.on(e, 'tick')) {
        ticks.push(n);
        if (n === 3) break;
    }
    check('events.on async iterator', ticks.join() === '1,2,3', ticks.join());
    check('events.on cleans up after break', e.listenerCount('tick') === 0);
}

run()
    .catch((err) => {
        failed++;
        console.error('Unexpected error:', err && err.stack || err);
    })
    .then(() => {
        console.log(`\n${passed} passed, ${failed} failed`);
        if (failed > 0) process.exit(1);
    });
