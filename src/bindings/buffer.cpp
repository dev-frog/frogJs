#include <v8.h>
#include "frogjs/allocator.h"
#include "frogjs/buffer.h"
#include "frogjs/encoding.h"
#include "frogjs/js_module.h"
#include "frogjs/v8_compat.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace v8;

// Buffer is a Uint8Array subclass, as in Node.js. The bytes live in an
// ArrayBuffer, so V8 owns them: they are freed with the last Buffer or view
// using them, count as external memory for the GC, and every access is bounds
// checked. Everything except the encoding conversions is JavaScript.
static const char* kBufferSource = R"JS(
(function (native) {
'use strict';

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function received(value) {
  if (value === null || value === undefined) return `Received ${value}`;
  if (typeof value === 'function') return `Received function ${value.name}`;
  if (typeof value === 'object') {
    const name = value.constructor && value.constructor.name;
    return name ? `Received an instance of ${name}` : 'Received an object';
  }
  let shown = typeof value === 'string' ? `'${value.slice(0, 25)}'` : String(value);
  if (typeof value === 'bigint') shown += 'n';
  return `Received type ${typeof value} (${shown})`;
}

function invalidType(name, expected, value) {
  return makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. ${received(value)}`);
}

function outOfRange(name, range, value) {
  return makeError(RangeError, 'ERR_OUT_OF_RANGE',
    `The value of "${name}" is out of range. It must be ${range}. Received ${String(value)}`);
}

function validateSize(size) {
  if (typeof size !== 'number') throw invalidType('size', 'of type number', size);
  if (!(size >= 0 && size <= Number.MAX_SAFE_INTEGER)) {
    throw outOfRange('size', `>= 0 && <= ${Number.MAX_SAFE_INTEGER}`, size);
  }
  if (size > native.maxLength) {
    throw makeError(RangeError, 'ERR_BUFFER_TOO_LARGE', `Cannot create a Buffer larger than ${native.maxLength} bytes`);
  }
}

function isSharedArrayBuffer(value) {
  return Object.prototype.toString.call(value) === '[object SharedArrayBuffer]';
}

// An offset argument: an integer within [0, max]
function validateOffset(name, value, max) {
  if (typeof value !== 'number') throw invalidType(name, 'of type number', value);
  if (!Number.isInteger(value)) throw outOfRange(name, 'an integer', value);
  if (value < 0 || value > max) throw outOfRange(name, `>= 0 && <= ${max}`, value);
}

// Small Buffers are carved out of a shared block: allocating an ArrayBuffer for
// each is much slower. A block is zero-filled and each byte is handed out once,
// so a new Buffer never shows old data. (Like in Node.js, buf.buffer of a small
// Buffer can be larger than the Buffer; a transferred block is simply replaced.)
const POOL_SIZE = 8192;
let pool = null;
let poolOffset = 0;

function fromPool(size) {
  if (size >= POOL_SIZE >>> 1) return new Buffer(size);
  if (pool === null || pool.byteLength === 0 || poolOffset + size > POOL_SIZE) {
    pool = new ArrayBuffer(POOL_SIZE);
    poolOffset = 0;
  }
  const buffer = new Buffer(pool, poolOffset, size);
  poolOffset = (poolOffset + size + 7) & ~7;
  return buffer;
}

class Buffer extends Uint8Array {
  // new Buffer() is deprecated in Node.js but still works there: prefer
  // Buffer.alloc() and Buffer.from()
  constructor(arg, offsetOrEncoding, length) {
    if (typeof arg === 'number') {
      validateSize(arg);
      super(arg);
    } else if (arg instanceof ArrayBuffer) {
      super(arg, offsetOrEncoding, length);
    } else if (arg === undefined) {
      super(0);
    } else {
      return Buffer.from(arg, offsetOrEncoding, length);
    }
  }

  static alloc(size, fill, encoding) {
    validateSize(size);
    const buffer = new Buffer(size);
    if (fill !== undefined && fill !== 0 && size > 0) buffer.fill(fill, encoding);
    return buffer;
  }

  // Always zero-filled: there is no uninitialized memory to read in FrogJS
  static allocUnsafe(size) {
    validateSize(size);
    return fromPool(size);
  }

  static allocUnsafeSlow(size) {
    validateSize(size);
    return new Buffer(size);
  }

  static from(value, encodingOrOffset, length) {
    if (typeof value === 'string') {
      if (encodingOrOffset === 'hex' || encodingOrOffset === 'base64') {
        const room = fromPool(encodingOrOffset === 'hex' ? value.length >>> 1 : (value.length >>> 2) * 3 + 3);
        const written = native.decodeInto(room, value, encodingOrOffset === 'hex');
        return written === room.length ? room : room.subarray(0, written);
      }
      if (encodingOrOffset !== undefined && encodingOrOffset !== 'utf8') return native.decode(value, encodingOrOffset);
      const buffer = fromPool(native.utf8Length(value));
      if (buffer.length > 0) native.utf8Write(buffer, 0, value);
      return buffer;
    }
    if (value instanceof ArrayBuffer) return new Buffer(value, encodingOrOffset, length);
    if (isSharedArrayBuffer(value)) {
      // A copy: FrogJS Buffers don't live in shared memory
      const copy = new Buffer(value.byteLength);
      copy.set(new Uint8Array(value));
      return copy;
    }
    if (value instanceof DataView) {
      const copy = new Buffer(value.byteLength);
      copy.set(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
      return copy;
    }
    if (value !== null && typeof value === 'object') {
      if (value.type === 'Buffer' && Array.isArray(value.data)) return Buffer.from(value.data);
      if (typeof value.length === 'number') {
        const size = Math.max(0, Math.trunc(value.length) || 0);
        const copy = fromPool(size);
        // Element values wrap modulo 256 (typed arrays too, as in Node.js)
        for (let i = 0; i < size; i++) copy[i] = value[i];
        return copy;
      }
    }
    throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE',
      'The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an ' +
      `Array-like Object. ${received(value)}`);
  }

  static concat(list, totalLength) {
    if (!Array.isArray(list)) throw invalidType('list', 'an instance of Array', list);
    let total = 0;
    list.forEach((item, i) => {
      if (!(item instanceof Uint8Array)) {
        throw invalidType(`list[${i}]`, 'an instance of Buffer or Uint8Array', item);
      }
      total += item.length;
    });
    if (totalLength !== undefined) {
      validateOffset('length', totalLength, Number.MAX_SAFE_INTEGER);
      total = totalLength;
    }
    const result = fromPool(total);
    let offset = 0;
    for (const item of list) {
      if (offset >= total) break;
      const part = offset + item.length > total ? item.subarray(0, total - offset) : item;
      result.set(part, offset);
      offset += part.length;
    }
    return result;
  }

  static isBuffer(value) {
    return value instanceof Buffer;
  }

  static isEncoding(encoding) {
    return typeof encoding === 'string' && native.isEncoding(encoding);
  }

  static byteLength(value, encoding) {
    if (typeof value === 'string') return native.byteLength(value, encoding);
    if (ArrayBuffer.isView(value) || value instanceof ArrayBuffer || isSharedArrayBuffer(value)) {
      return value.byteLength;
    }
    throw invalidType('string', 'of type string or an instance of Buffer or ArrayBuffer', value);
  }

  static compare(a, b) {
    if (!(a instanceof Uint8Array)) throw invalidType('buf1', 'an instance of Buffer or Uint8Array', a);
    if (!(b instanceof Uint8Array)) throw invalidType('buf2', 'an instance of Buffer or Uint8Array', b);
    const length = Math.min(a.length, b.length);
    for (let i = 0; i < length; i++) {
      if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return a.length === b.length ? 0 : (a.length < b.length ? -1 : 1);
  }

  toString(encoding, start, end) {
    return native.encode(this, encoding, start, end);
  }

  toLocaleString(encoding, start, end) {
    return this.toString(encoding, start, end);
  }

  toJSON() {
    return { type: 'Buffer', data: Array.from(this) };
  }

  // write(string[, offset[, length]][, encoding])
  write(string, offset, length, encoding) {
    if (typeof string !== 'string') throw invalidType('string', 'of type string', string);
    if (typeof offset === 'string') {
      encoding = offset;
      offset = 0;
      length = undefined;
    } else if (typeof length === 'string') {
      encoding = length;
      length = undefined;
    }
    if (offset === undefined) offset = 0;
    validateOffset('offset', offset, this.length);
    const remaining = this.length - offset;
    if (length === undefined) length = remaining;
    else validateOffset('length', length, Number.MAX_SAFE_INTEGER);
    return native.write(this, string, offset, Math.min(length, remaining), encoding);
  }

  // A view of the same memory, like subarray()
  slice(start, end) {
    return this.subarray(start, end);
  }

  equals(other) {
    if (!(other instanceof Uint8Array)) throw invalidType('otherBuffer', 'an instance of Buffer or Uint8Array', other);
    return Buffer.compare(this, other) === 0;
  }

  compare(target, targetStart = 0, targetEnd = target.length, sourceStart = 0, sourceEnd = this.length) {
    if (!(target instanceof Uint8Array)) throw invalidType('target', 'an instance of Buffer or Uint8Array', target);
    validateOffset('targetStart', targetStart, target.length);
    validateOffset('targetEnd', targetEnd, target.length);
    validateOffset('sourceStart', sourceStart, this.length);
    validateOffset('sourceEnd', sourceEnd, this.length);
    return Buffer.compare(this.subarray(sourceStart, sourceEnd), target.subarray(targetStart, targetEnd));
  }

  copy(target, targetStart = 0, sourceStart = 0, sourceEnd = this.length) {
    if (!(target instanceof Uint8Array)) throw invalidType('target', 'an instance of Buffer or Uint8Array', target);
    validateOffset('targetStart', targetStart, Number.MAX_SAFE_INTEGER);
    validateOffset('sourceStart', sourceStart, this.length);
    validateOffset('sourceEnd', sourceEnd, Number.MAX_SAFE_INTEGER);
    if (targetStart >= target.length) return 0;
    const end = Math.min(sourceEnd, this.length, sourceStart + (target.length - targetStart));
    if (end <= sourceStart) return 0;
    target.set(this.subarray(sourceStart, end), targetStart);
    return end - sourceStart;
  }

  // fill(value[, offset[, end]][, encoding])
  fill(value, offset, end, encoding) {
    if (typeof offset === 'string') {
      encoding = offset;
      offset = undefined;
      end = undefined;
    } else if (typeof end === 'string') {
      encoding = end;
      end = undefined;
    }
    const start = offset === undefined ? 0 : offset;
    const stop = end === undefined ? this.length : end;
    validateOffset('offset', start, Number.MAX_SAFE_INTEGER);
    validateOffset('end', stop, this.length);
    if (start >= stop) return this;

    let pattern = value;
    if (typeof value === 'string') pattern = native.decode(value, encoding);
    if (typeof pattern === 'number' || typeof pattern === 'boolean') {
      super.fill(pattern & 255, start, stop);
      return this;
    }
    if (!(pattern instanceof Uint8Array)) {
      throw invalidType('value', 'of type string, number or an instance of Buffer or Uint8Array', value);
    }
    if (pattern.length === 0) {
      if (typeof value === 'string' && value.length === 0) {
        super.fill(0, start, stop);
        return this;
      }
      throw makeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'value' is invalid. Received ${String(value)}`);
    }
    for (let i = start; i < stop; i++) this[i] = pattern[(i - start) % pattern.length];
    return this;
  }

  indexOf(value, byteOffset, encoding) {
    return search(this, value, byteOffset, encoding, true);
  }

  lastIndexOf(value, byteOffset, encoding) {
    return search(this, value, byteOffset, encoding, false);
  }

  includes(value, byteOffset, encoding) {
    return this.indexOf(value, byteOffset, encoding) !== -1;
  }
}

