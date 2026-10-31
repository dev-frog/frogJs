// Util module self-test: exits with code 1 if any check fails.
// The same file runs under Node.js, which is how the expected values were checked.
const util = require('util');
const { inspect, format } = util;

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

console.log('inspect: primitives');
check('number', inspect(42), '42');
check('negative zero', inspect(-0), '-0');
check('bigint', inspect(10n), '10n');
check('string is quoted', inspect('hi'), "'hi'");
check('string with single quote uses double quotes', inspect("it's"), '"it\'s"');
check('string escapes', inspect('a\nb\tc'), "'a\\nb\\tc'");
check('symbol', inspect(Symbol('s')), 'Symbol(s)');
check('null and undefined', `${inspect(null)} ${inspect(undefined)}`, 'null undefined');

console.log('inspect: objects');
check('empty object', inspect({}), '{}');
check('object', inspect({ a: 1, 'b-c': 'x' }), "{ a: 1, 'b-c': 'x' }");
check('nested beyond depth', inspect({ a: { b: { c: { d: 1 } } } }), '{ a: { b: { c: [Object] } } }');
check('depth option', inspect({ a: { b: 1 } }, { depth: 0 }), '{ a: [Object] }');
check('depth: null', inspect({ a: { b: { c: { d: 1 } } } }, { depth: null }), '{\n  a: { b: { c: { d: 1 } } }\n}');
check('long object breaks lines', inspect({ first: 'a'.repeat(30), second: 'b'.repeat(30) }),
    "{\n  first: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',\n  second: 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'\n}");
check('symbol key', inspect({ [Symbol('k')]: 1 }), '{ Symbol(k): 1 }');
check('getter', inspect(Object.defineProperty({}, 'g', { get() { return 1; }, enumerable: true })), '{ g: [Getter] }');
check('null prototype', inspect(Object.assign(Object.create(null), { k: 1 })), '[Object: null prototype] { k: 1 }');

const circular = { name: 'c' };
circular.self = circular;
check('circular reference', inspect(circular), "<ref *1> { name: 'c', self: [Circular *1] }");

class Point {
    constructor() { this.x = 1; this.y = 2; }
}
check('class instance', inspect(new Point()), 'Point { x: 1, y: 2 }');
check('custom inspect', inspect({ [inspect.custom]() { return 'custom!'; } }), 'custom!');

console.log('inspect: arrays and collections');
check('array', inspect([1, 'a', null]), "[ 1, 'a', null ]");
check('sparse array', inspect([1, , 3]), '[ 1, <1 empty item>, 3 ]');
check('long number array is grouped', inspect([1, 2, 3, 4, 5, 6, 7]), '[\n  1, 2, 3, 4,\n  5, 6, 7\n]');
check('more than 100 items', inspect(new Array(101).fill(0)).endsWith('... 1 more item\n]'), true);
check('Map', inspect(new Map([['a', 1]])), "Map(1) { 'a' => 1 }");
check('Set', inspect(new Set([1, 2])), 'Set(2) { 1, 2 }');
check('typed array', inspect(new Uint8Array([1, 2])), 'Uint8Array(2) [ 1, 2 ]');

console.log('inspect: other built-ins');
check('function', inspect(function named() {}), '[Function: named]');
check('anonymous arrow', inspect(() => {}), '[Function (anonymous)]');
check('class', inspect(Point), '[class Point]');
check('async function', inspect(async function run() {}), '[AsyncFunction: run]');
check('date', inspect(new Date(0)), '1970-01-01T00:00:00.000Z');
check('regexp', inspect(/a+b/g), '/a+b/g');
check('boxed number', inspect(new Number(3)), '[Number: 3]');
check('resolved promise', inspect(Promise.resolve(5)), 'Promise { 5 }');
check('pending promise', inspect(new Promise(() => {})), 'Promise { <pending> }');
check('proxy shows its target', inspect(new Proxy({ t: 1 }, {})), '{ t: 1 }');
check('error shows its stack', inspect(new Error('boom')).split('\n')[0], 'Error: boom');
const withCode = new Error('boom');
withCode.code = 'E_BOOM';
check('error with extra property', inspect(withCode).endsWith("{\n  code: 'E_BOOM'\n}"), true);
check('colors', inspect(1, { colors: true }), '\u001b[33m1\u001b[39m');

console.log('format');
check('plain strings', format('a', 'b'), 'a b');
check('%s %d %i %f', format('%s=%d %i %f', 'n', 42.5, 42.5, '1.5'), 'n=42.5 42 1.5');
check('%j', format('%j', { a: 1 }), '{"a":1}');
check('%o and %O', format('%O', { a: 1 }), '{ a: 1 }');
check('%%', format('100%% %s', 'done'), '100% done');
check('%c is consumed', format('%cstyled', 'color: red'), 'styled');
check('extra arguments are appended', format('%s', 'a', { b: 1 }), 'a { b: 1 }');
check('non-string first argument', format(1, [2]), '1 [ 2 ]');

console.log('helpers');
check('isDeepStrictEqual', util.isDeepStrictEqual({ a: [1] }, { a: [1] }) && !util.isDeepStrictEqual({ a: 1 }, { a: '1' }), true);
function Parent() {}
function Child() {}
util.inherits(Child, Parent);
check('inherits', new Child() instanceof Parent && Child.super_ === Parent, true);

const promisified = util.promisify((value, callback) => setTimeout(() => callback(null, value * 2), 1));
promisified(21).then((value) => {
    check('promisify', value, 42);
    console.log(`\n${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
});
