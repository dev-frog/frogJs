#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The assert module. Error messages follow Node.js's: values are shown with
// util.inspect, and deep comparisons print a line diff (Myers' algorithm,
// with long runs of equal lines collapsed). Messages are never colored.
static const char* kAssertSource = R"JS(
(function (util, fs, url) {
'use strict';

const { inspect, isDeepStrictEqual } = util;

const kReadableOperator = {
  deepStrictEqual: 'Expected values to be strictly deep-equal:',
  partialDeepStrictEqual: 'Expected values to be partially and strictly deep-equal:',
  strictEqual: 'Expected values to be strictly equal:',
  strictEqualObject: 'Expected "actual" to be reference-equal to "expected":',
  deepEqual: 'Expected values to be loosely deep-equal:',
  notDeepStrictEqual: 'Expected "actual" not to be strictly deep-equal to:',
  notStrictEqual: 'Expected "actual" to be strictly unequal to:',
  notStrictEqualObject: 'Expected "actual" not to be reference-equal to "expected":',
  notDeepEqual: 'Expected "actual" not to be loosely deep-equal to:',
  notIdentical: 'Values have same structure but are not reference-equal:',
  notDeepEqualUnequal: 'Expected values not to be loosely deep-equal:',
};

const kMaxShortStringLength = 12;
const kMaxLongStringLength = 512;
const kNopLinesToCollapse = 5;

function inspectValue(value) {
  return inspect(value, {
    compact: false, customInspect: false, depth: 1000, maxArrayLength: Infinity,
    showHidden: false, showProxy: false, sorted: true, getters: true,
  });
}

function codeError(Base, code, message) {
  const err = new Base(message);
  err.code = code;
  return err;
}

// How Node.js's errors describe a value they didn't expect
function describeType(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return `function ${value.name}`;
  if (typeof value === 'object') {
    return value.constructor && value.constructor.name ? `an instance of ${value.constructor.name}` : inspect(value, { depth: -1 });
  }
  return `type ${typeof value} (${inspect(value)})`;
}

function invalidArgType(name, expected, value) {
  return codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. Received ${describeType(value)}`);
}

// A copy of an error without its stack, so that messages show [Error: a]
function copyError(source) {
  const target = Object.assign(Object.create(Object.getPrototypeOf(source)), source);
  Object.defineProperty(target, 'message', { value: source.message });
  if (source.cause !== undefined) {
    const cause = source.cause instanceof Error ? copyError(source.cause) : source.cause;
    Object.defineProperty(target, 'cause', { value: cause });
  }
  return target;
}

// ---- The line diff ----

function areLinesEqual(actual, expected, checkCommaDisparity) {
  if (actual === expected) return true;
  if (checkCommaDisparity) return `${actual},` === expected || actual === `${expected},`;
  return false;
}

function backtrack(trace, actual, expected, checkCommaDisparity) {
  const max = actual.length + expected.length;
  let x = actual.length;
  let y = expected.length;
  const result = [];
  for (let level = trace.length - 1; level >= 0; level--) {
    const v = trace[level];
    const diagonal = x - y;
    const offset = diagonal + max;
    const prevDiagonal = diagonal === -level || (diagonal !== level && v[offset - 1] < v[offset + 1]) ? diagonal + 1 : diagonal - 1;
    const prevX = v[prevDiagonal + max];
    const prevY = prevX - prevDiagonal;
    while (x > prevX && y > prevY) {
      const actualItem = actual[x - 1];
      const value = checkCommaDisparity && !actualItem.endsWith(',') ? expected[y - 1] : actualItem;
      result.push({ type: 'nop', value });
      x--;
      y--;
    }
    if (level > 0) {
      if (x > prevX) {
        result.push({ type: 'insertion', value: actual[x - 1] });
        x--;
      } else {
        result.push({ type: 'deletion', value: expected[y - 1] });
        y--;
      }
    }
  }
  return result;
}

// The edit script from expected to actual, last line first
function myersDiff(actual, expected, checkCommaDisparity) {
  const max = actual.length + expected.length;
  const v = new Int32Array(2 * max + 1);
  const trace = [];
  for (let level = 0; level <= max; level++) {
    trace.push(new Int32Array(v));
    for (let diagonal = -level; diagonal <= level; diagonal += 2) {
      const offset = diagonal + max;
      let x = diagonal === -level || (diagonal !== level && v[offset - 1] < v[offset + 1]) ? v[offset + 1] : v[offset - 1] + 1;
      let y = x - diagonal;
      while (x < actual.length && y < expected.length && areLinesEqual(actual[x], expected[y], checkCommaDisparity)) {
        x++;
        y++;
      }
      v[offset] = x;
      if (x >= actual.length && y >= expected.length) return backtrack(trace, actual, expected, checkCommaDisparity);
    }
  }
  return [];
}

function printMyersDiff(diff) {
  let message = '';
  let skipped = false;
  let nopCount = 0;
  for (let i = diff.length - 1; i >= 0; i--) {
    const { type, value } = diff[i];
    const previousType = i < diff.length - 1 ? diff[i + 1].type : null;
    // Unchanged lines before a change: the last one or two, or "..." and the last
    if (previousType === 'nop' && type !== previousType) {
      if (nopCount === kNopLinesToCollapse + 1) {
        message += `  ${diff[i + 1].value}\n`;
      } else if (nopCount === kNopLinesToCollapse + 2) {
        message += `  ${diff[i + 2].value}\n`;
        message += `  ${diff[i + 1].value}\n`;
      } else if (nopCount >= kNopLinesToCollapse + 3) {
        message += '...\n';
        message += `  ${diff[i + 1].value}\n`;
        skipped = true;
      }
      nopCount = 0;
    }
    if (type === 'insertion') {
      message += `+ ${value}\n`;
    } else if (type === 'deletion') {
      message += `- ${value}\n`;
    } else {
      if (nopCount < kNopLinesToCollapse) message += `  ${value}\n`;
      nopCount++;
    }
  }
  return { message: `\n${message.trimEnd()}`, skipped };
}

function getStackedDiff(actual, expected) {
  let message = `\n+ ${actual}\n- ${expected}`;
  if (actual.length + expected.length <= 80) {
    let indicator = -1;
    for (let i = 0; i < actual.length; i++) {
      if (actual[i] !== expected[i]) {
        if (i >= 3) indicator = i;
        break;
      }
    }
    if (indicator === -1 && actual.length !== expected.length) indicator = actual.length;
    if (indicator !== -1) message += `\n${' '.repeat(indicator + 2)}^`;
  }
  return message;
}

function getSimpleDiff(originalActual, actual, originalExpected, expected) {
  let length = actual.length + expected.length;
  if (typeof originalActual === 'string') length -= 2;
  if (typeof originalExpected === 'string') length -= 2;
  if (length <= kMaxShortStringLength && (originalActual !== 0 || originalExpected !== 0)) {
    return { message: `${actual} !== ${expected}`, header: '' };
  }
  return { message: getStackedDiff(actual, expected) };
}

function isSimpleDiff(actual, inspectedActual, expected, inspectedExpected) {
  if (inspectedActual.length > 1 || inspectedExpected.length > 1) return false;
  return typeof actual !== 'object' || actual === null || typeof expected !== 'object' || expected === null;
}

function createErrDiff(actual, expected, operator, customMessage) {
  if (operator === 'strictEqual' && ((typeof actual === 'object' && actual !== null && typeof expected === 'object' && expected !== null) ||
                                     (typeof actual === 'function' && typeof expected === 'function'))) {
    operator = 'strictEqualObject';
  }
  let skipped = false;
  let message;
  const inspectedActual = inspectValue(actual);
  const inspectedExpected = inspectValue(expected);
  const splitActual = inspectedActual.split('\n');
  const splitExpected = inspectedExpected.split('\n');
  let header = '+ actual - expected';

  if (isSimpleDiff(actual, splitActual, expected, splitExpected)) {
    const simple = getSimpleDiff(actual, splitActual[0], expected, splitExpected[0]);
    message = simple.message;
    if (simple.header !== undefined) header = simple.header;
  } else if (inspectedActual === inspectedExpected) {
    // Equal structure, different objects
    operator = 'notIdentical';
    if (splitActual.length > 50) {
      message = `${splitActual.slice(0, 50).join('\n')}\n...}`;
      skipped = true;
    } else {
      message = splitActual.join('\n');
    }
    header = '';
  } else {
    const diff = myersDiff(splitActual, splitExpected, actual != null && typeof actual === 'object');
    const printed = printMyersDiff(diff);
    message = printed.message;
    skipped = printed.skipped;
  }
  const headerMessage = `${customMessage || kReadableOperator[operator]}\n${header}`;
  return `${headerMessage}${skipped ? '\n... Skipped lines' : ''}\n${message}\n`;
}

// ---- AssertionError ----

class AssertionError extends Error {
  constructor(options) {
    if (options === null || typeof options !== 'object') throw invalidArgType('options', 'of type object', options);
    const { message, operator, stackStartFn } = options;
    let { actual, expected } = options;
    // Errors are shown without their stacks, and kept as those copies (as in Node.js)
    if (actual instanceof Error && 'stack' in actual && expected instanceof Error && 'stack' in expected) {
      actual = copyError(actual);
      expected = copyError(expected);
    }
    if (message != null && (operator === 'deepStrictEqual' || operator === 'strictEqual' || operator === 'partialDeepStrictEqual')) {
      // As in Node.js 24, a message of one's own is followed by the diff
      super(createErrDiff(actual, expected, operator, String(message)));
    } else if (message != null) {
      super(String(message));
    } else if (operator === 'deepStrictEqual' || operator === 'strictEqual' || operator === 'partialDeepStrictEqual') {
      super(createErrDiff(actual, expected, operator));
    } else if (operator === 'notDeepStrictEqual' || operator === 'notStrictEqual') {
      let base = kReadableOperator[operator];
      const lines = inspectValue(actual).split('\n');
      if (operator === 'notStrictEqual' && ((typeof actual === 'object' && actual !== null) || typeof actual === 'function')) {
        base = kReadableOperator.notStrictEqualObject;
      }
      if (lines.length > 50) {
        lines[46] = '...';
        while (lines.length > 47) lines.pop();
      }
      if (lines.length === 1) super(`${base}${lines[0].length > 5 ? '\n\n' : ' '}${lines[0]}`);
      else super(`${base}\n\n${lines.join('\n')}\n`);
    } else {
      let res = inspectValue(actual);
      let other = inspectValue(expected);
      const known = kReadableOperator[operator];
      if (operator === 'notDeepEqual' && res === other) {
        res = `${known}\n\n${res}`;
        if (res.length > 1024) res = `${res.slice(0, 1021)}...`;
        super(res);
      } else {
        if (res.length > kMaxLongStringLength) res = `${res.slice(0, 509)}...`;
        if (other.length > kMaxLongStringLength) other = `${other.slice(0, 509)}...`;
        if (operator === 'deepEqual') {
          res = `${known}\n\n${res}\n\nshould loosely deep-equal\n\n`;
        } else {
          const unequal = kReadableOperator[`${operator}Unequal`];
          if (unequal) res = `${unequal}\n\n${res}\n\nshould not loosely deep-equal\n\n`;
          else other = ` ${operator} ${other}`;
        }
        super(`${res}${other}`);
      }
    }
    this.generatedMessage = !message;
    // The stack starts with "AssertionError [ERR_ASSERTION]: ..."
    Object.defineProperty(this, 'name', { value: 'AssertionError [ERR_ASSERTION]', enumerable: false, writable: true, configurable: true });
    this.code = 'ERR_ASSERTION';
    this.actual = actual;
    this.expected = expected;
    this.operator = operator;
    if (Error.captureStackTrace) Error.captureStackTrace(this, stackStartFn || AssertionError);
    void this.stack;
    this.name = 'AssertionError';
  }

  toString() {
    return `${this.name} [${this.code}]: ${this.message}`;
  }
}

function innerFail(options) {
  if (options.message instanceof Error) throw options.message;
  throw new AssertionError(options);
}

// ---- Loose deep equality (assert.deepEqual) ----

function isDeepLooseEqual(a, b, memo) {
  if (a === b) return true;
  if (a === null || typeof a !== 'object') {
    // Loose equality is the point of deepEqual
    // eslint-disable-next-line eqeqeq
    if (b === null || typeof b !== 'object') return a == b || (Number.isNaN(a) && Number.isNaN(b));
    return false;
  }
  if (b === null || typeof b !== 'object') return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  if (a instanceof Date && b instanceof Date) {
    if (a.getTime() !== b.getTime()) return false;
  } else if (a instanceof RegExp && b instanceof RegExp) {
    if (String(a) !== String(b) || a.lastIndex !== b.lastIndex) return false;
  } else if (a instanceof Error || b instanceof Error) {
    if (a.message !== b.message || a.name !== b.name) return false;
  } else if (ArrayBuffer.isView(a) && ArrayBuffer.isView(b)) {
    if (a.length !== b.length) return false;
    // eslint-disable-next-line eqeqeq
    for (let i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
  }
  memo = memo || { a: new Map(), b: new Map() };
  if (memo.a.get(a) === b) return true;
  memo.a.set(a, b);
  if (a instanceof Map || b instanceof Map) {
    if (!(a instanceof Map && b instanceof Map) || a.size !== b.size) return false;
    outer: for (const [key, value] of a) {
      if (b.has(key) && isDeepLooseEqual(value, b.get(key), memo)) continue;
      for (const [otherKey, otherValue] of b) {
        if (isDeepLooseEqual(key, otherKey, memo) && isDeepLooseEqual(value, otherValue, memo)) continue outer;
      }
      return false;
    }
  } else if (a instanceof Set || b instanceof Set) {
    if (!(a instanceof Set && b instanceof Set) || a.size !== b.size) return false;
    outer: for (const value of a) {
      if (b.has(value)) continue;
      for (const other of b) if (isDeepLooseEqual(value, other, memo)) continue outer;
      return false;
    }
  }
  const keysA = Object.keys(a);
  const keysB = Object.keys(b);
  if (keysA.length !== keysB.length) return false;
  for (const key of keysA) {
    if (!Object.prototype.propertyIsEnumerable.call(b, key)) return false;
    if (!isDeepLooseEqual(a[key], b[key], memo)) return false;
  }
  return true;
}

// ---- Assertions ----

// The source of a falsy assert.ok() call, as Node.js shows it: the call
// expression at the caller's position in its file
const errorCache = new Map();

function callSite(fn) {
  const limit = Error.stackTraceLimit;
  const prepare = Error.prepareStackTrace;
  Error.stackTraceLimit = 1;
  Error.prepareStackTrace = (_, frames) => frames;
  const holder = {};
  try {
    Error.captureStackTrace(holder, fn);
    return Array.isArray(holder.stack) ? holder.stack[0] : undefined;
  } finally {
    Error.prepareStackTrace = prepare;
    Error.stackTraceLimit = limit;
  }
}

// The text of the call whose callee name starts at offset, or undefined
function extractCall(code, offset) {
  const isIdentifier = (ch) => ch !== undefined && /[\w$]/.test(ch);
  // Back over the member chain: a.b.c(
  let start = offset;
  for (;;) {
    let i = start;
    while (i > 0 && /\s/.test(code[i - 1])) i--;
    if (code[i - 1] !== '.') break;
    i--;
    if (code[i - 1] === '?') i--;
    while (i > 0 && /\s/.test(code[i - 1])) i--;
    if (!isIdentifier(code[i - 1])) break;
    while (i > 0 && isIdentifier(code[i - 1])) i--;
    start = i;
  }
  let i = offset;
  while (isIdentifier(code[i])) i++;
  while (/\s/.test(code[i] || '')) i++;
  if (code[i] === '?' && code[i + 1] === '.') i += 2;
  if (code[i] !== '(') return undefined;
  // Forward to the matching parenthesis, past strings, templates and comments
  let depth = 0;
  for (; i < code.length; i++) {
    const ch = code[i];
    if (ch === '"' || ch === "'" || ch === '`') {
      for (i++; i < code.length && code[i] !== ch; i++) if (code[i] === '\\') i++;
    } else if (ch === '/' && code[i + 1] === '/') {
      i = code.indexOf('\n', i);
      if (i < 0) return undefined;
    } else if (ch === '/' && code[i + 1] === '*') {
      i = code.indexOf('*/', i + 2) + 1;
      if (i <= 0) return undefined;
    } else if (ch === '(' || ch === '[' || ch === '{') {
      depth++;
    } else if (ch === ')' || ch === ']' || ch === '}') {
      depth--;
      if (depth === 0) return { start, text: code.slice(start, i + 1) };
    }
  }
  return undefined;
}

