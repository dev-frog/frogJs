#include <v8.h>
#include <string>
#include <vector>
#include "frogjs/buffer.h"
#include "frogjs/js_module.h"
#include "frogjs/util.h"

using namespace v8;

// util.inspect / util.format, written in JavaScript. The layout rules (when an
// object fits on one line, how long arrays are grouped into columns, quoting)
// follow Node.js so output matches `node -p` for the supported types.
static const char* kUtilSource = R"JS(
(function (native) {
'use strict';

const customInspect = Symbol.for('nodejs.util.inspect.custom');

const inspectDefaults = {
  showHidden: false,
  depth: 2,
  colors: false,
  maxArrayLength: 100,
  maxStringLength: 10000,
  breakLength: 80,
  compact: 3,
  sorted: false,
};

const colorCodes = {
  bold: [1, 22], italic: [3, 23], underline: [4, 24], inverse: [7, 27],
  white: [37, 39], grey: [90, 39], black: [30, 39], blue: [34, 39],
  cyan: [36, 39], green: [32, 39], magenta: [35, 39], red: [31, 39], yellow: [33, 39],
};

const styles = {
  special: 'cyan', number: 'yellow', bigint: 'yellow', boolean: 'yellow',
  undefined: 'grey', null: 'bold', string: 'green', symbol: 'green',
  date: 'magenta', regexp: 'red', module: 'underline',
};

const kObjectType = 0;
const kArrayType = 1;
const kArrayExtrasType = 2;

const ansiPattern = /\u001b\[\d\d?m/g;
const keyIdentifier = /^[a-zA-Z_][a-zA-Z_0-9]*$/;

function removeColors(str) {
  return str.replace(ansiPattern, '');
}

function stylizeWithColor(str, styleType) {
  const style = styles[styleType];
  if (style !== undefined) {
    const color = colorCodes[style];
    if (color !== undefined) return `\u001b[${color[0]}m${str}\u001b[${color[1]}m`;
  }
  return str;
}

function stylizeNoColor(str) {
  return str;
}

function inspect(value, opts) {
  const ctx = {
    indentationLvl: 0,
    seen: [],
    currentDepth: 0,
    stylize: stylizeNoColor,
    showHidden: inspectDefaults.showHidden,
    depth: inspectDefaults.depth,
    colors: inspectDefaults.colors,
    maxArrayLength: inspectDefaults.maxArrayLength,
    maxStringLength: inspectDefaults.maxStringLength,
    breakLength: inspectDefaults.breakLength,
    compact: inspectDefaults.compact,
    sorted: inspectDefaults.sorted,
  };
  if (typeof opts === 'boolean') {
    // Legacy signature: inspect(value, showHidden, depth, colors)
    ctx.showHidden = opts;
    if (arguments.length > 2 && arguments[2] !== undefined) ctx.depth = arguments[2];
    if (arguments.length > 3 && arguments[3] !== undefined) ctx.colors = arguments[3];
  } else if (opts !== null && typeof opts === 'object') {
    for (const key of Object.keys(opts)) {
      if (Object.prototype.hasOwnProperty.call(inspectDefaults, key) || key === 'stylize') {
        ctx[key] = opts[key];
      }
    }
  }
  if (ctx.colors) ctx.stylize = stylizeWithColor;
  if (ctx.depth === null) ctx.depth = Infinity;
  if (ctx.maxArrayLength === null) ctx.maxArrayLength = Infinity;
  if (ctx.maxStringLength === null) ctx.maxStringLength = Infinity;
  return formatValue(ctx, value, 0);
}

inspect.custom = customInspect;
inspect.defaultOptions = inspectDefaults;
inspect.colors = colorCodes;
inspect.styles = styles;

function formatValue(ctx, value, recurseTimes, typedArray) {
  if (typeof value !== 'object' && typeof value !== 'function') {
    return formatPrimitive(ctx.stylize, value, ctx);
  }
  if (value === null) {
    return ctx.stylize('null', 'null');
  }

  // Show what a Proxy wraps without running its traps
  const proxyTarget = native.getProxyTarget(value);
  if (proxyTarget !== undefined) {
    if (proxyTarget === null) return ctx.stylize('<Revoked Proxy>', 'special');
    return formatValue(ctx, proxyTarget, recurseTimes, typedArray);
  }

  // User-defined [util.inspect.custom](depth, options, inspect)
  let maybeCustom;
  try {
    maybeCustom = value[customInspect];
  } catch {
    maybeCustom = undefined;
  }
  if (typeof maybeCustom === 'function' && maybeCustom !== inspect) {
    const depth = ctx.depth === Infinity ? Infinity : ctx.depth - recurseTimes;
    const options = Object.assign({}, ctx, { depth, stylize: ctx.stylize });
    const ret = maybeCustom.call(value, depth, options, inspect);
    if (ret !== value) {
      if (typeof ret !== 'string') return formatValue(ctx, ret, recurseTimes);
      return ret.replace(/\n/g, `\n${' '.repeat(ctx.indentationLvl)}`);
    }
  }

  if (ctx.seen.includes(value)) {
    let index = 1;
    if (ctx.circular === undefined) {
      ctx.circular = new Map();
      ctx.circular.set(value, index);
    } else {
      index = ctx.circular.get(value);
      if (index === undefined) {
        index = ctx.circular.size + 1;
        ctx.circular.set(value, index);
      }
    }
    return ctx.stylize(`[Circular *${index}]`, 'special');
  }

  return formatRaw(ctx, value, recurseTimes, typedArray);
}

function getConstructorName(obj) {
  let current = obj;
  while (current !== null && current !== undefined) {
    let descriptor;
    try {
      descriptor = Object.getOwnPropertyDescriptor(current, 'constructor');
    } catch {
      return null;
    }
    if (descriptor !== undefined && typeof descriptor.value === 'function' &&
        descriptor.value.name !== '' && isInstanceof(obj, descriptor.value)) {
      return String(descriptor.value.name);
    }
    current = Object.getPrototypeOf(current);
  }
  return null;
}

function isInstanceof(object, proto) {
  try {
    return object instanceof proto;
  } catch {
    return false;
  }
}

function getPrefix(constructor, tag, fallback, size = '') {
  if (constructor === null) {
    if (tag !== '' && fallback !== tag) return `[${fallback}${size}: null prototype] [${tag}] `;
    return `[${fallback}${size}: null prototype] `;
  }
  if (tag !== '' && constructor !== tag) return `${constructor}${size} [${tag}] `;
  return `${constructor}${size} `;
}

function getKeys(value, showHidden) {
  let keys;
  const symbols = Object.getOwnPropertySymbols(value);
  if (showHidden) {
    keys = Object.getOwnPropertyNames(value);
    if (symbols.length !== 0) keys.push(...symbols);
  } else {
    try {
      keys = Object.keys(value);
    } catch {
      keys = [];
    }
    for (const sym of symbols) {
      if (Object.prototype.propertyIsEnumerable.call(value, sym)) keys.push(sym);
    }
  }
  return keys;
}

function isIndex(key) {
  return typeof key === 'string' && /^(0|[1-9][0-9]*)$/.test(key) && Number(key) < 4294967295;
}

function isTypedArray(value) {
  return ArrayBuffer.isView(value) && !(value instanceof DataView);
}

function isError(value) {
  return value instanceof Error || Object.prototype.toString.call(value) === '[object Error]';
}

function getFunctionBase(value, constructor, tag) {
  let source;
  try {
    source = Function.prototype.toString.call(value);
  } catch {
    source = '';
  }
  if (source.startsWith('class') && /^class(\s|\{)/.test(source)) {
    let base = `[class ${value.name || '(anonymous)'}`;
    if (constructor !== 'Function' && constructor !== null) base += ` [${constructor}]`;
    if (tag !== '' && constructor !== tag) base += ` [${tag}]`;
    if (constructor !== null) {
      const superName = Object.getPrototypeOf(value).name;
      if (superName) base += ` extends ${superName}`;
    } else {
      base += ' extends [null prototype]';
    }
    return `${base}]`;
  }
  let type = 'Function';
  const ctorName = Object.prototype.toString.call(value);
  if (ctorName === '[object AsyncFunction]') type = 'AsyncFunction';
  else if (ctorName === '[object GeneratorFunction]') type = 'GeneratorFunction';
  else if (ctorName === '[object AsyncGeneratorFunction]') type = 'AsyncGeneratorFunction';
  let base = `[${type}`;
  if (constructor === null) base += ' (null prototype)';
  base += value.name === '' ? ' (anonymous)' : `: ${value.name}`;
  base += ']';
  if (constructor !== type && constructor !== null) base += ` ${constructor}`;
  if (tag !== '' && constructor !== tag) base += ` [${tag}]`;
  return base;
}

function formatError(err, constructor, tag, ctx, keys) {
  let stack;
  try {
    stack = err.stack;
  } catch {
    stack = undefined;
  }
  const name = err.name != null ? String(err.name) : 'Error';
  const message = err.message != null ? String(err.message) : '';
  if (typeof stack !== 'string' || stack === '') {
    stack = message ? `${name}: ${message}` : name;
    stack = `[${stack}]`;
  }
  // Own enumerable name/message are already part of the stack
  for (const key of ['name', 'message', 'stack']) {
    const index = keys.indexOf(key);
    if (index !== -1 && typeof err[key] === 'string' && stack.includes(err[key])) keys.splice(index, 1);
  }
  if (constructor === null) {
    stack = `[${name}: null prototype] ${stack}`;
  }
  if (ctx.indentationLvl !== 0) {
    const indentation = ' '.repeat(ctx.indentationLvl);
    stack = stack.replace(/\n/g, `\n${indentation}`);
  }
  return stack;
}

function formatRaw(ctx, value, recurseTimes, typedArray) {
  let keys;
  const constructor = getConstructorName(value);
  let tag = '';
  try {
    tag = value[Symbol.toStringTag];
  } catch {
    tag = '';
  }
  if (typeof tag !== 'string' || (tag !== '' && (ctx.showHidden ?
      Object.prototype.hasOwnProperty : Object.prototype.propertyIsEnumerable).call(value, Symbol.toStringTag))) {
    tag = '';
  }

  let base = '';
  let formatter = getEmptyFormatArray;
  let braces;
  let noIterator = true;
  let i = 0;
  let extrasType = kObjectType;

  if (Symbol.iterator in value || constructor === null) {
    noIterator = false;
    if (Array.isArray(value)) {
      const prefix = (constructor !== 'Array' || tag !== '') ?
        getPrefix(constructor, tag, 'Array', `(${value.length})`) : '';
      keys = getOwnNonIndexProperties(value, ctx.showHidden);
      braces = [`${prefix}[`, ']'];
      if (value.length === 0 && keys.length === 0) return `${braces[0]}]`;
      extrasType = kArrayExtrasType;
      formatter = formatArray;
    } else if (value instanceof Set) {
      const size = value.size;
      const prefix = getPrefix(constructor, tag, 'Set', `(${size})`);
      keys = getKeys(value, ctx.showHidden);
      formatter = formatSet.bind(null, value);
      if (size === 0 && keys.length === 0) return `${prefix}{}`;
      braces = [`${prefix}{`, '}'];
    } else if (value instanceof Map) {
      const size = value.size;
      const prefix = getPrefix(constructor, tag, 'Map', `(${size})`);
      keys = getKeys(value, ctx.showHidden);
      formatter = formatMap.bind(null, value);
      if (size === 0 && keys.length === 0) return `${prefix}{}`;
      braces = [`${prefix}{`, '}'];
    } else if (isTypedArray(value)) {
      keys = getOwnNonIndexProperties(value, ctx.showHidden);
      const fallback = Object.prototype.toString.call(value).slice(8, -1);
      const size = value.length;
      const prefix = getPrefix(constructor, tag, fallback, `(${size})`);
      braces = [`${prefix}[`, ']'];
      if (value.length === 0 && keys.length === 0 && !ctx.showHidden) return `${braces[0]}]`;
      formatter = formatTypedArray.bind(null, value, size);
      extrasType = kArrayExtrasType;
    } else {
      noIterator = true;
    }
  }

  if (noIterator) {
    keys = getKeys(value, ctx.showHidden);
    braces = ['{', '}'];
    if (typeof value === 'function') {
      base = getFunctionBase(value, constructor, tag);
      if (keys.length === 0) return ctx.stylize(base, 'special');
    } else if (native.isBuffer(value)) {
      return formatBuffer(ctx, value);
    } else if (constructor === 'Object' && !isError(value) && !(value instanceof Date) &&
               !(value instanceof RegExp) && !native.isBoxedPrimitive(value) && !(value instanceof Promise)) {
      if (Object.prototype.toString.call(value) === '[object Arguments]') {
        braces[0] = '[Arguments] [';
        braces[1] = ']';
        keys = getOwnNonIndexProperties(value, ctx.showHidden);
        extrasType = kArrayExtrasType;
        formatter = formatArray;
        if (value.length === 0 && keys.length === 0) return '[Arguments] []';
      } else {
        if (tag !== '') braces[0] = `${getPrefix(constructor, tag, 'Object')}{`;
        if (keys.length === 0) return `${braces[0]}}`;
      }
    } else if (value instanceof RegExp) {
      base = RegExp.prototype.toString.call(value);
      const prefix = getPrefix(constructor, tag, 'RegExp');
      if (prefix !== 'RegExp ') base = `${prefix}${base}`;
      base = ctx.stylize(base, 'regexp');
      if (keys.length === 0) return base;
    } else if (value instanceof Date) {
      if (Number.isNaN(value.getTime())) {
        base = Date.prototype.toString.call(value);
      } else {
        base = Date.prototype.toISOString.call(value);
      }
      const prefix = getPrefix(constructor, tag, 'Date');
      if (prefix !== 'Date ') base = `${prefix}${base}`;
      if (keys.length === 0) return ctx.stylize(base, 'date');
      base = ctx.stylize(base, 'date');
    } else if (isError(value)) {
      base = formatError(value, constructor, tag, ctx, keys);
      if (keys.length === 0) return base;
    } else if (value instanceof ArrayBuffer || Object.prototype.toString.call(value) === '[object SharedArrayBuffer]') {
      const arrayType = value instanceof ArrayBuffer ? 'ArrayBuffer' : 'SharedArrayBuffer';
      const prefix = getPrefix(constructor, tag, arrayType);
      if (typedArray === undefined) {
        formatter = formatArrayBuffer;
      } else if (keys.length === 0) {
        return prefix + `{ byteLength: ${formatNumber(ctx.stylize, value.byteLength)} }`;
      }
      braces[0] = `${prefix}{`;
      keys.unshift('byteLength');
    } else if (value instanceof DataView) {
      braces[0] = `${getPrefix(constructor, tag, 'DataView')}{`;
      keys.unshift('byteLength', 'byteOffset', 'buffer');
    } else if (value instanceof Promise) {
      braces[0] = `${getPrefix(constructor, tag, 'Promise')}{`;
      formatter = formatPromise;
    } else if (value instanceof WeakSet) {
      braces[0] = `${getPrefix(constructor, tag, 'WeakSet')}{`;
      formatter = formatWeakCollection;
    } else if (value instanceof WeakMap) {
      braces[0] = `${getPrefix(constructor, tag, 'WeakMap')}{`;
      formatter = formatWeakCollection;
    } else if (native.isBoxedPrimitive(value)) {
      base = getBoxedBase(value, ctx, keys, constructor, tag);
      if (keys.length === 0) return base;
    } else {
      if (keys.length === 0) {
        return `${getPrefix(constructor, tag, 'Object')}{}`;
      }
      braces[0] = `${getPrefix(constructor, tag, 'Object')}{`;
    }
  }

  if (recurseTimes > ctx.depth && ctx.depth !== null) {
    let constructorName = getPrefix(constructor, tag, 'Object').slice(0, -1);
    if (constructor !== null) constructorName = `[${constructorName}]`;
    return ctx.stylize(constructorName, 'special');
  }
  recurseTimes += 1;

  ctx.seen.push(value);
  ctx.currentDepth = recurseTimes;
  let output;
  try {
    output = formatter(ctx, value, recurseTimes);
    for (i = 0; i < keys.length; i++) {
      output.push(formatProperty(ctx, value, recurseTimes, keys[i], extrasType));
    }
  } catch (err) {
    ctx.seen.pop();
    if (err instanceof RangeError && /call stack/i.test(err.message)) {
      return ctx.stylize(`[${getPrefix(constructor, tag, 'Object').slice(0, -1)}: Inspection interrupted ` +
        'prematurely. Maximum call stack size exceeded.]', 'special');
    }
    throw err;
  }

  if (ctx.circular !== undefined) {
    const index = ctx.circular.get(value);
    if (index !== undefined) {
      const reference = ctx.stylize(`<ref *${index}>`, 'special');
      if (ctx.compact !== true) {
        base = base === '' ? reference : `${reference} ${base}`;
      } else {
        braces[0] = `${reference} ${braces[0]}`;
      }
    }
  }
  ctx.seen.pop();

  if (ctx.sorted) {
    const comparator = ctx.sorted === true ? undefined : ctx.sorted;
    if (extrasType === kObjectType) {
      output = output.sort(comparator);
    } else if (keys.length > 1) {
      const sorted = output.slice(output.length - keys.length).sort(comparator);
      output.splice(output.length - keys.length, keys.length, ...sorted);
    }
  }

  return reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value);
}

function getBoxedBase(value, ctx, keys, constructor, tag) {
  let type;
  let primitive;
  if (value instanceof Number) { type = 'Number'; primitive = Number.prototype.valueOf.call(value); }
  else if (value instanceof String) {
    type = 'String';
    primitive = String.prototype.valueOf.call(value);
    keys.splice(0, primitive.length);
  }
  else if (value instanceof Boolean) { type = 'Boolean'; primitive = Boolean.prototype.valueOf.call(value); }
  else if (typeof BigInt === 'function' && value instanceof BigInt) { type = 'BigInt'; primitive = BigInt.prototype.valueOf.call(value); }
  else { type = 'Symbol'; primitive = Symbol.prototype.valueOf.call(value); }
  let base = `[${type}`;
  if (type !== constructor) {
    if (constructor === null) base += ' (null prototype)';
    else base += ` (${constructor})`;
  }
  base += `: ${formatPrimitive(stylizeNoColor, primitive, ctx)}]`;
  if (tag !== '' && tag !== constructor) base += ` [${tag}]`;
  if (keys.length !== 0 || ctx.stylize === stylizeNoColor) return base;
  return ctx.stylize(base, type.toLowerCase());
}

function getOwnNonIndexProperties(value, showHidden) {
  const names = showHidden ? Object.getOwnPropertyNames(value) : Object.keys(value);
  const keys = names.filter((key) => !isIndex(key));
  const symbols = Object.getOwnPropertySymbols(value);
  for (const sym of symbols) {
    if (showHidden || Object.prototype.propertyIsEnumerable.call(value, sym)) keys.push(sym);
  }
  return keys;
}

function formatNumber(fn, number) {
  if (Object.is(number, -0)) return fn('-0', 'number');
  return fn(`${number}`, 'number');
}

function formatBigInt(fn, bigint) {
  return fn(`${bigint}n`, 'bigint');
}

const strEscapeSequencesRegExp = /[\x00-\x1f\x27\x5c\x7f-\x9f]/;
const meta = [
  '\\x00', '\\x01', '\\x02', '\\x03', '\\x04', '\\x05', '\\x06', '\\x07',
  '\\b', '\\t', '\\n', '\\x0B', '\\f', '\\r', '\\x0E', '\\x0F',
  '\\x10', '\\x11', '\\x12', '\\x13', '\\x14', '\\x15', '\\x16', '\\x17',
  '\\x18', '\\x19', '\\x1A', '\\x1B', '\\x1C', '\\x1D', '\\x1E', '\\x1F',
];

function escapeChar(ch, quote) {
  const code = ch.charCodeAt(0);
  if (code < 32) return meta[code];
  if (ch === quote) return `\\${quote}`;
  if (ch === '\\') return '\\\\';
  return `\\x${code.toString(16).toUpperCase()}`;
}

// Quote with single quotes, or with " or ` when that avoids escaping
function strEscape(str) {
  let quote = "'";
  if (str.includes("'")) {
    if (!str.includes('"')) quote = '"';
    else if (!str.includes('`') && !str.includes('${')) quote = '`';
  }
  if (!strEscapeSequencesRegExp.test(str) && (quote !== "'" || !str.includes("'"))) {
    return quote + str + quote;
  }
  let result = '';
  for (const ch of str) {
    const code = ch.charCodeAt(0);
    if (ch === quote || ch === '\\' || code < 32 || (code > 126 && code < 160)) {
      result += escapeChar(ch, quote);
    } else {
      result += ch;
    }
  }
  return quote + result + quote;
}

function formatPrimitive(fn, value, ctx) {
  if (typeof value === 'string') {
    let trailer = '';
    if (value.length > ctx.maxStringLength) {
      const remaining = value.length - ctx.maxStringLength;
      value = value.slice(0, ctx.maxStringLength);
      trailer = `... ${remaining} more character${remaining > 1 ? 's' : ''}`;
    }
    return fn(strEscape(value), 'string') + trailer;
  }
  if (typeof value === 'number') return formatNumber(fn, value);
  if (typeof value === 'bigint') return formatBigInt(fn, value);
  if (typeof value === 'boolean') return fn(`${value}`, 'boolean');
  if (typeof value === 'undefined') return fn('undefined', 'undefined');
  return fn(Symbol.prototype.toString.call(value), 'symbol');
}

function getEmptyFormatArray() {
  return [];
}

function formatArrayBuffer(ctx, value) {
  const bytes = new Uint8Array(value);
  let str = Array.from(bytes.subarray(0, Math.min(ctx.maxArrayLength, bytes.length)),
    (b) => b.toString(16).padStart(2, '0')).join(' ');
  const remaining = bytes.length - ctx.maxArrayLength;
  if (remaining > 0) str += ` ... ${remaining} more byte${remaining > 1 ? 's' : ''}`;
  return [`${ctx.stylize('[Uint8Contents]', 'special')}: <${str}>`];
}

function formatBuffer(ctx, value) {
  const max = 50;
  const info = native.bufferHex(value, max);
  let str = info.hex.replace(/(.{2})/g, '$1 ').trim();
  const remaining = info.length - max;
  if (remaining > 0) str += ` ... ${remaining} more byte${remaining > 1 ? 's' : ''}`;
  return `<Buffer ${str}>`;
}

function formatArray(ctx, value, recurseTimes) {
  const valLen = value.length;
  const len = Math.min(Math.max(0, ctx.maxArrayLength), valLen);
  const remaining = valLen - len;
  const output = [];
  for (let i = 0; i < len; i++) {
    if (!Object.prototype.hasOwnProperty.call(value, i)) {
      return formatArrayWithHoles(ctx, value, recurseTimes, len, output, i);
    }
    output.push(formatProperty(ctx, value, recurseTimes, i, kArrayType));
  }
  if (remaining > 0) output.push(`... ${remaining} more item${remaining > 1 ? 's' : ''}`);
  return output;
}

function formatArrayWithHoles(ctx, value, recurseTimes, len, output, i) {
  const valLen = value.length;
  let outputCount = output.length;
  while (i < valLen && outputCount < len) {
    if (Object.prototype.hasOwnProperty.call(value, i)) {
      output.push(formatProperty(ctx, value, recurseTimes, i, kArrayType));
      i++;
    } else {
      let j = i + 1;
      while (j < valLen && !Object.prototype.hasOwnProperty.call(value, j)) j++;
      const emptyItems = j - i;
      output.push(ctx.stylize(`<${emptyItems} empty item${emptyItems > 1 ? 's' : ''}>`, 'undefined'));
      i = j;
    }
    outputCount++;
  }
  const remaining = valLen - i;
  if (remaining > 0) output.push(`... ${remaining} more item${remaining > 1 ? 's' : ''}`);
  return output;
}

function formatTypedArray(value, length, ctx, ignored, recurseTimes) {
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = value.length - maxLength;
  const output = new Array(maxLength);
  const elementFormatter = value.length > 0 && typeof value[0] === 'number' ? formatNumber : formatBigInt;
  for (let i = 0; i < maxLength; ++i) output[i] = elementFormatter(ctx.stylize, value[i]);
  if (remaining > 0) output[maxLength] = `... ${remaining} more item${remaining > 1 ? 's' : ''}`;
  if (ctx.showHidden) {
    ctx.indentationLvl += 2;
    for (const key of ['BYTES_PER_ELEMENT', 'length', 'byteLength', 'byteOffset', 'buffer']) {
      const str = formatValue(ctx, value[key], recurseTimes, true);
      output.push(`[${key}]: ${str}`);
    }
    ctx.indentationLvl -= 2;
  }
  return output;
}

function formatSet(value, ctx, ignored, recurseTimes) {
  const length = value.size;
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = length - maxLength;
  const output = [];
  ctx.indentationLvl += 2;
  let i = 0;
  for (const v of value) {
    if (i >= maxLength) break;
    output.push(formatValue(ctx, v, recurseTimes));
    i++;
  }
  if (remaining > 0) output.push(`... ${remaining} more item${remaining > 1 ? 's' : ''}`);
  ctx.indentationLvl -= 2;
  return output;
}

function formatMap(value, ctx, ignored, recurseTimes) {
  const length = value.size;
  const maxLength = Math.min(Math.max(0, ctx.maxArrayLength), length);
  const remaining = length - maxLength;
  const output = [];
  ctx.indentationLvl += 2;
  let i = 0;
  for (const [k, v] of value) {
    if (i >= maxLength) break;
    output.push(`${formatValue(ctx, k, recurseTimes)} => ${formatValue(ctx, v, recurseTimes)}`);
    i++;
  }
  if (remaining > 0) output.push(`... ${remaining} more item${remaining > 1 ? 's' : ''}`);
  ctx.indentationLvl -= 2;
  return output;
}

function formatPromise(ctx, value, recurseTimes) {
  const [state, result] = native.getPromiseDetails(value);
  if (state === 'pending') return [ctx.stylize('<pending>', 'special')];
  ctx.indentationLvl += 2;
  const str = formatValue(ctx, result, recurseTimes);
  ctx.indentationLvl -= 2;
  return [state === 'rejected' ? `${ctx.stylize('<rejected>', 'special')} ${str}` : str];
}

function formatWeakCollection(ctx) {
  return [ctx.stylize('<items unknown>', 'special')];
}

function formatProperty(ctx, value, recurseTimes, key, type) {
  let name;
  let str;
  let extra = ' ';
  const desc = Object.getOwnPropertyDescriptor(value, key) || { value: value[key], enumerable: true };
  if (desc.value !== undefined) {
    const diff = (ctx.compact !== true || type !== kObjectType) ? 2 : 3;
    ctx.indentationLvl += diff;
    str = formatValue(ctx, desc.value, recurseTimes);
    if (diff === 3 && ctx.breakLength < getStringWidth(str, ctx.colors)) {
      extra = `\n${' '.repeat(ctx.indentationLvl)}`;
    }
    ctx.indentationLvl -= diff;
  } else if (desc.get !== undefined) {
    str = ctx.stylize(desc.set !== undefined ? '[Getter/Setter]' : '[Getter]', 'special');
  } else if (desc.set !== undefined) {
    str = ctx.stylize('[Setter]', 'special');
  } else {
    str = ctx.stylize('undefined', 'undefined');
  }
  if (type === kArrayType) return str;
  if (typeof key === 'symbol') {
    name = ctx.stylize(Symbol.prototype.toString.call(key), 'symbol');
  } else if (key === '__proto__') {
    name = "['__proto__']";
  } else if (desc.enumerable === false) {
    name = `[${keyIdentifier.test(key) ? key : strEscape(key)}]`;
  } else if (keyIdentifier.test(key)) {
    name = ctx.stylize(key, 'name');
  } else {
    name = ctx.stylize(strEscape(key), 'string');
  }
  return `${name}:${extra}${str}`;
}

function getStringWidth(str, removeControlChars = true) {
  if (removeControlChars) str = removeColors(str);
  return str.length;
}

function isBelowBreakLength(ctx, output, start, base) {
  let totalLength = output.length + start;
  if (totalLength + output.length > ctx.breakLength) return false;
  for (let i = 0; i < output.length; i++) {
    totalLength += ctx.colors ? removeColors(output[i]).length : output[i].length;
    if (totalLength > ctx.breakLength) return false;
  }
  return base === '' || !base.includes('\n');
}

// Lay out arrays with more than six entries in aligned columns
function groupArrayElements(ctx, output, value) {
  let totalLength = 0;
  let maxLength = 0;
  let i = 0;
  let outputLength = output.length;
  if (ctx.maxArrayLength < output.length) outputLength--;
  const separatorSpace = 2;
  const dataLen = new Array(outputLength);
  for (; i < outputLength; i++) {
    const len = getStringWidth(output[i], ctx.colors);
    dataLen[i] = len;
    totalLength += len + separatorSpace;
    if (maxLength < len) maxLength = len;
  }
  const actualMax = maxLength + separatorSpace;
  if (actualMax * 3 + ctx.indentationLvl < ctx.breakLength &&
      (totalLength / actualMax > 5 || maxLength <= 6)) {
    const approxCharHeights = 2.5;
    const averageBias = Math.sqrt(actualMax - totalLength / output.length);
    const biasedMax = Math.max(actualMax - 3 - averageBias, 1);
    const columns = Math.min(
      Math.round(Math.sqrt(approxCharHeights * biasedMax * outputLength) / biasedMax),
      Math.floor((ctx.breakLength - ctx.indentationLvl) / actualMax),
      ctx.compact * 4,
      15,
    );
    if (columns <= 1) return output;
    const tmp = [];
    const maxLineLength = [];
    for (let i = 0; i < columns; i++) {
      let lineLength = 0;
      for (let j = i; j < output.length; j += columns) {
        if (dataLen[j] > lineLength) lineLength = dataLen[j];
      }
      maxLineLength.push(lineLength + separatorSpace);
    }
    let order = String.prototype.padStart;
    if (value !== undefined) {
      for (let i = 0; i < output.length; i++) {
        if (typeof value[i] !== 'number' && typeof value[i] !== 'bigint') {
          order = String.prototype.padEnd;
          break;
        }
      }
    }
    for (let i = 0; i < outputLength; i += columns) {
      const max = Math.min(i + columns, outputLength);
      let str = '';
      let j = i;
      for (; j < max - 1; j++) {
        const padding = maxLineLength[j - i] + output[j].length - dataLen[j];
        str += order.call(`${output[j]}, `, padding, ' ');
      }
      if (order === String.prototype.padStart) {
        const padding = maxLineLength[j - i] + output[j].length - dataLen[j] - separatorSpace;
        str += output[j].padStart(padding, ' ');
      } else {
        str += output[j];
      }
      tmp.push(str);
    }
    if (ctx.maxArrayLength < output.length) tmp.push(output[outputLength]);
    output = tmp;
  }
  return output;
}

function reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value) {
  if (ctx.compact !== true) {
    if (typeof ctx.compact === 'number' && ctx.compact >= 1) {
      const entries = output.length;
      if (extrasType === kArrayExtrasType && entries > 6) {
        output = groupArrayElements(ctx, output, value);
      }
      if (ctx.currentDepth - recurseTimes < ctx.compact && entries === output.length) {
        const start = output.length + ctx.indentationLvl + braces[0].length + base.length + 10;
        if (isBelowBreakLength(ctx, output, start, base)) {
          const joinedOutput = output.join(', ');
          if (!joinedOutput.includes('\n')) {
            return `${base ? `${base} ` : ''}${braces[0]} ${joinedOutput}` + ` ${braces[1]}`;
          }
        }
      }
    }
    const indentation = `\n${' '.repeat(ctx.indentationLvl)}`;
    return `${base ? `${base} ` : ''}${braces[0]}${indentation}  ${
      output.join(`,${indentation}  `)}${indentation}${braces[1]}`;
  }
  if (isBelowBreakLength(ctx, output, 0, base)) {
    return `${braces[0]}${base ? ` ${base}` : ''} ${output.join(', ')} ` + braces[1];
  }
  const indentation = ' '.repeat(ctx.indentationLvl);
  const ln = base === '' && braces[0].length === 1 ? ' ' : `${base ? ` ${base}` : ''}\n${indentation}  `;
  return `${braces[0]}${ln}${output.join(`,\n${indentation}  `)} ${braces[1]}`;
}

function hasBuiltInToString(value) {
  let proto = value;
  while (proto !== null) {
    const desc = Object.getOwnPropertyDescriptor(proto, 'toString');
    if (desc !== undefined) {
      return desc.value === Object.prototype.toString || desc.value === Error.prototype.toString ||
             desc.value === Date.prototype.toString || desc.value === RegExp.prototype.toString;
    }
    proto = Object.getPrototypeOf(proto);
  }
  return true;
}

// util.format(format, ...args) with %s %d %i %f %j %o %O %c %%
function format(...args) {
  return formatWithOptions(undefined, ...args);
}

function formatWithOptions(inspectOptions, ...args) {
  const first = args[0];
  let a = 0;
  let str = '';
  let join = '';
  const opts = inspectOptions || {};

  if (typeof first === 'string') {
    if (args.length === 1) return first;
    let tempStr;
    let lastPos = 0;
    for (let i = 0; i < first.length - 1; i++) {
      if (first.charCodeAt(i) === 37) { // '%'
        const nextChar = first.charCodeAt(++i);
        if (a + 1 !== args.length) {
          switch (nextChar) {
            case 115: { // 's'
              const tempArg = args[++a];
              if (typeof tempArg === 'number') tempStr = formatNumber(stylizeNoColor, tempArg);
              else if (typeof tempArg === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempArg);
              else if (typeof tempArg !== 'object' || tempArg === null || !hasBuiltInToString(tempArg)) tempStr = String(tempArg);
              else tempStr = inspect(tempArg, Object.assign({}, opts, { depth: 0, colors: false, compact: 3 }));
              break;
            }
            case 106: // 'j'
              tempStr = tryStringify(args[++a]);
              break;
            case 100: { // 'd'
              const tempNum = args[++a];
              if (typeof tempNum === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempNum);
              else if (typeof tempNum === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, Number(tempNum));
              break;
            }
            case 79: // 'O'
              tempStr = inspect(args[++a], opts);
              break;
            case 111: // 'o'
              tempStr = inspect(args[++a], Object.assign({}, opts, { showHidden: true, showProxy: true, depth: 4 }));
              break;
            case 105: { // 'i'
              const tempInteger = args[++a];
              if (typeof tempInteger === 'bigint') tempStr = formatBigInt(stylizeNoColor, tempInteger);
              else if (typeof tempInteger === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, Number.parseInt(tempInteger));
              break;
            }
            case 102: { // 'f'
              const tempFloat = args[++a];
              if (typeof tempFloat === 'symbol') tempStr = 'NaN';
              else tempStr = formatNumber(stylizeNoColor, Number.parseFloat(tempFloat));
              break;
            }
            case 99: // 'c'
              a += 1;
              tempStr = '';
              break;
            case 37: // '%'
              str += first.slice(lastPos, i);
              lastPos = i + 1;
              continue;
            default:
              continue;
          }
          if (lastPos !== i - 1) str += first.slice(lastPos, i - 1);
          str += tempStr;
          lastPos = i + 1;
        } else if (nextChar === 37) {
          str += first.slice(lastPos, i);
          lastPos = i + 1;
        }
      }
    }
    if (lastPos !== 0) {
      a++;
      join = ' ';
      if (lastPos < first.length) str += first.slice(lastPos);
    }
  }

  while (a < args.length) {
    const value = args[a];
    str += join;
    str += typeof value !== 'string' ? inspect(value, opts) : value;
    join = ' ';
    a++;
  }
  return str;
}

function tryStringify(arg) {
  try {
    return JSON.stringify(arg);
  } catch (err) {
    if (err instanceof TypeError && /circular/i.test(err.message)) return '[Circular]';
    throw err;
  }
}

function isDeepStrictEqual(a, b) {
  if (Object.is(a, b)) return true;
  if (typeof a !== 'object' || typeof b !== 'object' || a === null || b === null) return false;
  if (Object.getPrototypeOf(a) !== Object.getPrototypeOf(b)) return false;
  const keysA = Object.keys(a);
  const keysB = Object.keys(b);
  if (keysA.length !== keysB.length) return false;
  return keysA.every((key) => Object.prototype.hasOwnProperty.call(b, key) && isDeepStrictEqual(a[key], b[key]));
}

function inherits(ctor, superCtor) {
  Object.defineProperty(ctor, 'super_', { value: superCtor, writable: true, configurable: true });
  Object.setPrototypeOf(ctor.prototype, superCtor.prototype);
}

const kCustomPromisify = Symbol.for('nodejs.util.promisify.custom');

function promisify(original) {
  if (typeof original !== 'function') {
    const err = new TypeError('The "original" argument must be of type function');
    err.code = 'ERR_INVALID_ARG_TYPE';
    throw err;
  }
  // A function can say how it is promisified (child_process.exec does)
  if (typeof original[kCustomPromisify] === 'function') return original[kCustomPromisify];
  return function (...args) {
    return new Promise((resolve, reject) => {
      original.call(this, ...args, (err, value) => (err ? reject(err) : resolve(value)));
    });
  };
}
promisify.custom = kCustomPromisify;

return {
  inspect,
  format,
  formatWithOptions,
  isDeepStrictEqual,
  inherits,
  promisify,
  stripVTControlCharacters: removeColors,
};
})
)JS";