function search(buffer, value, byteOffset, encoding, forward) {
  if (typeof byteOffset === 'string') {
    encoding = byteOffset;
    byteOffset = undefined;
  }
  let needle;
  if (typeof value === 'number') needle = [value & 255];
  else if (typeof value === 'string') needle = native.decode(value, encoding);
  else if (value instanceof Uint8Array) needle = value;
  else throw invalidType('value', 'one of type number or string or an instance of Buffer or Uint8Array', value);

  const lastStart = buffer.length - needle.length;
  let start = Number(byteOffset);
  if (Number.isNaN(start)) start = forward ? 0 : lastStart;
  start = Math.trunc(start);
  if (start < 0) start += buffer.length;
  if (forward) {
    if (start < 0) start = 0;
    if (needle.length === 0) return Math.min(start, buffer.length);
  } else {
    if (start < 0) return -1;
    if (needle.length === 0) return Math.min(start, buffer.length);
    start = Math.min(start, lastStart);
  }

  const step = forward ? 1 : -1;
  for (let i = start; i >= 0 && i <= lastStart; i += step) {
    let j = 0;
    while (j < needle.length && buffer[i + j] === needle[j]) j++;
    if (j === needle.length) return i;
  }
  return -1;
}

function checkBounds(buffer, offset, size) {
  if (typeof offset !== 'number') throw invalidType('offset', 'of type number', offset);
  if (!Number.isInteger(offset)) throw outOfRange('offset', 'an integer', offset);
  if (buffer.length < size) {
    throw makeError(RangeError, 'ERR_BUFFER_OUT_OF_BOUNDS', 'Attempt to access memory outside buffer bounds');
  }
  if (offset < 0 || offset > buffer.length - size) {
    throw outOfRange('offset', `>= 0 and <= ${buffer.length - size}`, offset);
  }
}

