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