static thread_local Persistent<Function> g_inspect;
static thread_local Persistent<Function> g_format;

// getProxyTarget(value): the target of a Proxy (null if revoked), else undefined
static void GetProxyTarget(const FunctionCallbackInfo<Value>& args) {
    if (args.Length() < 1 || !args[0]->IsProxy()) return;
    Local<Value> target = args[0].As<Proxy>()->GetTarget();
    if (target->IsNull() || target->IsUndefined()) {
        args.GetReturnValue().SetNull();
    } else {
        args.GetReturnValue().Set(target);
    }
}

// getPromiseDetails(promise): [state, result]
static void GetPromiseDetails(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    if (args.Length() < 1 || !args[0]->IsPromise()) return;
    Local<Promise> promise = args[0].As<Promise>();

    const char* state = "pending";
    Local<Value> result = Undefined(isolate);
    if (promise->State() == Promise::kFulfilled) {
        state = "fulfilled";
        result = promise->Result();
    } else if (promise->State() == Promise::kRejected) {
        state = "rejected";
        result = promise->Result();
    }
    Local<Array> details = Array::New(isolate, 2);
    details->Set(context, 0, String::NewFromUtf8(isolate, state).ToLocalChecked()).Check();
    details->Set(context, 1, result).Check();
    args.GetReturnValue().Set(details);
}