// readUInt8, writeInt32LE, readDoubleBE, ... (with the readUint8 spellings too).
// The integer ones work on the bytes directly: they are hot in binary protocols.
function defineMethod(name, fn) {
  Object.defineProperty(Buffer.prototype, name, { value: fn, writable: true, configurable: true });
  if (name.includes('UInt')) {
    Object.defineProperty(Buffer.prototype, name.replace('UInt', 'Uint'), { value: fn, writable: true, configurable: true });
  }
}

function checkValue(value, min, max) {
  if (value < min || value > max || value !== value) throw outOfRange('value', `>= ${min} and <= ${max}`, value);
}

defineMethod('readUInt8', function (offset = 0) {
  checkBounds(this, offset, 1);
  return this[offset];
});
defineMethod('readInt8', function (offset = 0) {
  checkBounds(this, offset, 1);
  return (this[offset] << 24) >> 24;
});
defineMethod('writeUInt8', function (value, offset = 0) {
  value = +value;
  checkValue(value, 0, 255);
  checkBounds(this, offset, 1);
  this[offset] = value;
  return offset + 1;
});
defineMethod('writeInt8', function (value, offset = 0) {
  value = +value;
  checkValue(value, -128, 127);
  checkBounds(this, offset, 1);
  this[offset] = value;
  return offset + 1;
});

