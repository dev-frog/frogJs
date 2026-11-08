#include <v8.h>
#include <uv.h>
#include "frogjs/loop.h"
#include <cstring>
#include <memory>
#include <string>
#include "frogjs/buffer.h"
#include "frogjs/encoding.h"
#include "frogjs/errors.h"
#include "frogjs/hash.h"
#include "frogjs/js_module.h"
#include "frogjs/v8_compat.h"

#include <unordered_set>

using namespace v8;

// The crypto module's API is written in JavaScript on top of the native
// helpers below; the algorithms live in src/utils/hash.cpp. Argument checks
// and error messages follow Node.js. Hash and Hmac are Transform streams.
static const char* kCryptoSource = R"JS(
(function (native, stream) {
'use strict';

const kHandle = Symbol('kHandle');
const kAlgorithm = Symbol('kAlgorithm');
const kFinalized = Symbol('kFinalized');
const kMaxInt32 = 2147483647;

function makeError(Ctor, code, message) {
  const err = new Ctor(message);
  err.code = code;
  return err;
}

function describe(value) {
  if (value === null || value === undefined) return ` Received ${value}`;
  if (typeof value === 'function') return ` Received function ${value.name}`;
  if (typeof value === 'object') {
    const name = value.constructor && value.constructor.name;
    return name ? ` Received an instance of ${name}` : ` Received ${String(value)}`;
  }
  let shown = typeof value === 'string' ? `'${value.length > 28 ? value.slice(0, 25) + '...' : value}'` : String(value);
  if (typeof value === 'bigint') shown += 'n';
  return ` Received type ${typeof value} (${shown})`;
}

function invalidArgType(name, expected, value) {
  return makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}.${describe(value)}`);
}

function outOfRange(name, range, value) {
  return makeError(RangeError, 'ERR_OUT_OF_RANGE',
    `The value of "${name}" is out of range. It must be ${range}. Received ${String(value)}`);
}

function isBinary(value) {
  return Buffer.isBuffer(value) || ArrayBuffer.isView(value) || value instanceof ArrayBuffer;
}

function validateInt32(value, name, min) {
  if (typeof value !== 'number') throw invalidArgType(name, 'of type number', value);
  if (!Number.isInteger(value)) throw outOfRange(name, 'an integer', value);
  if (value < min || value > kMaxInt32) throw outOfRange(name, `>= ${min} && <= ${kMaxInt32}`, value);
}

function validateFunction(value, name) {
  if (typeof value !== 'function') throw invalidArgType(name, 'of type function', value);
}

function finalizedError() {
  return makeError(Error, 'ERR_CRYPTO_HASH_FINALIZED', 'Digest already called');
}

// Encode a digest Buffer; like Node.js, unknown encodings return the Buffer
function encodeDigest(buffer, encoding) {
  if (encoding === undefined || encoding === 'buffer') return buffer;
  try {
    return buffer.toString(encoding);
  } catch (err) {
    if (err.code === 'ERR_UNKNOWN_ENCODING') return buffer;
    throw err;
  }
}

function update(self, data, encoding) {
  if (self[kFinalized]) throw finalizedError();
  if (typeof data !== 'string' && !(Buffer.isBuffer(data) || ArrayBuffer.isView(data))) {
    throw invalidArgType('data', 'of type string or an instance of Buffer, TypedArray, or DataView', data);
  }
  native.digestUpdate(self[kHandle], data, typeof data === 'string' ? encodingOrUtf8(encoding) : undefined);
  return self;
}

function encodingOrUtf8(encoding) {
  if (typeof encoding !== 'string') return 'utf8';
  try {
    Buffer.from('', encoding);
    return encoding;
  } catch {
    return 'utf8';
  }
}

class Hash extends stream.Transform {
  constructor(algorithm, options) {
    super(options);
    if (typeof algorithm !== 'string') throw invalidArgType('algorithm', 'of type string', algorithm);
    const name = native.normalizeHash(algorithm);
    if (name === '') throw new Error('Digest method not supported');
    this[kAlgorithm] = name;
    this[kHandle] = native.createDigest(name);
    this[kFinalized] = false;
  }

  update(data, encoding) {
    return update(this, data, encoding);
  }

  digest(encoding) {
    if (this[kFinalized]) throw finalizedError();
    this[kFinalized] = true;
    return encodeDigest(native.digestFinal(this[kHandle]), encoding);
  }

  copy(options) {
    if (this[kFinalized]) throw finalizedError();
    const copy = new Hash(this[kAlgorithm], options);
    copy[kHandle] = native.digestCopy(this[kHandle]);
    return copy;
  }

  _transform(chunk, encoding, callback) {
    this.update(chunk, encoding);
    callback();
  }

  _flush(callback) {
    this.push(this.digest());
    callback();
  }
}

class Hmac extends stream.Transform {
  constructor(algorithm, key, options) {
    super(options);
    if (typeof algorithm !== 'string') throw invalidArgType('hmac', 'of type string', algorithm);
    if (typeof key !== 'string' && !isBinary(key)) {
      throw invalidArgType('key',
        'of type string or an instance of ArrayBuffer, Buffer, TypedArray, DataView, KeyObject, or CryptoKey', key);
    }
    const name = native.normalizeHash(algorithm);
    if (name === '') throw makeError(TypeError, 'ERR_CRYPTO_INVALID_DIGEST', `Invalid digest: ${algorithm}`);
    const encoding = options && typeof options.encoding === 'string' ? options.encoding : 'utf8';
    this[kHandle] = native.createDigest(name, typeof key === 'string' ? Buffer.from(key, encoding) : key);
    this[kFinalized] = false;
  }

  update(data, encoding) {
    return update(this, data, encoding);
  }

  // Calling digest() again returns an empty result, as in Node.js
  digest(encoding) {
    const result = this[kFinalized] ? Buffer.alloc(0) : native.digestFinal(this[kHandle]);
    this[kFinalized] = true;
    return encodeDigest(result, encoding);
  }

  _transform(chunk, encoding, callback) {
    this.update(chunk, encoding);
    callback();
  }

  _flush(callback) {
    this.push(this.digest());
    callback();
  }
}

function createHash(algorithm, options) {
  return new Hash(algorithm, options);
}

function createHmac(algorithm, key, options) {
  return new Hmac(algorithm, key, options);
}

function getHashes() {
  return native.hashNames();
}

function checkPbkdf2(password, salt, iterations, keylen, digest) {
  if (typeof digest !== 'string') throw invalidArgType('digest', 'of type string', digest);
  const expected = 'of type string or an instance of ArrayBuffer, Buffer, TypedArray, or DataView';
  if (typeof password !== 'string' && !isBinary(password)) throw invalidArgType('password', expected, password);
  if (typeof salt !== 'string' && !isBinary(salt)) throw invalidArgType('salt', expected, salt);
  validateInt32(iterations, 'iterations', 1);
  validateInt32(keylen, 'keylen', 0);
  const name = native.normalizeHash(digest);
  if (name === '') throw makeError(TypeError, 'ERR_CRYPTO_INVALID_DIGEST', `Invalid digest: ${digest}`);
  return name;
}

// pbkdf2(password, salt, iterations, keylen, digest, callback): runs on the
// libuv thread pool and calls back with (err, derivedKey)
function pbkdf2(password, salt, iterations, keylen, digest, callback) {
  if (typeof digest === 'function') {
    callback = digest;
    digest = undefined;
  }
  const name = checkPbkdf2(password, salt, iterations, keylen, digest);
  validateFunction(callback, 'callback');
  native.pbkdf2(password, salt, iterations, keylen, name, callback);
}

function pbkdf2Sync(password, salt, iterations, keylen, digest) {
  const name = checkPbkdf2(password, salt, iterations, keylen, digest);
  return native.pbkdf2(password, salt, iterations, keylen, name);
}

function validateSize(size) {
  if (typeof size !== 'number') throw invalidArgType('size', 'of type number', size);
  if (!(size >= 0 && size <= kMaxInt32)) throw outOfRange('size', `>= 0 && <= ${kMaxInt32}`, size);
  return Math.trunc(size);
}

// randomBytes(size[, callback]): bytes from the OS CSPRNG
function randomBytes(size, callback) {
  size = validateSize(size);
  if (callback === undefined) return native.randomBytes(size);
  validateFunction(callback, 'callback');
  native.randomBytes(size, callback);
}

function domException(name, code, message) {
  const err = new Error(message);
  err.name = name;
  err.code = code;
  return err;
}

// getRandomValues(typedArray): fill an integer TypedArray in place (Web Crypto)
function getRandomValues(data) {
  const integerTypes = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array,
    Int32Array, Uint32Array, BigInt64Array, BigUint64Array];
  if (!integerTypes.some((Type) => data instanceof Type)) {
    throw domException('TypeMismatchError', 17, 'The data argument must be an integer-type TypedArray');
  }
  if (data.byteLength > 65536) {
    throw domException('QuotaExceededError', 22, 'The requested length exceeds 65,536 bytes');
  }
  native.randomFill(data);
  return data;
}

// randomUUID(): a random RFC 4122 version 4 UUID
function randomUUID() {
  const h = native.randomBytes(16).toString('hex');
  const version = '4';                                            // high nibble of byte 6
  const variant = ((parseInt(h[16], 16) & 0x3) | 0x8).toString(16); // top bits of byte 8: 10
  return `${h.slice(0, 8)}-${h.slice(8, 12)}-${version}${h.slice(13, 16)}-${variant}${h.slice(17, 20)}-${h.slice(20)}`;
}

// randomInt([min, ]max[, callback]): a uniformly random integer in [min, max)
function randomInt(min, max, callback) {
  if (typeof max === 'undefined' || typeof max === 'function') {
    callback = max;
    max = min;
    min = 0;
  }
  if (!Number.isSafeInteger(min)) throw invalidArgType('min', 'a safe integer', min);
  if (!Number.isSafeInteger(max)) throw invalidArgType('max', 'a safe integer', max);
  if (max <= min) throw outOfRange('max', `greater than the value of "min" (${min})`, max);
  const range = max - min;
  if (range > 2 ** 48) throw outOfRange('max - min', `<= ${2 ** 48}`, range);
  if (callback !== undefined) validateFunction(callback, 'callback');

  // Rejection sampling over 48-bit values avoids modulo bias
  const limit = 2 ** 48 - (2 ** 48 % range);
  let value;
  do {
    const buf = native.randomBytes(6);
    value = parseInt(buf.toString('hex'), 16);
  } while (value >= limit);
  const result = min + (value % range);
  if (callback === undefined) return result;
  setTimeout(() => callback(null, result), 0);
}

// timingSafeEqual(a, b): compare without leaking where the inputs differ
function timingSafeEqual(buf1, buf2) {
  const expected = 'an instance of ArrayBuffer, Buffer, TypedArray, or DataView';
  if (!isBinary(buf1)) throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "buf1" argument must be ${expected}.`);
  if (!isBinary(buf2)) throw makeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "buf2" argument must be ${expected}.`);
  if (Buffer.byteLength(buf1) !== Buffer.byteLength(buf2)) {
    throw makeError(RangeError, 'ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH', 'Input buffers must have the same byte length');
  }
  return native.timingSafeEqual(buf1, buf2);
}

return {
  createHash,
  createHmac,
  Hash,
  Hmac,
  getHashes,
  pbkdf2,
  pbkdf2Sync,
  randomBytes,
  getRandomValues,
  randomUUID,
  randomInt,
  timingSafeEqual,
};
})
)JS";

namespace {

// A Digest owned by a JS handle object; freed when the handle is collected
struct DigestHandle {
    std::unique_ptr<frogjs::Digest> digest;
    Global<Object> object;
};

thread_local Persistent<ObjectTemplate> g_handle_template;

// Digests still alive when the thread ends: their weak callbacks never run
thread_local std::unordered_set<DigestHandle*> tls_digests;
thread_local bool tls_cleanup_registered = false;

void OnHandleCollected(const WeakCallbackInfo<DigestHandle>& info) {
    DigestHandle* handle = info.GetParameter();
    handle->object.Reset();
    tls_digests.erase(handle);
    delete handle;
}

void CleanupDigests() {
    for (DigestHandle* handle : tls_digests) {
        handle->object.Reset();
        delete handle;
    }
    tls_digests.clear();
    tls_cleanup_registered = false;
}

Local<Object> WrapDigest(Isolate* isolate, Local<Context> context, std::unique_ptr<frogjs::Digest> digest) {
    Local<Object> object = Local<ObjectTemplate>::New(isolate, g_handle_template)->NewInstance(context).ToLocalChecked();
    DigestHandle* handle = new DigestHandle{std::move(digest), Global<Object>(isolate, object)};
    object->SetInternalField(0, frogjs::NewExternal(isolate, handle));
    handle->object.SetWeak(handle, OnHandleCollected, WeakCallbackType::kParameter);
    tls_digests.insert(handle);
    return object;
}

frogjs::Digest* UnwrapDigest(Local<Value> value) {
    if (!value->IsObject() || value.As<Object>()->InternalFieldCount() < 1) return nullptr;
    DigestHandle* handle = frogjs::ExternalValue<DigestHandle>(value.As<Object>()->GetInternalField(0).As<Value>());
    return handle != nullptr ? handle->digest.get() : nullptr;
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value str(isolate, value);
    return *str != nullptr ? std::string(*str, str.length()) : std::string();
}

// A string (UTF-8) or binary argument as bytes
std::string BytesArg(Isolate* isolate, Local<Value> value) {
    std::string bytes;
    if (value->IsString()) return frogjs::DecodeString(isolate, value.As<String>(), frogjs::Encoding::kUtf8);
    frogjs::GetBinaryData(value, &bytes);
    return bytes;
}

// Call a JS callback from a libuv completion with (err, buffer)
void CallBack(Isolate* isolate, Global<Context>& saved_context, Global<Function>& saved_callback,
              int status, const std::string& bytes) {
    HandleScope handle_scope(isolate);
    Local<Context> context = saved_context.Get(isolate);
    Context::Scope context_scope(context);

    Local<Value> argv[2];
    if (status < 0) {
        argv[0] = Exception::Error(String::NewFromUtf8(isolate, uv_strerror(status)).ToLocalChecked());
        argv[1] = Undefined(isolate);
    } else {
        argv[0] = Null(isolate);
        argv[1] = NewBuffer(isolate, context, bytes.data(), bytes.size());
    }
    TryCatch try_catch(isolate);
    Local<Value> ignored;
    if (!saved_callback.Get(isolate)->Call(context, Undefined(isolate), 2, argv).ToLocal(&ignored) &&
        try_catch.HasCaught()) {
        frogjs::HandleUncaught(isolate, &try_catch);
    }
}

// normalizeHash(name): canonical supported name or ""
void NormalizeHash(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    std::string name = frogjs::NormalizeHashName(ToStdString(isolate, args[0]));
    args.GetReturnValue().Set(String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked());
}

void HashNames(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    const auto& names = frogjs::SupportedHashes();
    Local<Array> array = Array::New(isolate, static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); i++) {
        array->Set(context, static_cast<uint32_t>(i), String::NewFromUtf8(isolate, names[i].c_str()).ToLocalChecked()).Check();
    }
    args.GetReturnValue().Set(array);
}

// createDigest(name[, hmacKey]): a hash, or an HMAC when a key is given
void CreateDigest(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    std::string name = ToStdString(isolate, args[0]);
    std::unique_ptr<frogjs::Digest> digest = args[1]->IsUndefined()
        ? frogjs::NewHash(name)
        : frogjs::NewHmac(name, BytesArg(isolate, args[1]));
    if (!digest) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8Literal(isolate, "Digest method not supported")));
        return;
    }
    args.GetReturnValue().Set(WrapDigest(isolate, context, std::move(digest)));
}

// digestUpdate(handle, data, encoding): encoding applies to string data
void DigestUpdate(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    frogjs::Digest* digest = UnwrapDigest(args[0]);
    if (digest == nullptr) return;

    if (args[1]->IsString()) {
        frogjs::Encoding encoding;
        if (!frogjs::GetEncodingArg(isolate, args[2], frogjs::Encoding::kUtf8, &encoding)) return;
        digest->Update(frogjs::DecodeString(isolate, args[1].As<String>(), encoding));
    } else if (args[1]->IsArrayBufferView()) {
        BufferData bytes = GetBufferData(args[1].As<Object>());
        if (bytes.length > 0) digest->Update(reinterpret_cast<const uint8_t*>(bytes.data), bytes.length);
    } else {
        std::string bytes;
        frogjs::GetBinaryData(args[1], &bytes);
        digest->Update(bytes);
    }
}

void DigestFinal(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    frogjs::Digest* digest = UnwrapDigest(args[0]);
    if (digest == nullptr) return;
    std::string result = digest->Final();
    args.GetReturnValue().Set(NewBuffer(isolate, isolate->GetCurrentContext(), result.data(), result.size()));
}

void DigestCopy(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    frogjs::Digest* digest = UnwrapDigest(args[0]);
    if (digest == nullptr) return;
    args.GetReturnValue().Set(WrapDigest(isolate, isolate->GetCurrentContext(), digest->Clone()));
}

struct Pbkdf2Work {
    uv_work_t req;
    Isolate* isolate;
    Global<Context> context;
    Global<Function> callback;
    std::string name, password, salt, result;
    uint32_t iterations;
    size_t key_length;
};

// pbkdf2(password, salt, iterations, keylen, name[, callback]): arguments were
// validated in JS. With a callback the work runs on the thread pool.
void Pbkdf2(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    auto work = std::make_unique<Pbkdf2Work>();
    work->isolate = isolate;
    work->password = BytesArg(isolate, args[0]);
    work->salt = BytesArg(isolate, args[1]);
    work->iterations = args[2]->Uint32Value(context).FromMaybe(1);
    work->key_length = args[3]->Uint32Value(context).FromMaybe(0);
    work->name = ToStdString(isolate, args[4]);

    if (!args[5]->IsFunction()) {
        std::string key = frogjs::Pbkdf2(work->name, work->password, work->salt, work->iterations, work->key_length);
        args.GetReturnValue().Set(NewBuffer(isolate, context, key.data(), key.size()));
        return;
    }

    work->context.Reset(isolate, context);
    work->callback.Reset(isolate, args[5].As<Function>());
    work->req.data = work.get();
    uv_queue_work(frogjs::Loop(), &work->req,
        [](uv_work_t* req) {
            Pbkdf2Work* w = static_cast<Pbkdf2Work*>(req->data);
            w->result = frogjs::Pbkdf2(w->name, w->password, w->salt, w->iterations, w->key_length);
        },
        [](uv_work_t* req, int status) {
            std::unique_ptr<Pbkdf2Work> w(static_cast<Pbkdf2Work*>(req->data));
            CallBack(w->isolate, w->context, w->callback, status, w->result);
        });
    work.release();
}

struct RandomWork {
    uv_random_t req;
    Isolate* isolate;
    Global<Context> context;
    Global<Function> callback;
    std::string bytes;
};

// randomBytes(size[, callback])
void RandomBytes(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    size_t size = args[0]->Uint32Value(context).FromMaybe(0);

    if (!args[1]->IsFunction()) {
        std::string bytes(size, '\0');
        int status = size > 0 ? uv_random(nullptr, nullptr, &bytes[0], size, 0, nullptr) : 0;
        if (status < 0) {
            isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, uv_strerror(status)).ToLocalChecked()));
            return;
        }
        args.GetReturnValue().Set(NewBuffer(isolate, context, bytes.data(), bytes.size()));
        return;
    }

    auto work = std::make_unique<RandomWork>();
    work->isolate = isolate;
    work->context.Reset(isolate, context);
    work->callback.Reset(isolate, args[1].As<Function>());
    work->bytes.assign(size, '\0');
    work->req.data = work.get();
    int status = uv_random(frogjs::Loop(), &work->req, &work->bytes[0], size, 0,
        [](uv_random_t* req, int status, void*, size_t) {
            std::unique_ptr<RandomWork> w(static_cast<RandomWork*>(req->data));
            CallBack(w->isolate, w->context, w->callback, status, w->bytes);
        });
    if (status < 0) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, uv_strerror(status)).ToLocalChecked()));
        return;
    }
    work.release();
}

// randomFill(typedArray): overwrite the view's bytes in place
void RandomFill(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    if (!args[0]->IsArrayBufferView()) return;
    Local<ArrayBufferView> view = args[0].As<ArrayBufferView>();
    size_t length = view->ByteLength();
    if (length == 0) return;
    std::shared_ptr<BackingStore> store = view->Buffer()->GetBackingStore();
    char* data = static_cast<char*>(store->Data()) + view->ByteOffset();
    int status = uv_random(nullptr, nullptr, data, length, 0, nullptr);
    if (status < 0) {
        isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, uv_strerror(status)).ToLocalChecked()));
    }
}

// timingSafeEqual(a, b): same-length inputs (checked in JS); the loop always
// visits every byte
void TimingSafeEqual(const FunctionCallbackInfo<Value>& args) {
    std::string a, b;
    frogjs::GetBinaryData(args[0], &a);
    frogjs::GetBinaryData(args[1], &b);
    if (a.size() != b.size()) {
        args.GetReturnValue().Set(false);
        return;
    }
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    args.GetReturnValue().Set(diff == 0);
}

}  // namespace

// Set up crypto module, exposed as global `crypto` and returned by require('crypto')
void SetupCrypto(Isolate* isolate, Local<Context> context) {
    Local<ObjectTemplate> handle_template = ObjectTemplate::New(isolate);
    handle_template->SetInternalFieldCount(1);
    g_handle_template.Reset(isolate, handle_template);
    if (!tls_cleanup_registered) {
        tls_cleanup_registered = true;
        frogjs::AddThreadCleanup(CleanupDigests);
    }

    Local<Value> stream = context->Global()->Get(context, String::NewFromUtf8Literal(isolate, "stream")).ToLocalChecked();
    if (!stream->IsObject()) {
        fprintf(stderr, "Failed to initialize crypto module: stream module is missing\n");
        return;
    }

    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "normalizeHash", NormalizeHash);
    frogjs::SetFunction(isolate, context, native, "hashNames", HashNames);
    frogjs::SetFunction(isolate, context, native, "createDigest", CreateDigest);
    frogjs::SetFunction(isolate, context, native, "digestUpdate", DigestUpdate);
    frogjs::SetFunction(isolate, context, native, "digestFinal", DigestFinal);
    frogjs::SetFunction(isolate, context, native, "digestCopy", DigestCopy);
    frogjs::SetFunction(isolate, context, native, "pbkdf2", Pbkdf2);
    frogjs::SetFunction(isolate, context, native, "randomBytes", RandomBytes);
    frogjs::SetFunction(isolate, context, native, "randomFill", RandomFill);
    frogjs::SetFunction(isolate, context, native, "timingSafeEqual", TimingSafeEqual);

    Local<Value> argv[2] = {native, stream};
    Local<Value> crypto;
    if (!frogjs::RunJsModule(isolate, context, "crypto", kCryptoSource, 2, argv).ToLocal(&crypto)) {
        return;
    }
    context->Global()->Set(context, String::NewFromUtf8Literal(isolate, "crypto"), crypto).Check();
}