static void IsBoxedPrimitive(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(args.Length() > 0 && (args[0]->IsNumberObject() || args[0]->IsStringObject() ||
        args[0]->IsBooleanObject() || args[0]->IsBigIntObject() || args[0]->IsSymbolObject()));
}

static void IsBufferValue(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(args.Length() > 0 && IsBuffer(args[0]));
}

// bufferHex(buffer, max): { hex: first max bytes as hex, length }
static void BufferHex(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    if (args.Length() < 2 || !IsBuffer(args[0])) return;

    BufferData* data = GetBufferData(args[0].As<Object>());
    size_t length = data != nullptr ? data->length : 0;
    size_t max = static_cast<size_t>(args[1]->IntegerValue(context).FromMaybe(50));
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (size_t i = 0; i < length && i < max; i++) {
        unsigned char byte = static_cast<unsigned char>(data->data[i]);
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0f];
    }

    Local<Object> result = Object::New(isolate);
    result->Set(context, String::NewFromUtf8(isolate, "hex").ToLocalChecked(),
        String::NewFromUtf8(isolate, hex.c_str()).ToLocalChecked()).Check();
    result->Set(context, String::NewFromUtf8(isolate, "length").ToLocalChecked(),
        Number::New(isolate, static_cast<double>(length))).Check();
    args.GetReturnValue().Set(result);
}