function defineInt(bits, little) {
  const size = bits / 8;
  const suffix = little ? 'LE' : 'BE';
  // The position of byte i, counted from the least significant one
  const at = little ? (offset, i) => offset + i : (offset, i) => offset + size - 1 - i;
  const unsignedMax = bits === 16 ? 65535 : 4294967295;
  const signedMax = bits === 16 ? 32767 : 2147483647;
  const signedMin = -signedMax - 1;

  const readBits = bits === 16
    ? (b, o) => b[at(o, 0)] | (b[at(o, 1)] << 8)
    : (b, o) => b[at(o, 0)] | (b[at(o, 1)] << 8) | (b[at(o, 2)] << 16) | (b[at(o, 3)] << 24);
  const writeBits = bits === 16
    ? (b, o, v) => {
      b[at(o, 0)] = v;
      b[at(o, 1)] = v >>> 8;
    }
    : (b, o, v) => {
      b[at(o, 0)] = v;
      b[at(o, 1)] = v >>> 8;
      b[at(o, 2)] = v >>> 16;
      b[at(o, 3)] = v >>> 24;
    };

  defineMethod(`readUInt${bits}${suffix}`, bits === 16
    ? function (offset = 0) {
      checkBounds(this, offset, size);
      return readBits(this, offset);
    }
    : function (offset = 0) {
      checkBounds(this, offset, size);
      return readBits(this, offset) >>> 0;
    });
  defineMethod(`readInt${bits}${suffix}`, bits === 16
    ? function (offset = 0) {
      checkBounds(this, offset, size);
      return (readBits(this, offset) << 16) >> 16;
    }
    : function (offset = 0) {
      checkBounds(this, offset, size);
      return readBits(this, offset);
    });
  defineMethod(`writeUInt${bits}${suffix}`, function (value, offset = 0) {
    value = +value;
    checkValue(value, 0, unsignedMax);
    checkBounds(this, offset, size);
    writeBits(this, offset, value);
    return offset + size;
  });
  defineMethod(`writeInt${bits}${suffix}`, function (value, offset = 0) {
    value = +value;
    checkValue(value, signedMin, signedMax);
    checkBounds(this, offset, size);
    writeBits(this, offset, value);
    return offset + size;
  });
}