function getErrMessage(fn) {
  const call = callSite(fn);
  if (!call) return undefined;
  let filename = call.getFileName();
  if (!filename || filename.startsWith('frogjs:') || filename.startsWith('[') || filename.startsWith('node:')) return undefined;
  const line = call.getLineNumber() - 1;
  const column = call.getColumnNumber() - 1;
  const identifier = `${filename}${line}${column}`;
  if (errorCache.has(identifier)) return errorCache.get(identifier);
  let message;
  try {
    if (filename.startsWith('file:')) filename = url.fileURLToPath(filename);
    const code = fs.readFileSync(filename, 'utf8').split('\n').slice(line).join('\n');
    const found = extractCall(code, column);
    if (found) {
      // eslint-disable-next-line no-control-regex -- escapes control characters in the message
      let text = found.text.replace(/[\x00-\x08\x0b\x0c\x0e-\x1f]/g, (ch) => `\\u${ch.charCodeAt(0).toString(16).padStart(4, '0')}`);
      // Lines after the first lose the indentation of the first
      if (text.includes('\n')) {
        const frames = text.split('\n');
        text = frames.shift();
        for (const frame of frames) {
          let pos = 0;
          while (pos < found.start && (frame[pos] === ' ' || frame[pos] === '\t')) pos++;
          text += `\n  ${frame.slice(pos)}`;
        }
      }
      message = `The expression evaluated to a falsy value:\n\n  ${text}\n`;
    }
  } catch {
    message = undefined;
  }
  errorCache.set(identifier, message);
  return message;
}

