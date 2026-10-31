#pragma once

#include <string>
#include <v8.h>

namespace frogjs {

// Byte encodings understood by Buffer and crypto (names as in Node.js)
enum class Encoding { kUtf8, kHex, kBase64, kBase64Url, kLatin1, kAscii };

// Parse an encoding name ("utf8", "UTF-8", "hex", "binary", ...). Returns false
// for unknown names.
bool ParseEncoding(const std::string& name, Encoding* encoding);

// Parse an optional encoding argument: undefined/null give `fallback`; an
// unknown name throws TypeError [ERR_UNKNOWN_ENCODING] and returns false.
bool GetEncodingArg(v8::Isolate* isolate, v8::Local<v8::Value> value, Encoding fallback, Encoding* encoding);

// String -> bytes. hex stops at the first invalid pair and base64 skips
// characters outside the alphabet, like Node.js.
std::string DecodeString(v8::Isolate* isolate, v8::Local<v8::String> str, Encoding encoding);

// Bytes -> string
v8::Local<v8::String> EncodeBytes(v8::Isolate* isolate, const char* data, size_t length, Encoding encoding);

// The bytes of a Buffer, TypedArray, DataView or ArrayBuffer (copied into *out).
// Returns false for any other value.
bool GetBinaryData(v8::Local<v8::Value> value, std::string* out);

}  // namespace frogjs
