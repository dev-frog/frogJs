// assert self-test: exits with code 1 if a check fails. Also runs under
// Node.js, which is where the expected messages come from.
const assert = require('assert');
const util = require('util');
const { check, section, run } = require('./lib/check');

// The AssertionError fn throws, as { message, operator, generatedMessage }
function failure(fn) {
  try {
    fn();
  } catch (err) {
    return { name: err.name, code: err.code, message: err.message, operator: err.operator, generatedMessage: err.generatedMessage };
  }
  return 'no error';
}
const message = (fn) => failure(fn).message;

run(async () => {
  section('Passing assertions');
  assert(true);
  assert.ok(1);
  assert.equal(1, '1');
  assert.notEqual(1, 2);
  assert.strictEqual(NaN, NaN);
  assert.notStrictEqual(0, -0);
  assert.deepEqual({ a: [1, '2'] }, { a: ['1', 2] });
  assert.deepStrictEqual(new Map([[{ k: 1 }, new Set([1, { v: 2 }])]]), new Map([[{ k: 1 }, new Set([{ v: 2 }, 1])]]));
  assert.notDeepStrictEqual({ a: 1 }, { a: '1' });
  assert.partialDeepStrictEqual({ a: 1, b: [1, 2, 3], c: { d: 4, e: 5 }, s: new Set([1, 2, 3]) }, { b: [1, 3], c: { e: 5 }, s: new Set([3, 1]) });
  assert.throws(() => assert.partialDeepStrictEqual([1, 2, 3], [3, 1]), { code: 'ERR_ASSERTION' });
  assert.throws(() => { throw new TypeError('bad thing'); }, TypeError);
  assert.throws(() => { throw new TypeError('bad thing'); }, /bad/);
  assert.throws(() => { throw new TypeError('bad thing'); }, { name: 'TypeError', message: /thing$/ });
  assert.throws(() => { throw new Error('x'); }, (err) => err.message === 'x');
  assert.doesNotThrow(() => {});
  assert.match('abc', /b/);
  assert.doesNotMatch('abc', /x/);
  assert.ifError(null);
  await assert.rejects(Promise.reject(new Error('r')), /r/);
  await assert.doesNotReject(async () => 1);
  check('no failures so far', true, true);

  section('Messages');
  check('strictEqual', failure(() => assert.strictEqual(1, 2)),
    { name: 'AssertionError', code: 'ERR_ASSERTION', message: 'Expected values to be strictly equal:\n\n1 !== 2\n', operator: 'strictEqual', generatedMessage: true });
  check('strictEqual of objects', message(() => assert.strictEqual({ a: 1 }, { a: 1 })),
    'Values have same structure but are not reference-equal:\n\n{\n  a: 1\n}\n');
  check('strictEqual of long strings', message(() => assert.strictEqual('aaaaaaaaaaaaaaaaaaaaaa', 'aaaaaaaaaaaaaaaaaaaaab')),
    "Expected values to be strictly equal:\n+ actual - expected\n\n+ 'aaaaaaaaaaaaaaaaaaaaaa'\n- 'aaaaaaaaaaaaaaaaaaaaab'\n                        ^\n");
  check('equal', message(() => assert.equal(1, 2)), '1 == 2');
  check('notStrictEqual', message(() => assert.notStrictEqual(1, 1)), 'Expected "actual" to be strictly unequal to: 1');
  check('deepStrictEqual', message(() => assert.deepStrictEqual({ a: 1, b: 2 }, { a: 1, b: 3 })),
    'Expected values to be strictly deep-equal:\n+ actual - expected\n\n  {\n    a: 1,\n+   b: 2\n-   b: 3\n  }\n');
  check('deepStrictEqual skips lines', message(() => assert.deepStrictEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], [1, 2, 3, 4, 5, 6, 7, 8, 9, 11])),
    'Expected values to be strictly deep-equal:\n+ actual - expected\n... Skipped lines\n\n  [\n    1,\n    2,\n    3,\n    4,\n...\n    9,\n+   10\n-   11\n  ]\n');
  check('deepStrictEqual of nested values', message(() => assert.deepStrictEqual({ a: [1, { b: 2 }] }, { a: [1, { b: '2' }] })),
    "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  {\n    a: [\n      1,\n      {\n+       b: 2\n-       b: '2'\n      }\n    ]\n  }\n");
  check('deepStrictEqual of Maps', message(() => assert.deepStrictEqual(new Map([[1, 2]]), new Map([[1, 3]]))),
    'Expected values to be strictly deep-equal:\n+ actual - expected\n\n  Map(1) {\n+   1 => 2\n-   1 => 3\n  }\n');
  check('deepStrictEqual of a missing item', message(() => assert.deepStrictEqual([1, 2], [1, 2, 3])),
    'Expected values to be strictly deep-equal:\n+ actual - expected\n\n  [\n    1,\n    2,\n-   3\n  ]\n');
  check('deepStrictEqual of primitives', message(() => assert.deepStrictEqual(1, '1')), "Expected values to be strictly deep-equal:\n\n1 !== '1'\n");
  check('deepStrictEqual of prototypes', message(() => assert.deepStrictEqual({ a: 1 }, Object.create(null, { a: { value: 1, enumerable: true } }))),
    'Expected values to be strictly deep-equal:\n+ actual - expected\n\n+ {\n- [Object: null prototype] {\n    a: 1\n  }\n');
  check('deepEqual', message(() => assert.deepEqual({ a: 1 }, { a: 2 })),
    'Expected values to be loosely deep-equal:\n\n{\n  a: 1\n}\n\nshould loosely deep-equal\n\n{\n  a: 2\n}');
  check('notDeepStrictEqual', message(() => assert.notDeepStrictEqual({ a: 1 }, { a: 1 })),
    'Expected "actual" not to be strictly deep-equal to:\n\n{\n  a: 1\n}\n');
  check('ok', [message(() => assert.ok(false)), message(() => assert(null)), message(() => assert.ok())],
    ['The expression evaluated to a falsy value:\n\n  assert.ok(false)\n', 'The expression evaluated to a falsy value:\n\n  assert(null)\n',
     'No value argument passed to `assert.ok()`']);
  check('ok over several lines', message(() => assert.ok(
    typeof 1 === 'string',
    undefined,
  )), "The expression evaluated to a falsy value:\n\n  assert.ok(\n  typeof 1 === 'string',\n  undefined,\n  )\n");
  check('ok in --eval code', require('child_process').execFileSync(process.execPath, ['-e',
    'try { require("assert").ok(0) } catch (e) { console.log(e.message) }']).toString().trim(), '0 == true');
  check('a message of your own', failure(() => assert(0, 'custom')), { name: 'AssertionError', code: 'ERR_ASSERTION', message: 'custom', operator: '==', generatedMessage: false });
  check('an error of your own', failure(() => assert.strictEqual(1, 2, new TypeError('own'))), { name: 'TypeError', code: undefined, message: 'own', operator: undefined, generatedMessage: undefined });
  check('fail', [message(() => assert.fail()), message(() => assert.fail('boom'))], ['Failed', 'boom']);
  check('throws: nothing thrown', message(() => assert.throws(() => {})), 'Missing expected exception.');
  check('throws: RegExp', message(() => assert.throws(() => { throw new Error('x'); }, /y/)),
    "The input did not match the regular expression /y/. Input:\n\n'Error: x'\n");
  check('throws: class', message(() => assert.throws(() => { throw new TypeError('x'); }, RangeError)),
    'The error is expected to be an instance of "RangeError". Received "TypeError"\n\nError message:\n\nx');
  check('throws: object', message(() => assert.throws(() => { throw new Error('x'); }, { message: 'y' })),
    "Expected values to be strictly deep-equal:\n+ actual - expected\n\n  Comparison {\n+   message: 'x'\n-   message: 'y'\n  }\n");
  check('doesNotThrow', message(() => assert.doesNotThrow(() => { throw new Error('x'); })), 'Got unwanted exception.\nActual message: "x"');
  check('match and doesNotMatch', [message(() => assert.match('abc', /x/)), message(() => assert.doesNotMatch('abc', /b/))],
    ["The input did not match the regular expression /x/. Input:\n\n'abc'\n", "The input was expected to not match the regular expression /b/. Input:\n\n'abc'\n"]);
  check('ifError', [message(() => assert.ifError(new Error('e'))), message(() => assert.ifError('str'))],
    ['ifError got unwanted exception: e', "ifError got unwanted exception: 'str'"]);
  let rejected;
  await assert.rejects(Promise.resolve(1)).catch((err) => { rejected = err.message; });
  check('rejects: nothing rejected', rejected, 'Missing expected rejection.');

  section('AssertionError');
  const err = new assert.AssertionError({ actual: 1, expected: 2, operator: 'strictEqual' });
  check('keys', Object.keys(err), ['generatedMessage', 'code', 'actual', 'expected', 'operator']);
  check('toString', String(err).split('\n')[0], 'AssertionError [ERR_ASSERTION]: Expected values to be strictly equal:');
  check('stack', err.stack.split('\n')[0], 'AssertionError [ERR_ASSERTION]: Expected values to be strictly equal:');
  check('inspect', util.inspect(err).split('\n')[0], 'AssertionError [ERR_ASSERTION]: Expected values to be strictly equal:');

  section('assert/strict');
  const strict = require('assert/strict');
  check('the same object', [strict, assert.strict.strict, strict.equal, strict.deepEqual], [assert.strict, assert.strict, assert.strictEqual, assert.deepStrictEqual]);
  check('strict equal', message(() => strict.equal(1, '1')), "Expected values to be strictly equal:\n\n1 !== '1'\n");

  section('CallTracker');
  const tracker = new assert.CallTracker();
  const double = tracker.calls((x) => x * 2, 2);
  check('wraps', double(3), 6);
  check('report', tracker.report().map((item) => item.message), ['Expected the calls function to be executed 2 time(s) but was executed 1 time(s).']);
  double(1);
  tracker.verify();
  check('getCalls', tracker.getCalls(double).map((call) => call.arguments), [[3], [1]]);
});
