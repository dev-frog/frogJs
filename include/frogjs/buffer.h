#pragma once

#include <cstddef>
#include <v8.h>

// Raw storage behind a JS Buffer object (internal field 0).
struct BufferData {
    char* data;           // Raw byte array
    size_t length;        // Buffer length
    bool owns_data;       // Free on destructor if true
};

// Get internal BufferData from a Buffer object
BufferData* GetBufferData(v8::Local<v8::Object> buffer_obj);

// True if value was created by the Buffer constructor (safe to pass to GetBufferData)
bool IsBuffer(v8::Local<v8::Value> value);

// Byte strings hold one char per byte (codes 0-255). These back the JS modules
// (http, stream) that work on raw bytes:
// toByteString(string | Buffer): UTF-8 encode a string, or copy a Buffer's bytes
void ToByteString(const v8::FunctionCallbackInfo<v8::Value>& args);
// byteStringToUtf8(bytes): decode as UTF-8 (invalid sequences become U+FFFD)
void ByteStringToUtf8(const v8::FunctionCallbackInfo<v8::Value>& args);
// byteStringToBuffer(bytes): a new Buffer holding the bytes
void ByteStringToBuffer(const v8::FunctionCallbackInfo<v8::Value>& args);