for (const bits of [16, 32]) {
  defineInt(bits, true);
  defineInt(bits, false);
}

// Floats and 64-bit integers go through a DataView
function defineViewMethods(type, view, size, min, max, suffix, littleEndian) {
  const getter = `get${view}`;
  const setter = `set${view}`;
  const isBigInt = typeof min === 'bigint';
  defineMethod(`read${type}${suffix}`, function (offset = 0) {
    checkBounds(this, offset, size);
    return new DataView(this.buffer, this.byteOffset, this.length)[getter](offset, littleEndian);
  });
  defineMethod(`write${type}${suffix}`, function (value, offset = 0) {
    if (isBigInt && typeof value !== 'bigint') throw invalidType('value', 'of type bigint', value);
    if (!isBigInt) value = +value;
    if (min !== undefined && (value < min || value > max)) {
      throw outOfRange('value', `>= ${min} and <= ${max}`, value);
    }
    checkBounds(this, offset, size);
    new DataView(this.buffer, this.byteOffset, this.length)[setter](offset, value, littleEndian);
    return offset + size;
  });
}

for (const [type, view, size, min, max] of [
  ['BigUInt64', 'BigUint64', 8, 0n, 18446744073709551615n],
  ['BigInt64', 'BigInt64', 8, -9223372036854775808n, 9223372036854775807n],
  ['Float', 'Float32', 4],
  ['Double', 'Float64', 8],
]) {
  defineViewMethods(type, view, size, min, max, 'LE', true);
  defineViewMethods(type, view, size, min, max, 'BE', false);
}

Buffer.poolSize = POOL_SIZE;
return Buffer;
})
)JS";

// The Buffer constructor of this thread
static thread_local Global<Function> g_buffer_ctor;

bool IsBuffer(Local<Value> value) {
    if (g_buffer_ctor.IsEmpty() || !value->IsUint8Array()) return false;
    Isolate* isolate = Isolate::GetCurrent();
    return value->InstanceOf(isolate->GetCurrentContext(), g_buffer_ctor.Get(isolate)).FromMaybe(false);
}

BufferData GetBufferData(Local<Object> view_object) {
    Local<ArrayBufferView> view = view_object.As<ArrayBufferView>();
    size_t length = view->ByteLength();
    if (length == 0) return BufferData{nullptr, 0};
    return BufferData{static_cast<char*>(view->Buffer()->Data()) + view->ByteOffset(), length};
}

Local<Object> NewBuffer(Isolate* isolate, Local<Context> context, const char* data, size_t length) {
    EscapableHandleScope handle_scope(isolate);
    // Filled right away: zeroing it first would be wasted work on every socket read
    std::unique_ptr<BackingStore> store = ArrayBuffer::NewBackingStore(isolate, length,
        BackingStoreInitializationMode::kUninitialized);
    if (length > 0) memcpy(store->Data(), data, length);
    Local<ArrayBuffer> bytes = ArrayBuffer::New(isolate, std::move(store));

    Local<Value> argv[1] = {bytes};
    Local<Object> buffer;
    // Only fails when a worker is being stopped: nobody will look at the result
    if (g_buffer_ctor.IsEmpty() || !g_buffer_ctor.Get(isolate)->NewInstance(context, 1, argv).ToLocal(&buffer)) {
        buffer = Uint8Array::New(bytes, 0, length);
    }
    return handle_scope.Escape(buffer);
}

// utf8Write(view, offset, string): the caller made room for utf8Length(string) bytes
static void Utf8Write(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    BufferData buffer = GetBufferData(args[0].As<Object>());
    size_t offset = static_cast<size_t>(args[1].As<Number>()->Value());
    if (offset <= buffer.length && buffer.length > 0) {
        frogjs::WriteUtf8(isolate, args[2].As<String>(), buffer.data + offset, buffer.length - offset);
    }
}

