#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The querystring module, following Node.js: parse() and stringify() with
// custom separators, repeated keys as arrays, '+' as a space, and lenient
// decoding of malformed percent-escapes.
static const char* kQuerystringSource = R"JS(
(function () {
'use strict';

const hexValue = (code) => {
  if (code >= 48 && code <= 57) return code - 48;
  if (code >= 65 && code <= 70) return code - 55;
  if (code >= 97 && code <= 102) return code - 87;
  return -1;
};

// Decode %XX escapes into bytes; anything else is kept as UTF-8
function unescapeBuffer(text, decodeSpaces) {
  const bytes = new TextEncoder().encode(String(text));
  const out = new Uint8Array(bytes.length);
  let length = 0;
  for (let i = 0; i < bytes.length; i++) {
    const byte = bytes[i];
    if (byte === 0x2b && decodeSpaces) {
      out[length++] = 0x20;
      continue;
    }
    if (byte === 0x25 && i + 2 < bytes.length) {
      const high = hexValue(bytes[i + 1]);
      const low = hexValue(bytes[i + 2]);
      if (high >= 0 && low >= 0) {
        out[length++] = high * 16 + low;
        i += 2;
        continue;
      }
    }
    out[length++] = byte;
  }
  const result = Buffer.from(out.buffer, out.byteOffset, length);
  return result;
}

function unescape(text, decodeSpaces) {
  try {
    return decodeURIComponent(text);
  } catch {
    return unescapeBuffer(text, decodeSpaces).toString();
  }
}

function escape(text) {
  if (typeof text !== 'string') {
    if (typeof text === 'object') text = String(text);
    else text += '';
  }
  try {
    return encodeURIComponent(text);
  } catch {
    const err = new URIError('URI malformed');
    err.code = 'ERR_INVALID_URI';
    throw err;
  }
}

function stringifyPrimitive(value) {
  if (typeof value === 'string') return value;
  if (typeof value === 'number' && Number.isFinite(value)) return '' + value;
  if (typeof value === 'bigint') return '' + value;
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  return '';
}

function stringify(object, sep, eq, options) {
  sep = sep || '&';
  eq = eq || '=';
  const encode = options && typeof options.encodeURIComponent === 'function' ? options.encodeURIComponent : escape;
  if (object === null || typeof object !== 'object') return '';
  const fields = [];
  for (const key of Object.keys(object)) {
    const value = object[key];
    const name = encode(stringifyPrimitive(key)) + eq;
    if (Array.isArray(value)) {
      for (const item of value) fields.push(name + encode(stringifyPrimitive(item)));
    } else {
      fields.push(name + encode(stringifyPrimitive(value)));
    }
  }
  return fields.join(sep);
}

function parse(text, sep, eq, options) {
  const result = { __proto__: null };
  if (typeof text !== 'string' || text.length === 0) return result;
  sep = sep ? String(sep) : '&';
  eq = eq ? String(eq) : '=';
  let maxKeys = 1000;
  if (options && typeof options.maxKeys === 'number') maxKeys = options.maxKeys;
  if (!(maxKeys > 0)) maxKeys = Infinity;
  const decode = options && typeof options.decodeURIComponent === 'function' ? options.decodeURIComponent : null;
  const decodeOne = (value) => {
    const spaced = value.replace(/\+/g, ' ');
    if (decode) {
      try {
        return decode(spaced);
      } catch {
        return unescape(spaced, true);
      }
    }
    return spaced.includes('%') ? unescape(spaced, true) : spaced;
  };

  let pairs = 0;
  for (const part of text.split(sep)) {
    if (pairs >= maxKeys) break;
    if (part.length === 0 && sep.length > 0) continue;
    pairs++;
    const index = part.indexOf(eq);
    const rawKey = index >= 0 ? part.slice(0, index) : part;
    const rawValue = index >= 0 ? part.slice(index + eq.length) : '';
    const key = decodeOne(rawKey);
    const value = decodeOne(rawValue);
    if (!(key in result)) result[key] = value;
    else if (Array.isArray(result[key])) result[key].push(value);
    else result[key] = [result[key], value];
  }
  return result;
}

return { parse, stringify, escape, unescape, unescapeBuffer, decode: parse, encode: stringify };
})
)JS";

// Set up the querystring module (requires SetupBuffer first)
void SetupQuerystring(Isolate* isolate, Local<Context> context) {
    Local<Value> module;
    if (!frogjs::RunJsModule(isolate, context, "querystring", kQuerystringSource, 0, nullptr).ToLocal(&module)) return;
    frogjs::RegisterBuiltin(isolate, context, "querystring", module);
}