function innerOk(fn, argLen, value, message) {
  if (!value) {
    let generatedMessage = false;
    if (argLen === 0) {
      generatedMessage = true;
      message = 'No value argument passed to `assert.ok()`';
    } else if (message == null) {
      generatedMessage = true;
      message = getErrMessage(fn) || `${inspect(value)} == true`;
    } else if (message instanceof Error) {
      throw message;
    }
    const err = new AssertionError({ actual: value, expected: true, message, operator: '==', stackStartFn: fn });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

function ok(...args) {
  innerOk(ok, args.length, ...args);
}

function missing(...names) {
  const shown = names.map((name) => `"${name}"`).join(' and ');
  return codeError(TypeError, 'ERR_MISSING_ARGS', `The ${shown} arguments must be specified`);
}

function equal(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  // assert.equal is loose (==) by definition
  // eslint-disable-next-line eqeqeq
  if (actual != expected && (!Number.isNaN(actual) || !Number.isNaN(expected))) {
    innerFail({ actual, expected, message, operator: '==', stackStartFn: equal });
  }
}

function notEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  // eslint-disable-next-line eqeqeq
  if (actual == expected || (Number.isNaN(actual) && Number.isNaN(expected))) {
    innerFail({ actual, expected, message, operator: '!=', stackStartFn: notEqual });
  }
}

function deepEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (!isDeepLooseEqual(actual, expected)) innerFail({ actual, expected, message, operator: 'deepEqual', stackStartFn: deepEqual });
}

function notDeepEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (isDeepLooseEqual(actual, expected)) innerFail({ actual, expected, message, operator: 'notDeepEqual', stackStartFn: notDeepEqual });
}

function deepStrictEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (!isDeepStrictEqual(actual, expected)) innerFail({ actual, expected, message, operator: 'deepStrictEqual', stackStartFn: deepStrictEqual });
}

function notDeepStrictEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (isDeepStrictEqual(actual, expected)) innerFail({ actual, expected, message, operator: 'notDeepStrictEqual', stackStartFn: notDeepStrictEqual });
}

function strictEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (!Object.is(actual, expected)) innerFail({ actual, expected, message, operator: 'strictEqual', stackStartFn: strictEqual });
}

function notStrictEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (Object.is(actual, expected)) innerFail({ actual, expected, message, operator: 'notStrictEqual', stackStartFn: notStrictEqual });
}

// Whether actual contains expected: objects by their keys, arrays by elements
// in order, Sets by elements, Maps by entries
function isPartialEqual(actual, expected) {
  if (typeof expected !== 'object' || expected === null || typeof actual !== 'object' || actual === null) {
    return isDeepStrictEqual(actual, expected);
  }
  if (Object.getPrototypeOf(actual) !== Object.getPrototypeOf(expected)) return false;
  if (expected instanceof Map) {
    for (const [key, value] of expected) if (!actual.has(key) || !isPartialEqual(actual.get(key), value)) return false;
    return true;
  }
  if (Array.isArray(expected)) {
    // In order, but not necessarily next to each other
    let position = 0;
    for (const value of expected) {
      while (position < actual.length && !isPartialEqual(actual[position], value)) position++;
      if (position >= actual.length) return false;
      position++;
    }
    return true;
  }
  if (expected instanceof Set) {
    const remaining = Array.from(actual);
    for (const value of expected) {
      const index = remaining.findIndex((item) => isPartialEqual(item, value));
      if (index < 0) return false;
      remaining.splice(index, 1);
    }
    return true;
  }
  if (expected instanceof Date || expected instanceof RegExp || ArrayBuffer.isView(expected)) return isDeepStrictEqual(actual, expected);
  for (const key of Reflect.ownKeys(expected)) {
    if (!Object.prototype.propertyIsEnumerable.call(expected, key)) continue;
    if (!(key in actual) || !isPartialEqual(actual[key], expected[key])) return false;
  }
  return true;
}