namespace frogjs {

std::string Inspect(Isolate* isolate, Local<Context> context, Local<Value> value, bool colors) {
    HandleScope handle_scope(isolate);
    if (!g_inspect.IsEmpty()) {
        TryCatch try_catch(isolate);
        Local<Object> options = Object::New(isolate);
        options->Set(context, String::NewFromUtf8(isolate, "colors").ToLocalChecked(),
            Boolean::New(isolate, colors)).Check();
        Local<Value> argv[2] = {value, options};
        Local<Value> result;
        if (Local<Function>::New(isolate, g_inspect)->Call(context, Undefined(isolate), 2, argv).ToLocal(&result)) {
            String::Utf8Value str(isolate, result);
            if (*str != nullptr) return std::string(*str, str.length());
        }
    }

    // Fallback: an error's stack, or String(value)
    Local<Value> shown = value;
    if (value->IsNativeError()) {
        Local<Value> stack;
        if (value.As<Object>()->Get(context, String::NewFromUtf8(isolate, "stack").ToLocalChecked()).ToLocal(&stack) &&
            stack->IsString()) {
            shown = stack;
        }
    }
    TryCatch try_catch(isolate);
    String::Utf8Value str(isolate, shown);
    return *str != nullptr ? std::string(*str, str.length()) : std::string("<unprintable value>");
}

bool Format(Isolate* isolate, Local<Context> context, const FunctionCallbackInfo<Value>& args,
            std::string* out) {
    HandleScope handle_scope(isolate);
    out->clear();
    if (!g_format.IsEmpty()) {
        std::vector<Local<Value>> argv;
        for (int i = 0; i < args.Length(); i++) argv.push_back(args[i]);
        Local<Value> result;
        if (!Local<Function>::New(isolate, g_format)->Call(context, Undefined(isolate),
                static_cast<int>(argv.size()), argv.data()).ToLocal(&result)) {
            return false;
        }
        String::Utf8Value str(isolate, result);
        if (*str != nullptr) out->assign(*str, str.length());
        return true;
    }

    for (int i = 0; i < args.Length(); i++) {
        if (i > 0) *out += " ";
        String::Utf8Value str(isolate, args[i]);
        if (*str != nullptr) out->append(*str, str.length());
    }
    return true;
}

}  // namespace frogjs

