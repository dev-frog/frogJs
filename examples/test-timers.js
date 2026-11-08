// Timer self-test: exits with code 1 if a check fails. Also runs under Node.js.
const { check, throws, section, sleep, run } = require('./lib/check');

run(async () => {
  section('setTimeout');
  check('passes arguments', await new Promise((resolve) => setTimeout((a, b) => resolve([a, b]), 1, 'x', 'y')), ['x', 'y']);
  check('no delay', await new Promise((resolve) => setTimeout(() => resolve('ran'))), 'ran');
  check('negative delay', await new Promise((resolve) => setTimeout(() => resolve('ran'), -1)), 'ran');
  check('NaN delay', await new Promise((resolve) => setTimeout(() => resolve('ran'), NaN)), 'ran');
  check('huge delay becomes 1', await new Promise((resolve) => setTimeout(() => resolve('ran'), 2 ** 40)), 'ran');
  check('string delay', await new Promise((resolve) => setTimeout(() => resolve('ran'), '1')), 'ran');
  const self = await new Promise((resolve) => {
    const t = setTimeout(function () {
      resolve(this === t);
    }, 1);
  });
  check('this is the timeout', self, true);
  throws('needs a function', () => setTimeout('x', 1), { name: 'TypeError', code: 'ERR_INVALID_ARG_TYPE' });
  throws('needs a function: null', () => setTimeout(null), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('needs a function: object', () => setTimeout({}), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Order');
  const order = [];
  await new Promise((resolve) => {
    setTimeout(() => order.push('b'), 20);
    setTimeout(() => order.push('a'), 5);
    setTimeout(() => {
      order.push('c');
      resolve();
    }, 40);
  });
  check('by delay', order, ['a', 'b', 'c']);

  section('clearTimeout');
  let cleared = 0;
  const a = setTimeout(() => cleared++, 1);
  clearTimeout(a);
  const b = setTimeout(() => cleared++, 1);
  clearTimeout(+b);
  const c = setTimeout(() => cleared++, 1);
  c.close();
  clearTimeout(undefined);
  clearTimeout(null);
  clearTimeout(99999);
  clearTimeout(a);
  await sleep(10);
  check('never ran', cleared, 0);

  section('setInterval');
  check('repeats until cleared', await new Promise((resolve) => {
    let count = 0;
    const t = setInterval((step) => {
      count += step;
      if (count === 3) {
        clearInterval(t);
        resolve(count);
      }
    }, 1, 1);
  }), 3);
  throws('needs a function', () => setInterval(1, 1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Timeout objects');
  const t = setTimeout(() => {}, 1);
  check('hasRef', t.hasRef(), true);
  check('unref chains', t.unref() === t, true);
  check('unref', t.hasRef(), false);
  check('ref chains', t.ref() === t, true);
  check('primitive is a number', typeof +t, 'number');
  clearTimeout(t);
  check('refresh delays', await new Promise((resolve) => {
    const start = Date.now();
    const r = setTimeout(() => resolve(Date.now() - start >= 25), 20);
    setTimeout(() => r.refresh(), 10);
  }), true);
  const again = [];
  await new Promise((resolve) => {
    const r = setTimeout(() => {
      again.push(1);
      if (again.length === 2) resolve();
      else r.refresh();
    }, 1);
  });
  check('refresh after firing runs it again', again.length, 2);
  const refreshInterval = setInterval(() => {}, 1000);
  check('refresh of an interval', refreshInterval.refresh() === refreshInterval, true);
  clearInterval(refreshInterval);

  section('Unref');
  let unrefRan = false;
  setTimeout(() => {
    unrefRan = true;
  }, 100000).unref();
  check('an unref timer exists', unrefRan, false);

  section('setImmediate');
  check('passes arguments', await new Promise((resolve) => setImmediate((x) => resolve(x), 'x')), 'x');
  const seen = [];
  await new Promise((resolve) => {
    setImmediate(() => {
      seen.push('first');
      setImmediate(() => {
        seen.push('next iteration');
        resolve();
      });
    });
    setImmediate(() => seen.push('second'));
  });
  check('runs in order; new ones wait', seen, ['first', 'second', 'next iteration']);
  let immediateRan = false;
  const im = setImmediate(() => {
    immediateRan = true;
  });
  clearImmediate(im);
  clearImmediate(undefined);
  clearImmediate({});
  await sleep(5);
  check('clearImmediate', immediateRan, false);
  const refImmediate = setImmediate(() => {});
  check('Immediate hasRef', refImmediate.hasRef(), true);
  check('Immediate unref', refImmediate.unref().hasRef(), false);
  check('Immediate ref', refImmediate.ref().hasRef(), true);
  clearImmediate(refImmediate);
  const keep = setImmediate(() => {});
  const drop = setImmediate(() => {});
  clearImmediate(drop);
  clearImmediate(keep);
  const unrefedImmediate = setImmediate(() => {});
  unrefedImmediate.unref();
  await sleep(1);
  throws('needs a function', () => setImmediate(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('Scheduling');
  const steps = [];
  await new Promise((resolve) => {
    setTimeout(() => {
      steps.push('timeout');
      resolve();
    }, 5);
    process.nextTick(() => steps.push('tick'));
    queueMicrotask(() => steps.push('microtask'));
  });
  check('ticks and microtasks before timers', steps.slice().sort(), ['microtask', 'tick', 'timeout']);
  check('timeout last', steps[2], 'timeout');
  check('nextTick arguments', await new Promise((resolve) => process.nextTick((a, b) => resolve(a + b), 1, 2)), 3);
  throws('nextTick needs a function', () => process.nextTick(1), { code: 'ERR_INVALID_ARG_TYPE' });
  throws('queueMicrotask needs a function', () => queueMicrotask(1), { code: 'ERR_INVALID_ARG_TYPE' });

  section('require("timers")');
  const timers = require('timers');
  check('exports', [timers.setTimeout, timers.clearInterval, timers.setImmediate], [setTimeout, clearInterval, setImmediate]);
});