function partialDeepStrictEqual(actual, expected, message) {
  if (arguments.length < 2) throw missing('actual', 'expected');
  if (!isPartialEqual(actual, expected)) {
    innerFail({ actual, expected, message, operator: 'partialDeepStrictEqual', stackStartFn: partialDeepStrictEqual });
  }
}

class Comparison {
  constructor(obj, keys, actual) {
    for (const key of keys) {
      if (key in obj) {
        if (actual !== undefined && typeof actual[key] === 'string' && obj[key] instanceof RegExp && obj[key].test(actual[key])) {
          this[key] = actual[key];
        } else {
          this[key] = obj[key];
        }
      }
    }
  }
}

function compareExceptionKey(actual, expected, key, message, keys, fn) {
  if (!(key in actual) || !isDeepStrictEqual(actual[key], expected[key])) {
    if (!message) {
      const a = new Comparison(actual, keys);
      const b = new Comparison(expected, keys, actual);
      const err = new AssertionError({ actual: a, expected: b, operator: 'deepStrictEqual', stackStartFn: fn });
      err.actual = actual;
      err.expected = expected;
      err.operator = fn.name;
      throw err;
    }
    innerFail({ actual, expected, message, operator: fn.name, stackStartFn: fn });
  }
}

function expectedException(actual, expected, message, fn) {
  let generatedMessage = false;
  let throwError = false;
  if (typeof expected !== 'function') {
    if (expected instanceof RegExp) {
      const str = String(actual);
      if (expected.exec(str) !== null) return;
      if (!message) {
        generatedMessage = true;
        message = `The input did not match the regular expression ${inspect(expected)}. Input:\n\n${inspect(str)}\n`;
      }
      throwError = true;
    } else if (typeof actual !== 'object' || actual === null) {
      const err = new AssertionError({ actual, expected, message, operator: 'deepStrictEqual', stackStartFn: fn });
      err.operator = fn.name;
      throw err;
    } else {
      const keys = Object.keys(expected);
      if (expected instanceof Error) keys.push('name', 'message');
      else if (keys.length === 0) {
        throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'error' may not be an empty object. Received {}`);
      }
      for (const key of keys) {
        if (typeof actual[key] === 'string' && expected[key] instanceof RegExp && expected[key].exec(actual[key]) !== null) continue;
        compareExceptionKey(actual, expected, key, message, keys, fn);
      }
      return;
    }
  } else if (expected.prototype !== undefined && actual instanceof expected) {
    return;
  } else if (Object.prototype.isPrototypeOf.call(Error, expected) || expected === Error) {
    if (!message) {
      generatedMessage = true;
      message = 'The error is expected to be an instance of ' + `"${expected.name}". Received `;
      if (actual instanceof Error) {
        const name = (actual.constructor && actual.constructor.name) || actual.name;
        if (expected.name === name) {
          message += 'an error with identical name but a different prototype.';
        } else {
          message += `"${name}"`;
        }
        if (actual.message) message += `\n\nError message:\n\n${actual.message}`;
      } else {
        message += `"${inspect(actual, { depth: -1 })}"`;
      }
    }
    throwError = true;
  } else {
    const result = expected.call({}, actual);
    if (result !== true) {
      if (!message) {
        generatedMessage = true;
        const name = expected.name ? `"${expected.name}" ` : '';
        message = `The ${name}validation function is expected to return "true". Received ${inspect(result)}`;
        if (actual instanceof Error) message += `\n\nCaught error:\n\n${actual}`;
      }
      throwError = true;
    }
  }
  if (throwError) {
    const err = new AssertionError({ actual, expected, message, operator: fn.name, stackStartFn: fn });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

const NO_EXCEPTION = Symbol('no exception');

function getActual(fn) {
  if (typeof fn !== 'function') throw invalidArgType('fn', 'of type function', fn);
  try {
    fn();
  } catch (err) {
    return err;
  }
  return NO_EXCEPTION;
}

async function waitForActual(promiseFn) {
  let promise;
  if (typeof promiseFn === 'function') {
    promise = promiseFn();
    if (!promise || typeof promise.then !== 'function') {
      throw codeError(TypeError, 'ERR_INVALID_RETURN_VALUE',
        `Expected instance of Promise to be returned from the "promiseFn" function but got ${describeType(promise)}.`);
    }
  } else if (promiseFn && typeof promiseFn.then === 'function') {
    promise = promiseFn;
  } else {
    throw invalidArgType('promiseFn', 'of type function or an instance of Promise', promiseFn);
  }
  try {
    await promise;
  } catch (err) {
    return err;
  }
  return NO_EXCEPTION;
}

function expectsError(fn, actual, error, message) {
  if (typeof error === 'string') {
    if (arguments.length === 4) throw invalidArgType('error', 'of type function or an instance of Error, RegExp, or Object', error);
    if (typeof actual === 'object' && actual !== null) {
      if (actual.message === error) {
        throw codeError(TypeError, 'ERR_AMBIGUOUS_ARGUMENT', `The "error/message" argument is ambiguous. The error message "${actual.message}" is identical to the message.`);
      }
    } else if (actual === error) {
      throw codeError(TypeError, 'ERR_AMBIGUOUS_ARGUMENT', `The "error/message" argument is ambiguous. The error "${actual}" is identical to the message.`);
    }
    message = error;
    error = undefined;
  } else if (error != null && typeof error !== 'object' && typeof error !== 'function') {
    throw invalidArgType('error', 'of type function or an instance of Error, RegExp, or Object', error);
  }
  if (actual === NO_EXCEPTION) {
    let details = '';
    if (error && error.name) details += ` (${error.name})`;
    details += message ? `: ${message}` : '.';
    const fnType = fn === rejects ? 'rejection' : 'exception';
    innerFail({ actual: undefined, expected: error, operator: fn.name, message: `Missing expected ${fnType}${details}`, stackStartFn: fn });
  }
  if (error) expectedException(actual, error, message, fn);
}

function hasMatchingError(actual, expected) {
  if (typeof expected !== 'function') {
    if (expected instanceof RegExp) return expected.exec(String(actual)) !== null;
    throw invalidArgType('expected', 'of type function or an instance of RegExp', expected);
  }
  if (expected.prototype !== undefined && actual instanceof expected) return true;
  if (Object.prototype.isPrototypeOf.call(Error, expected)) return false;
  return expected.call({}, actual) === true;
}

function expectsNoError(fn, actual, error, message) {
  if (actual === NO_EXCEPTION) return;
  if (typeof error === 'string') {
    message = error;
    error = undefined;
  }
  if (!error || hasMatchingError(actual, error)) {
    const details = message ? `: ${message}` : '.';
    const fnType = fn === doesNotReject ? 'rejection' : 'exception';
    innerFail({ actual, expected: error, operator: fn.name,
      message: `Got unwanted ${fnType}${details}\nActual message: "${actual && actual.message}"`, stackStartFn: fn });
  }
  throw actual;
}

function throws(promiseFn, ...args) {
  expectsError(throws, getActual(promiseFn), ...args);
}

async function rejects(promiseFn, ...args) {
  expectsError(rejects, await waitForActual(promiseFn), ...args);
}

function doesNotThrow(fn, ...args) {
  expectsNoError(doesNotThrow, getActual(fn), ...args);
}

async function doesNotReject(fn, ...args) {
  expectsNoError(doesNotReject, await waitForActual(fn), ...args);
}

function ifError(err) {
  if (err !== null && err !== undefined) {
    let message = 'ifError got unwanted exception: ';
    if (typeof err === 'object' && typeof err.message === 'string') {
      if (err.message.length === 0 && err.constructor) message += err.constructor.name;
      else message += err.message;
    } else {
      message += inspect(err);
    }
    const newErr = new AssertionError({ actual: err, expected: null, operator: 'ifError', message, stackStartFn: ifError });
    // The original stack, under the new error's first line
    const originalStack = err && err.stack;
    if (typeof originalStack === 'string') {
      const lines = originalStack.split('\n').slice(1).filter((line) => line.startsWith('    at '));
      if (lines.length > 0) newErr.stack = `${newErr.stack}\n    ${lines.join('\n    ')}`.replace(/\n {4}( {4}at )/g, '\n$1');
    }
    throw newErr;
  }
}

function internalMatch(string, regexp, message, fn) {
  if (!(regexp instanceof RegExp)) throw invalidArgType('regexp', 'an instance of RegExp', regexp);
  const match = fn === match_;
  if (typeof string !== 'string' || (regexp.exec(string) !== null) !== match) {
    if (message instanceof Error) throw message;
    const generatedMessage = !message;
    message = message || (typeof string !== 'string' ?
      `The "string" argument must be of type string. Received type ${typeof string} (${inspect(string)})` :
      (match ? 'The input did not match the regular expression ' : 'The input was expected to not match the regular expression ') +
      `${inspect(regexp)}. Input:\n\n${inspect(string)}\n`);
    const err = new AssertionError({ actual: string, expected: regexp, message, operator: fn.name, stackStartFn: fn });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

function match_(string, regexp, message) {
  internalMatch(string, regexp, message, match_);
}
Object.defineProperty(match_, 'name', { value: 'match' });

function doesNotMatch(string, regexp, message) {
  internalMatch(string, regexp, message, doesNotMatch);
}

function fail(actual, expected, message, operator, stackStartFn) {
  const argsLen = arguments.length;
  let internalMessage = false;
  if (argsLen === 0) {
    internalMessage = true;
    message = 'Failed';
  } else if (argsLen === 1) {
    message = actual;
    actual = undefined;
  } else {
    if (argsLen === 2) operator = '!=';
  }
  if (message instanceof Error) throw message;
  const options = { actual, expected, operator: operator === undefined ? 'fail' : operator, stackStartFn: stackStartFn || fail, message };
  if (argsLen === 2 && message === undefined) options.message = `${inspect(actual)} != ${inspect(expected)}`;
  const err = new AssertionError(options);
  if (internalMessage) {
    err.generatedMessage = true;
  }
  throw err;
}

// What calls() wraps without a function: nameless, as in Node.js ("the calls function")
const kNoop = Function.prototype;

// The deprecated call tracker: tracker.calls(fn, exact) wraps fn, and
// verify() throws unless every wrapper was called exactly that often
let warnedCallTracker = false;

class CallTracker {
  #contexts = [];
  #byWrapper = new WeakMap();

  constructor() {
    if (!warnedCallTracker) {
      warnedCallTracker = true;
      process.emitWarning('assert.CallTracker is deprecated.', 'DeprecationWarning', 'DEP0173');
    }
  }

  calls(fn = kNoop, exact = 1) {
    if (typeof fn === 'number') {
      exact = fn;
      fn = kNoop;
    }
    if (typeof fn !== 'function') throw invalidArgType('fn', 'of type function', fn);
    const context = { exact, actual: 0, name: fn.name || 'calls', stackTrace: new Error(), calls: [] };
    const wrapper = new Proxy(fn, {
      apply(target, thisArg, args) {
        context.actual++;
        context.calls.push({ thisArg, arguments: args });
        return Reflect.apply(target, thisArg, args);
      },
    });
    this.#contexts.push(context);
    this.#byWrapper.set(wrapper, context);
    return wrapper;
  }

  #context(fn) {
    const context = this.#byWrapper.get(fn);
    if (!context) throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', 'The argument \'fn\' must be a tracked function.');
    return context;
  }

  getCalls(fn) {
    return this.#context(fn).calls.slice();
  }

  reset(fn) {
    for (const context of fn === undefined ? this.#contexts : [this.#context(fn)]) {
      context.actual = 0;
      context.calls = [];
    }
  }

  report() {
    return this.#contexts.filter((context) => context.actual !== context.exact).map((context) => ({
      message: `Expected the ${context.name} function to be executed ${context.exact} time(s) but was executed ${context.actual} time(s).`,
      actual: context.actual,
      expected: context.exact,
      operator: context.name,
      stack: context.stackTrace,
    }));
  }

  verify() {
    const errors = this.report();
    if (errors.length === 0) return;
    const message = errors.length === 1 ? errors[0].message : 'Functions were not called the expected number of times';
    throw new AssertionError({ message, details: errors });
  }
}

// ---- The module ----

function assert(...args) {
  innerOk(assert, args.length, ...args);
}

Object.assign(assert, {
  fail, AssertionError, ok, equal, notEqual, deepEqual, notDeepEqual, deepStrictEqual, notDeepStrictEqual,
  strictEqual, notStrictEqual, partialDeepStrictEqual, throws, rejects, doesNotThrow, doesNotReject,
  ifError, match: match_, doesNotMatch, CallTracker,
});

function strict(...args) {
  innerOk(strict, args.length, ...args);
}

Object.assign(strict, assert, {
  equal: strictEqual, deepEqual: deepStrictEqual, notEqual: notStrictEqual, notDeepEqual: notDeepStrictEqual,
});
assert.strict = strict;
strict.strict = strict;

return assert;
})
)JS";

// Set up the assert module (requires SetupUtil, SetupFileSystem and SetupURL first)
void SetupAssert(Isolate* isolate, Local<Context> context) {
    Local<Value> argv[3] = {frogjs::GetBuiltin(isolate, context, "util"), frogjs::GetBuiltin(isolate, context, "fs"),
                            frogjs::GetBuiltin(isolate, context, "url")};
    Local<Value> module;
    if (!frogjs::RunJsModule(isolate, context, "assert", kAssertSource, 3, argv).ToLocal(&module)) return;
    frogjs::RegisterBuiltin(isolate, context, "assert", module);
    Local<Value> strict = module.As<Object>()->Get(context, String::NewFromUtf8Literal(isolate, "strict")).ToLocalChecked();
    frogjs::RegisterBuiltin(isolate, context, "assert/strict", strict);
}