// decodeInto(buffer, string, isHex) -> bytes written. The buffer has room for
// MaxDecodedLength() bytes (JavaScript computes the same number).
static void DecodeIntoBuffer(const FunctionCallbackInfo<Value>& args) {
    BufferData buffer = GetBufferData(args[0].As<Object>());
    frogjs::Encoding encoding = args[2]->IsTrue() ? frogjs::Encoding::kHex : frogjs::Encoding::kBase64;
    Local<String> str = args[1].As<String>();
    if (buffer.length < frogjs::MaxDecodedLength(str->Length(), encoding)) return args.GetReturnValue().Set(0);
    args.GetReturnValue().Set(static_cast<double>(frogjs::DecodeInto(args.GetIsolate(), str, encoding, buffer.data)));
}

// utf8Length(string)
static void Utf8LengthOf(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(static_cast<double>(frogjs::Utf8Length(args.GetIsolate(), args[0].As<String>())));
}

// decode(string[, encoding]) -> Buffer
static void Decode(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    frogjs::Encoding encoding;
    if (!frogjs::GetEncodingArg(isolate, args[1], frogjs::Encoding::kUtf8, &encoding)) return;
    std::string bytes = frogjs::DecodeString(isolate, args[0].As<String>(), encoding);
    args.GetReturnValue().Set(NewBuffer(isolate, isolate->GetCurrentContext(), bytes.data(), bytes.size()));
}

// A position argument of toString(): a number clamped to [0, length]
static size_t ClampPosition(Local<Context> context, Local<Value> value, size_t fallback, size_t length) {
    if (value->IsUndefined()) return fallback;
    double position = value->NumberValue(context).FromMaybe(0);
    if (!(position > 0)) return 0;  // also NaN
    return position >= static_cast<double>(length) ? length : static_cast<size_t>(position);
}

// encode(buffer[, encoding[, start[, end]]]) -> string
static void Encode(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    frogjs::Encoding encoding;
    if (!frogjs::GetEncodingArg(isolate, args[1], frogjs::Encoding::kUtf8, &encoding)) return;
    if (!args[0]->IsUint8Array()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8Literal(isolate,
            "Buffer.prototype.toString called on an incompatible receiver")));
        return;
    }

    BufferData buffer = GetBufferData(args[0].As<Object>());
    size_t start = ClampPosition(context, args[2], 0, buffer.length);
    size_t end = ClampPosition(context, args[3], buffer.length, buffer.length);
    if (end <= start) {
        args.GetReturnValue().Set(String::Empty(isolate));
        return;
    }
    Local<String> result;
    if (frogjs::EncodeBytes(isolate, buffer.data + start, end - start, encoding).ToLocal(&result)) {
        args.GetReturnValue().Set(result);
    }
}

// write(buffer, string, offset, length[, encoding]) -> bytes written. A UTF-8
// character that doesn't fit completely isn't written at all. JavaScript has
// checked that offset + length is within the buffer.
static void Write(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    frogjs::Encoding encoding;
    if (!frogjs::GetEncodingArg(isolate, args[4], frogjs::Encoding::kUtf8, &encoding)) return;

    BufferData buffer = GetBufferData(args[0].As<Object>());
    size_t offset = static_cast<size_t>(args[2]->NumberValue(context).FromMaybe(0));
    size_t length = static_cast<size_t>(args[3]->NumberValue(context).FromMaybe(0));
    if (offset > buffer.length || length > buffer.length - offset) return;

    std::string bytes = frogjs::DecodeString(isolate, args[1].As<String>(), encoding);
    size_t count = std::min(bytes.size(), length);
    if (encoding == frogjs::Encoding::kUtf8 && count < bytes.size()) {
        // Back up over continuation bytes to the start of the cut character
        while (count > 0 && (static_cast<unsigned char>(bytes[count]) & 0xC0) == 0x80) count--;
    }
    if (count > 0) memcpy(buffer.data + offset, bytes.data(), count);
    args.GetReturnValue().Set(static_cast<double>(count));
}

