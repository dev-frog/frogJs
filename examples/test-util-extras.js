// util additions, inspect and deep-equality fixes, string_decoder, atob/btoa,
// UTF-16LE Buffers and the module aliases: exits with code 1 if a check
// fails. Also runs under Node.js, which is how the expected values were checked.
const util = require('util');
const { StringDecoder } = require('string_decoder');
const { execFileSync } = require('child_process');
const { check, throws, section, run } = require('./lib/check');

const child = (code, env) => execFileSync(process.execPath, ['-e', code], { encoding: 'utf8', env: { ...process.env, ...env } });

run(async () => {
  section('util.types');
  const t = util.types;
  check('names', Object.keys(t).length, 43);
  check('checks', [t.isPromise(Promise.resolve()), t.isDate(new Date()), t.isRegExp(/x/), t.isAsyncFunction(async () => {}),
                   t.isGeneratorFunction(function* () {}), t.isMap(new Map()), t.isSet(new Set()), t.isTypedArray(new Uint8Array()),
                   t.isUint8Array(Buffer.alloc(1)), t.isProxy(new Proxy({}, {})), t.isNativeError(new TypeError()),
                   t.isBoxedPrimitive(Object(1)), t.isAnyArrayBuffer(new SharedArrayBuffer(1)), t.isArrayBufferView(new DataView(new ArrayBuffer(1))),
                   t.isArgumentsObject((function () { return arguments; })())],
    [true, true, true, true, true, true, true, true, true, true, true, true, true, true, true]);
  check('negatives', [t.isPromise({ then() {} }), t.isDate({}), t.isMap(new WeakMap()), t.isKeyObject({}), t.isUint8Array(new Uint16Array())],
    [false, false, false, false, false]);

  section('util functions');
  const cb = util.callbackify(async (fail) => {
    // A falsy rejection is what this checks
    // eslint-disable-next-line no-throw-literal
    if (fail) throw null;
    return 5;
  });
  check('callbackify', await new Promise((resolve) => cb(0, (err, value) => resolve([err, value]))), [null, 5]);
  const falsy = await new Promise((resolve) => cb(1, resolve));
  check('callbackify with a falsy rejection', [falsy.code, falsy.message, falsy.reason], ['ERR_FALSY_VALUE_REJECTION', 'Promise was rejected with falsy value', null]);
  const warnings = child(`const util = require('util'); const f = util.deprecate(() => 1, 'old thing', 'DEP_X'); f(); f();
    util.deprecate(() => 2, 'other', 'DEP_X')(); util.deprecate(() => 3, 'no code')();
    process.on('warning', (w) => console.log(w.name, w.code, w.message));`);
  check('deprecate: once per code', warnings, 'DeprecationWarning DEP_X old thing\nDeprecationWarning undefined no code\n');
  check('deprecate as a constructor', new (util.deprecate(function Old(a) { this.a = a; }, 'x', 'DEP_Y'))(1).a, 1);
  check('debuglog', child(`const d = require('util').debuglog('foo'); d('hi %d', 1); console.log(d.enabled, require('util').debuglog('bar').enabled)`,
    { NODE_DEBUG: 'foo,ba*' }), 'true true\n');
  check('styleText', [util.styleText('red', 'x', { validateStream: false }), util.styleText(['bold', 'none', 'italic'], 'y', { validateStream: false }),
                      util.styleText('red', 'z', { stream: new (require('stream').PassThrough)() })],
    ['\u001b[31mx\u001b[39m', '\u001b[1m\u001b[3my\u001b[23m\u001b[22m', 'z']);
  throws('styleText format', () => util.styleText('nope', 'x'), { code: 'ERR_INVALID_ARG_VALUE', message: /^The argument 'format' must be one of: 'reset', 'bold'/ });
  check('inspect.colors', [Object.keys(util.inspect.colors).length, util.inspect.colors.grey, Object.keys(util.inspect.colors).includes('grey')],
    [44, [90, 39], false]);
  check('stripVTControlCharacters', util.stripVTControlCharacters('\u001b[1;31mred\u001b[0m \u001b]8;;http://x\u0007link\u001b]8;;\u0007 \u001b[2K'), 'red link ');
  check('getSystemErrorName', [util.getSystemErrorName(-2), util.getSystemErrorName(-13)], ['ENOENT', 'EACCES']);
  check('toUSVString', util.toUSVString('a\ud800b'), 'a�b');
  check('TextEncoder and TextDecoder', [util.TextEncoder, util.TextDecoder], [TextEncoder, TextDecoder]);

  section('util.parseArgs');
  const options = { verbose: { type: 'boolean', short: 'v' }, file: { type: 'string', short: 'f', default: 'def' },
                    tag: { type: 'string', multiple: true, short: 't' }, color: { type: 'boolean' } };
  const parse = (args, extra = {}) => {
    const result = util.parseArgs({ args, options, allowPositionals: true, ...extra });
    return { ...result, values: { ...result.values } };
  };
  check('options and positionals', parse(['-v', '--file', 'a.txt', 'pos']), { values: { verbose: true, file: 'a.txt' }, positionals: ['pos'] });
  check('short groups and values', [parse(['-vf', 'x']).values, parse(['-vfFILE']).values, parse(['--file=-x']).values],
    [{ verbose: true, file: 'x' }, { verbose: true, file: 'FILE' }, { file: '-x' }]);
  check('multiple', parse(['-t', 'a', '--tag=b', '-tc']).values, { tag: ['a', 'b', 'c'], file: 'def' });
  check('after --', parse(['--', '-v', 'x']).positionals, ['-v', 'x']);
  check('allowNegative', parse(['--no-color'], { allowNegative: true }).values, { color: false, file: 'def' });
  check('tokens', util.parseArgs({ args: ['-v', 'x'], options, allowPositionals: true, tokens: true }).tokens, [
    { kind: 'option', name: 'verbose', rawName: '-v', index: 0, value: undefined, inlineValue: undefined },
    { kind: 'positional', index: 1, value: 'x' },
  ]);
  check('strict: false', util.parseArgs({ args: ['--nope', '-z'], strict: false }).values, Object.assign(Object.create(null), { nope: true, z: true }));
  throws('unknown option', () => parse(['--nope']), { code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION' });
  throws('missing value', () => parse(['--file']), { code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE', message: "Option '-f, --file <value>' argument missing" });
  throws('ambiguous value', () => parse(['--file', '-x']), { code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE' });
  throws('boolean with a value', () => parse(['--verbose=yes']), { message: "Option '-v, --verbose' does not take an argument" });
  throws('positionals not allowed', () => util.parseArgs({ args: ['x'], options }), { code: 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL' });
  check('args default to the script\'s', child(`console.log(JSON.stringify(require('util').parseArgs({ strict: false }).positionals))`).trim(), '[]');

  section('inspect');
  class MyError extends Error {}
  check('error subclasses', util.inspect(new MyError('x')).split('\n')[0], 'MyError: x');
  check('names that differ', util.inspect(new DOMException('m', 'AbortError')).split('\n')[0], 'DOMException [AbortError]: m');
  const noStack = new MyError('ns');
  noStack.stack = undefined;
  check('errors without a stack', util.inspect(noStack), '[MyError: ns]');
  check('getters', util.inspect({ get a() { return 1; }, get b() { return { c: 2 }; } }, { getters: true }), '{ a: [Getter: 1], b: [Getter] { c: 2 } }');

  section('isDeepStrictEqual');
  const same = util.isDeepStrictEqual;
  check('Maps and Sets', [same(new Map([[1, 2]]), new Map([[1, 3]])), same(new Set([1]), new Set([2])), same(new Set([{ a: 1 }]), new Set([{ a: 1 }])),
                          same(new Map([[{ k: 1 }, 'v']]), new Map([[{ k: 1 }, 'v']]))], [false, false, true, true]);
  check('Dates and RegExps', [same(new Date(1), new Date(2)), same(/a/g, /a/i), same(new Date(1), new Date(1))], [false, false, true]);
  check('typed arrays and boxed values', [same(new Uint8Array([1]), new Uint8Array([2])), same(Object(1), Object(2)), same(new Float64Array([NaN]), new Float64Array([NaN]))],
    [false, false, true]);
  check('Errors and symbols', [same(new Error('a'), new Error('b')), same({ [Symbol.for('s')]: 1 }, { [Symbol.for('s')]: 2 })], [false, false]);
  const cycleA = { self: null };
  cycleA.self = cycleA;
  const cycleB = { self: null };
  cycleB.self = cycleB;
  check('cycles', same(cycleA, cycleB), true);

  section('string_decoder');
  const bytes = Buffer.from('€💩é');
  const chunks = [];
  for (let split = 0; split <= bytes.length; split++) {
    const decoder = new StringDecoder('utf8');
    chunks.push(decoder.write(bytes.subarray(0, split)) + '|' + decoder.write(bytes.subarray(split)) + decoder.end());
  }
  check('UTF-8 split anywhere', chunks.every((text) => text.replace('|', '') === '€💩é'), true);
  check('UTF-8 split in the middle', chunks[2], '|€💩é');
  const decoder = new StringDecoder('utf8');
  check('incomplete and invalid', [decoder.write(Buffer.from([0xe2, 0x82])), decoder.end(), decoder.write(Buffer.from([0xff, 0x41]))], ['', '�', '�A']);
  const utf16 = new StringDecoder('utf16le');
  const pair = Buffer.from('💩', 'utf16le');
  check('UTF-16LE surrogate pairs', [utf16.write(pair.subarray(0, 2)), utf16.end(pair.subarray(2))], ['', '💩']);
  // Node.js splits the pair when the chunk also ends in the middle of a code unit
  check('UTF-16LE odd split', [new StringDecoder('utf16le').write(pair.subarray(0, 3))], ['\ud83d']);
  const base64 = new StringDecoder('base64');
  check('base64 groups', [base64.write(Buffer.from('ab')), base64.write(Buffer.from('c')), base64.end()], ['', 'YWJj', '']);
  check('encodings', [new StringDecoder().encoding, new StringDecoder('UCS-2').encoding, new StringDecoder('binary').encoding], ['utf8', 'utf16le', 'latin1']);
  throws('unknown encoding', () => new StringDecoder('nope'), { code: 'ERR_UNKNOWN_ENCODING' });

  section('atob and btoa');
  check('round trip', [atob(' aGVs bG8= '), btoa('héllo'), atob('YQ'), atob(btoa('ÿ\u0000'))], ['hello', 'aOlsbG8=', 'a', 'ÿ\u0000']);
  check('in buffer', [require('buffer').atob, require('buffer').btoa], [atob, btoa]);
  throws('bad length', () => atob('abcde'), { name: 'InvalidCharacterError', message: 'The string to be decoded is not correctly encoded.' });
  throws('bad character', () => atob('ab!c'), { name: 'InvalidCharacterError', message: 'Invalid character' });
  throws('outside Latin-1', () => btoa('€'), { name: 'InvalidCharacterError' });

  section('UTF-16LE Buffers');
  const wide = Buffer.from('h€💩', 'utf16le');
  check('encode and decode', [wide.length, wide.toString('ucs2'), Buffer.byteLength('h€', 'utf-16le'), Buffer.isEncoding('UCS-2')], [8, 'h€💩', 4, true]);
  const target = Buffer.alloc(5);
  check('write whole code units', [target.write('abc', 'utf16le'), [...target]], [4, [97, 0, 98, 0, 0]]);
  check('odd byte dropped', Buffer.from([0x41, 0, 0x42]).toString('utf16le'), 'A');

  section('Module aliases');
  check('fs/promises', require('fs/promises'), require('fs').promises);
  check('stream/promises', require('stream/promises'), require('stream').promises);
  check('path/posix', require('path/posix'), require('path'));
  check('timers/promises', require('timers/promises'), require('timers').promises);
  check('perf_hooks', typeof require('perf_hooks').performance.now(), 'number');
  check('node: names', [require('node:fs/promises'), require('node:url').URL], [require('fs').promises, URL]);

  section('string_decoder and querystring edge cases (expected values from Node.js 24)');
  const sdQs = require('querystring');
  const decodeChunks = (enc, chunks, endChunk) => { const d = new StringDecoder(enc); const parts = chunks.map((c) => d.write(Buffer.from(c))); parts.push(endChunk === undefined ? d.end() : d.end(Buffer.from(endChunk))); return parts; };
  check("utf8Invalid", decodeChunks('utf8', [[0xe2, 0x41], [0xf0, 0x9f, 0x41], [0xc3]]), ["\ufffdA", "\ufffdA", "", "\ufffd"]);
  check("utf8Byte", decodeChunks('utf8', [[0xf0], [0x9f], [0x98], [0x80]]), ["", "", "", "\ud83d\ude00", ""]);
  check("utf8Split3", decodeChunks('utf8', [[0xf0, 0x9f], [0x98], [0x80, 0x41]]), ["", "", "\ud83d\ude00A", ""]);
  check("utf8EndPartial", decodeChunks('utf8', [[0xe2, 0x82]]), ["", "\ufffd"]);
  check("utf16", decodeChunks('utf16le', [[0x3d], [0xd8, 0x00], [0xde, 0x41, 0x00]]), ["", "\ud83d", "\ude00A", ""]);
  check("utf16End", decodeChunks('utf16le', [[0x3d, 0xd8]]), ["", "\ud83d"]);
  check("utf16Odd", decodeChunks('utf16le', [[0x41]], [0x00]), ["", "A"]);
  check("base64", decodeChunks('base64', [[1], [2, 3, 4], [5]]), ["", "AQID", "", "BAU="]);
  check("base64End", decodeChunks('base64', [[1, 2]]), ["", "AQI="]);
  check("latin1", decodeChunks('latin1', [[0xe9], [0x41]], [0x42]), ["\u00e9", "A", "B"]);
  check("hex", decodeChunks('hex', [[0xab]], []), ["ab", ""]);
  const sd = new StringDecoder('utf8');
  check("string", sd.write('already text'), "already text");
  check("view", sd.write(new Uint8Array([0x68, 0x69])), "hi");
  check("dataView", new StringDecoder('utf8').write(new DataView(new Uint8Array([0x6f, 0x6b]).buffer)), "ok");
  check("endEmpty", new StringDecoder('latin1').end(), "");
  check("qsPlus", sdQs.unescapeBuffer('a+b%20c', true).toString(), "a b c");
  check("qsPlusNo", sdQs.unescapeBuffer('a+b', false).toString(), "a+b");
  check("qsMalformed", { ...sdQs.parse('a=%E0%A4%A&b=%ZZ&c=%') }, {"a": "\ufffd%A", "b": "%ZZ", "c": "%"});
  check("qsObj", sdQs.escape({ toString() { return 'x y'; } }), "x%20y");
  check("qsNum", sdQs.escape(12), "12");
  throws("qsLone", () => { sdQs.escape('\uD800'); }, { name: "URIError", code: "ERR_INVALID_URI", message: "URI malformed" });
  check("qsDecodeFn", { ...sdQs.parse('a=1', null, null, { decodeURIComponent: (s) => s.toUpperCase() }) }, {"A": "1"});
});