// Set up util module: inspect, format, ... exposed as global `util` and
// returned by require('util')
void SetupUtil(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "getProxyTarget", GetProxyTarget);
    frogjs::SetFunction(isolate, context, native, "getPromiseDetails", GetPromiseDetails);
    frogjs::SetFunction(isolate, context, native, "isBoxedPrimitive", IsBoxedPrimitive);
    frogjs::SetFunction(isolate, context, native, "isBuffer", IsBufferValue);
    frogjs::SetFunction(isolate, context, native, "bufferHex", BufferHex);

    Local<Value> argv[1] = {native};
    Local<Value> util;
    if (!frogjs::RunJsModule(isolate, context, "util", kUtilSource, 1, argv).ToLocal(&util) || !util->IsObject()) {
        return;
    }
    Local<Object> util_obj = util.As<Object>();

    Local<Value> inspect = util_obj->Get(context, String::NewFromUtf8(isolate, "inspect").ToLocalChecked()).ToLocalChecked();
    Local<Value> format = util_obj->Get(context, String::NewFromUtf8(isolate, "format").ToLocalChecked()).ToLocalChecked();
    g_inspect.Reset(isolate, inspect.As<Function>());
    g_format.Reset(isolate, format.As<Function>());

    context->Global()->Set(context, String::NewFromUtf8(isolate, "util").ToLocalChecked(), util).Check();
}
