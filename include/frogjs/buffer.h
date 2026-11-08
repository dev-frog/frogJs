#pragma once

#include <cstddef>
#include <v8.h>

// The bytes of a Buffer or other ArrayBufferView. They belong to V8: don't keep
// the pointer past the current call into native code.
struct BufferData {
    char* data;     // nullptr when the length is 0
    size_t length;
};

// Get the bytes of an ArrayBufferView (a Buffer or any typed array)
BufferData GetBufferData(v8::Local<v8::Object> view_object);

// True if value is an instance of Buffer (safe to pass to GetBufferData)
bool IsBuffer(v8::Local<v8::Value> value);

// A new Buffer holding a copy of data
v8::Local<v8::Object> NewBuffer(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                const char* data, size_t length);

// Forget the thread's Buffer constructor (before its isolate is disposed)
void ResetBufferState();

// Byte strings hold one char per byte (codes 0-255). These back the JS modules
// (http, stream) that work on raw bytes:
// toByteString(string | Buffer): UTF-8 encode a string, or copy a Buffer's bytes
void ToByteString(const v8::FunctionCallbackInfo<v8::Value>& args);
// byteStringToUtf8(bytes): decode as UTF-8 (invalid sequences become U+FFFD)
void ByteStringToUtf8(const v8::FunctionCallbackInfo<v8::Value>& args);
// byteStringToBuffer(bytes): a new Buffer holding the bytes
void ByteStringToBuffer(const v8::FunctionCallbackInfo<v8::Value>& args);
