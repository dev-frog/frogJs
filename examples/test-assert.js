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

  section('Edge cases and errors (the expected values are what Node.js 24 gives)');
  // [name, code, the first three lines of the message], or 'no throw'
  const summary = (e) => [e.name, e.code, String(e.message).split('\n').slice(0, 3).join('|')];
  const outcome = (fn) => { try { fn(); return 'no throw'; } catch (e) { return summary(e); } };
  const outcomeAsync = async (promise) => { try { await promise; return 'no reject'; } catch (e) { return summary(e); } };
  check("missing", outcome(() => assert.equal(1)), ["TypeError", "ERR_MISSING_ARGS", "The \"actual\" and \"expected\" arguments must be specified"]);
  check("missing2", outcome(() => assert.notDeepEqual(1)), ["TypeError", "ERR_MISSING_ARGS", "The \"actual\" and \"expected\" arguments must be specified"]);
  check("notEqual", outcome(() => assert.notEqual(1, '1')), ["AssertionError", "ERR_ASSERTION", "1 != '1'"]);
  check("notDeepEqual", outcome(() => assert.notDeepEqual({ a: [1] }, { a: ['1'] })), ["AssertionError", "ERR_ASSERTION", "Expected values not to be loosely deep-equal:||{"]);
  check("notDeepEqualOk", outcome(() => assert.notDeepEqual({ a: 1 }, { a: 2 })), "no throw");
  check("looseMap", outcome(() => assert.deepEqual(new Map([[1, { a: 1 }]]), new Map([[1, { a: '1' }]]))), "no throw");
  check("looseMapKeys", outcome(() => assert.deepEqual(new Map([[{ k: 1 }, 1]]), new Map([[{ k: 1 }, 1]]))), "no throw");
  check("looseMapBad", outcome(() => assert.deepEqual(new Map([[1, 1]]), new Map([[2, 1]]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||Map(1) {"]);
  check("looseSet", outcome(() => assert.deepEqual(new Set([{ a: 1 }]), new Set([{ a: 1 }]))), "no throw");
  check("looseSetBad", outcome(() => assert.deepEqual(new Set([1]), new Set([2]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||Set(1) {"]);
  check("looseSetMapMix", outcome(() => assert.deepEqual(new Set([1]), new Map([[1, 1]]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||Set(1) {"]);
  check("looseDate", outcome(() => assert.deepEqual(new Date(1), new Date(2))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||1970-01-01T00:00:00.001Z"]);
  check("looseRe", outcome(() => assert.deepEqual(/a/g, /a/i)), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||/a/g"]);
  check("looseErr", outcome(() => assert.deepEqual(new Error('a'), new Error('b'))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||[Error: a]"]);
  check("looseTyped", outcome(() => assert.deepEqual(new Uint8Array([1, 2]), new Uint8Array([1, 3]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||Uint8Array(2) ["]);
  check("looseTypedLen", outcome(() => assert.deepEqual(new Uint8Array([1]), new Uint8Array([1, 2]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||Uint8Array(1) ["]);
  check("looseArrObj", outcome(() => assert.deepEqual([1], { 0: 1 })), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||["]);
  check("loosePrimObj", outcome(() => assert.deepEqual(1, { a: 1 })), ["AssertionError", "ERR_ASSERTION", "Expected values to be loosely deep-equal:||1"]);
  check("throwsBadErr", outcome(() => assert.throws(() => { throw new Error('x'); }, 5)), ["TypeError", "ERR_INVALID_ARG_TYPE", "The \"error\" argument must be of type function or an instance of Error, RegExp, or Object. Received type number (5)"]);
  check("throwsEmptyObj", outcome(() => assert.throws(() => { throw new Error('x'); }, {})), ["TypeError", "ERR_INVALID_ARG_VALUE", "The argument 'error' may not be an empty object. Received {}"]);
  check("throwsAmbig", outcome(() => assert.throws(() => { throw new Error('same'); }, 'same')), ["TypeError", "ERR_AMBIGUOUS_ARGUMENT", "The \"error/message\" argument is ambiguous. The error message \"same\" is identical to the message."]);
  check("throwsAmbig2", outcome(() => { const e = 'thing'; assert.throws(() => { throw e; }, e); }), ["TypeError", "ERR_AMBIGUOUS_ARGUMENT", "The \"error/message\" argument is ambiguous. The error \"thing\" is identical to the message."]);
  check("throwsMsg", outcome(() => assert.throws(() => {}, 'custom message')), ["AssertionError", "ERR_ASSERTION", "Missing expected exception: custom message"]);
  check("throwsValidate", outcome(() => assert.throws(() => { throw new Error('v'); }, function check() { return false; })), ["AssertionError", "ERR_ASSERTION", "The \"check\" validation function is expected to return \"true\". Received false||Caught error:"]);
  check("throwsValidateArrow", outcome(() => assert.throws(() => { throw new Error('v'); }, () => false)), ["AssertionError", "ERR_ASSERTION", "The validation function is expected to return \"true\". Received false||Caught error:"]);
  check("throwsSameName", outcome(() => { assert.throws(() => { throw new TypeError('x'); }, RangeError); }), ["AssertionError", "ERR_ASSERTION", "The error is expected to be an instance of \"RangeError\". Received \"TypeError\"||Error message:"]);
  check("throwsObjMismatch", outcome(() => assert.throws(() => { throw new Error('x'); }, { message: 'y' })), ["AssertionError", "ERR_ASSERTION", "Expected values to be strictly deep-equal:|+ actual - expected|"]);
  check("throwsFourArgs", outcome(() => assert.throws(() => {}, 'a', 'b', 'c')), ["AssertionError", "ERR_ASSERTION", "Missing expected exception: a"]);
  check("doesNotThrowMatch", outcome(() => assert.doesNotThrow(() => { throw new TypeError('t'); }, TypeError)), ["AssertionError", "ERR_ASSERTION", "Got unwanted exception.|Actual message: \"t\""]);
  check("doesNotThrowOther", outcome(() => assert.doesNotThrow(() => { throw new TypeError('t'); }, RangeError)), ["TypeError", undefined, "t"]);
  check("doesNotThrowRegExp", outcome(() => assert.doesNotThrow(() => { throw new Error('abc'); }, /abc/)), ["AssertionError", "ERR_ASSERTION", "Got unwanted exception.|Actual message: \"abc\""]);
  check("doesNotThrowMsg", outcome(() => assert.doesNotThrow(() => { throw new Error('abc'); }, 'oops')), ["AssertionError", "ERR_ASSERTION", "Got unwanted exception: oops|Actual message: \"abc\""]);
  check("doesNotThrowBadExpected", outcome(() => assert.doesNotThrow(() => { throw new Error('abc'); }, {})), ["TypeError", "ERR_INVALID_ARG_TYPE", "The \"expected\" argument must be of type function or an instance of RegExp. Received an instance of Object"]);
  check("doesNotThrowFn", outcome(() => assert.doesNotThrow(() => { throw new Error('abc'); }, (e) => e.message === 'abc')), ["AssertionError", "ERR_ASSERTION", "Got unwanted exception.|Actual message: \"abc\""]);
  check("rejectsNotPromise", await outcomeAsync(assert.rejects(() => 5)), ["TypeError", "ERR_INVALID_RETURN_VALUE", "Expected instance of Promise to be returned from the \"promiseFn\" function but got type number (5)."]);
  check("rejectsBadArg", await outcomeAsync(assert.rejects(5)), ["TypeError", "ERR_INVALID_ARG_TYPE", "The \"promiseFn\" argument must be of type function or an instance of Promise. Received type number (5)"]);
  check("doesNotReject", await outcomeAsync(assert.doesNotReject(Promise.reject(new TypeError('r')), TypeError)), ["AssertionError", "ERR_ASSERTION", "Got unwanted rejection.|Actual message: \"r\""]);
  check("doesNotRejectOk", await outcomeAsync(assert.doesNotReject(Promise.resolve(1))), "no reject");
  check("ifError", outcome(() => assert.ifError(new Error('boom'))), ["AssertionError", "ERR_ASSERTION", "ifError got unwanted exception: boom"]);
  check("ifErrorStr", outcome(() => assert.ifError('text')), ["AssertionError", "ERR_ASSERTION", "ifError got unwanted exception: 'text'"]);
  check("okMessageErr", outcome(() => assert.ok(false, new RangeError('mine'))), ["RangeError", undefined, "mine"]);
  check("okNoArgs", outcome(() => assert.ok()), ["AssertionError", "ERR_ASSERTION", "No value argument passed to `assert.ok()`"]);
  check("strictFn", outcome(() => assert.strict(0)), ["AssertionError", "ERR_ASSERTION", "The expression evaluated to a falsy value:||  assert.strict(0)"]);
  check("equalOperator", outcome(() => assert.notEqual(1, 1)), ["AssertionError", "ERR_ASSERTION", "1 != 1"]);
  check("partialMap", outcome(() => assert.partialDeepStrictEqual(new Map([[1, 1], [2, 2]]), new Map([[1, 1]]))), "no throw");
  check("partialMapBad", outcome(() => assert.partialDeepStrictEqual(new Map([[1, 1]]), new Map([[1, 2]]))), ["AssertionError", "ERR_ASSERTION", "Expected values to be partially and strictly deep-equal:|+ actual - expected|"]);
  check("longDiff", outcome(() => assert.deepStrictEqual(Array.from({ length: 80 }, (_, i) => i), Array.from({ length: 80 }, (_, i) => i + 1))), ["AssertionError", "ERR_ASSERTION", "Expected values to be strictly deep-equal:|+ actual - expected|... Skipped lines"]);
  check("longNotStrict", outcome(() => { const o = Object.fromEntries(Array.from({ length: 60 }, (_, i) => ['k' + i, i])); assert.notDeepStrictEqual(o, { ...o }); }), ["AssertionError", "ERR_ASSERTION", "Expected \"actual\" not to be strictly deep-equal to:||{"]);
  check("bigMsg", outcome(() => assert.strictEqual('x'.repeat(2000), 'y'.repeat(2000))).slice(0, 2), ["AssertionError", "ERR_ASSERTION"]);
  check("notStrictObj", outcome(() => { const o = { a: 1 }; assert.notStrictEqual(o, o); }), ["AssertionError", "ERR_ASSERTION", "Expected \"actual\" not to be reference-equal to \"expected\":||{"]);

  section('A custom message (Node.js 24 adds the diff for strict comparisons)');
  const messageOf = (fn) => { try { fn(); } catch (e) { return e.message; } };
  check("custom message: strictEqual", messageOf(() => assert.strictEqual(1, 2, 'm')), "m\n\n1 !== 2\n");
  check("custom message: deepStrictEqual", messageOf(() => assert.deepStrictEqual({ a: 1 }, { a: 2 }, 'm')), "m\n+ actual - expected\n\n  {\n+   a: 1\n-   a: 2\n  }\n");
  check("custom message: partial", messageOf(() => assert.partialDeepStrictEqual({ a: 1 }, { a: 2 }, 'm')), "m\n+ actual - expected\n\n  {\n+   a: 1\n-   a: 2\n  }\n");
  check("custom message: equal", messageOf(() => assert.equal(1, 2, 'm')), "m");
  check("custom message: notStrictEqual", messageOf(() => assert.notStrictEqual(1, 1, 'm')), "m");
  check("custom message: deepEqual", messageOf(() => assert.deepEqual({ a: 1 }, { a: 2 }, 'm')), "m");
  check("custom message: ctorStrict", new assert.AssertionError({ message: 'm', actual: 'a', expected: 'b', operator: 'strictEqual' }).message, "m\n\n'a' !== 'b'\n");
  check("custom message: ctorDeep", new assert.AssertionError({ message: 'm', actual: [1], expected: [2], operator: 'deepStrictEqual' }).message, "m\n+ actual - expected\n\n  [\n+   1\n-   2\n  ]\n");
  check("custom message: ctorPartial", new assert.AssertionError({ message: 'm', actual: [1], expected: [2], operator: 'partialDeepStrictEqual' }).message, "m\n+ actual - expected\n\n  [\n+   1\n-   2\n  ]\n");
  check("custom message: ctorEqual", new assert.AssertionError({ message: 'm', actual: 1, expected: 2, operator: '==' }).message, "m");
  check("custom message: errorMsg", messageOf(() => assert.strictEqual(1, 2, new TypeError('own'))), "own");

  section('CallTracker.reset');
  const resetTracker = new assert.CallTracker();
  const noop = resetTracker.calls(1);
  noop();
  resetTracker.reset(noop);
  check('reset(fn)', resetTracker.report().map((r) => r.message), ['Expected the calls function to be executed 1 time(s) but was executed 0 time(s).']);
  const counted = resetTracker.calls((x) => x);
  counted(1);
  resetTracker.reset();
  check('reset()', resetTracker.getCalls(counted).length, 0);
  const copiedA = new Error('a');
  const copiedB = new Error('b');
  const copied = outcome(() => assert.deepEqual(copiedA, copiedB));
  check('errors are shown without stacks', copied, ['AssertionError', 'ERR_ASSERTION', 'Expected values to be loosely deep-equal:||[Error: a]']);
});