// byteLength(string[, encoding])
static void ByteLength(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    frogjs::Encoding encoding;
    if (!frogjs::GetEncodingArg(isolate, args[1], frogjs::Encoding::kUtf8, &encoding)) return;
    if (encoding == frogjs::Encoding::kUtf8) {
        args.GetReturnValue().Set(static_cast<double>(frogjs::Utf8Length(isolate, args[0].As<String>())));
        return;
    }
    args.GetReturnValue().Set(static_cast<double>(frogjs::DecodeString(isolate, args[0].As<String>(), encoding).size()));
}

static void IsEncoding(const FunctionCallbackInfo<Value>& args) {
    String::Utf8Value name(args.GetIsolate(), args[0]);
    frogjs::Encoding encoding;
    args.GetReturnValue().Set(*name != nullptr && frogjs::ParseEncoding(*name, &encoding));
}

// toByteString(value): UTF-8 encode a string, or copy a Buffer's raw bytes, into a
// byte string (one char per byte, codes 0-255)
void ToByteString(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() > 0 && args[0]->IsArrayBufferView()) {
        BufferData view = GetBufferData(args[0].As<Object>());
        Local<String> result;
        if (frogjs::EncodeBytes(isolate, view.data, view.length, frogjs::Encoding::kLatin1).ToLocal(&result)) {
            args.GetReturnValue().Set(result);
        }
        return;
    }

    std::string bytes;
    if (args.Length() > 0 && args[0]->IsString()) {
        bytes = frogjs::DecodeString(isolate, args[0].As<String>(), frogjs::Encoding::kUtf8);
    } else if (args.Length() < 1 || !frogjs::GetBinaryData(args[0], &bytes)) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8Literal(isolate,
            "The \"chunk\" argument must be of type string or Buffer")));
        return;
    }
    Local<String> result;
    if (frogjs::EncodeBytes(isolate, bytes.data(), bytes.size(), frogjs::Encoding::kLatin1).ToLocal(&result)) {
        args.GetReturnValue().Set(result);
    }
}

// byteStringToUtf8(bytes): decode a byte string as UTF-8 (invalid sequences become U+FFFD)
void ByteStringToUtf8(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> str;
    if (args.Length() < 1 || !args[0]->ToString(context).ToLocal(&str) || str->Length() == 0) {
        args.GetReturnValue().Set(String::Empty(isolate));
        return;
    }

    std::vector<uint8_t> bytes(str->Length());
    frogjs::WriteOneByte(isolate, str, bytes.data());
    Local<String> result;
    if (frogjs::EncodeBytes(isolate, reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                            frogjs::Encoding::kUtf8).ToLocal(&result)) {
        args.GetReturnValue().Set(result);
    }
}

// byteStringToBuffer(bytes): a Buffer holding a byte string's bytes
void ByteStringToBuffer(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> str;
    if (args.Length() < 1 || !args[0]->ToString(context).ToLocal(&str)) return;
    std::vector<uint8_t> bytes(str->Length());
    if (!bytes.empty()) frogjs::WriteOneByte(isolate, str, bytes.data());
    args.GetReturnValue().Set(NewBuffer(isolate, context, reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// Set up the global Buffer class
void SetupBuffer(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "decode", Decode);
    frogjs::SetFunction(isolate, context, native, "utf8Write", Utf8Write);
    frogjs::SetFunction(isolate, context, native, "decodeInto", DecodeIntoBuffer);
    frogjs::SetFunction(isolate, context, native, "utf8Length", Utf8LengthOf);
    frogjs::SetFunction(isolate, context, native, "encode", Encode);
    frogjs::SetFunction(isolate, context, native, "write", Write);
    frogjs::SetFunction(isolate, context, native, "byteLength", ByteLength);
    frogjs::SetFunction(isolate, context, native, "isEncoding", IsEncoding);

    native->Set(context, String::NewFromUtf8Literal(isolate, "maxLength"),
        Number::New(isolate, static_cast<double>(frogjs::MaxBufferLength()))).Check();

    Local<Value> argv[1] = {native};
    Local<Value> buffer;
    if (!frogjs::RunJsModule(isolate, context, "buffer", kBufferSource, 1, argv).ToLocal(&buffer)) return;
    g_buffer_ctor.Reset(isolate, buffer.As<Function>());
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "Buffer"), buffer).Check();
}

void ResetBufferState() {
    g_buffer_ctor.Reset();
}
