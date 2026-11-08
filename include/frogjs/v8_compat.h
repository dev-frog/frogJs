#pragma once

#include <cstdint>
#include <v8.h>

// Helpers that compile against both older V8 (13.x) and newer V8 (15.x+).
// Newer V8 requires an ExternalPointerTypeTag for External::New/Value; tag 0
// is kExternalPointerTypeTagDefault, which older headers don't define.
namespace frogjs {

template <typename E = v8::External>
inline v8::Local<v8::External> NewExternal(v8::Isolate* isolate, void* value) {
    if constexpr (requires { E::New(isolate, value, uint16_t{0}); }) {
        return E::New(isolate, value, uint16_t{0});
    } else {
        return E::New(isolate, value);
    }
}

template <typename T, typename E = v8::External>
inline T* ExternalValue(v8::Local<v8::Value> value) {
    v8::Local<E> external = value.As<E>();
    if constexpr (requires { external->Value(uint16_t{0}); }) {
        return static_cast<T*>(external->Value(uint16_t{0}));
    } else {
        return static_cast<T*>(external->Value());
    }
}

// Copy a string's Latin-1 code units (each char must be <= 0xFF) into buffer,
// which must hold str->Length() bytes. V8 15 renamed WriteOneByteV2 to
// WriteOneByte; the old WriteOneByte signature in 13.x is deprecated.
template <typename S = v8::String>
inline void WriteOneByte(v8::Isolate* isolate, v8::Local<S> str, uint8_t* buffer) {
    uint32_t length = static_cast<uint32_t>(str->Length());
    if constexpr (requires { str->WriteOneByte(isolate, uint32_t{0}, length, buffer, 0); }) {
        str->WriteOneByte(isolate, 0, length, buffer, 0);
    } else {
        str->WriteOneByteV2(isolate, 0, length, buffer, 0);
    }
}

// Copy a string's UTF-16 code units into buffer, which must hold str->Length()
// units. Same rename as WriteOneByte (WriteV2 in 13.x, Write in 15.x).
template <typename S = v8::String>
inline void WriteTwoByte(v8::Isolate* isolate, v8::Local<S> str, uint16_t* buffer) {
    uint32_t length = static_cast<uint32_t>(str->Length());
    if constexpr (requires { str->Write(isolate, uint32_t{0}, length, buffer, 0); }) {
        str->Write(isolate, 0, length, buffer, 0);
    } else {
        str->WriteV2(isolate, 0, length, buffer, 0);
    }
}

// Encode a string as UTF-8 into buffer, which must hold capacity bytes (no NUL
// is added). Lone surrogates become U+FFFD. WriteUtf8V2 in 13.x, WriteUtf8 in 15.x.
template <typename S = v8::String>
inline size_t WriteUtf8(v8::Isolate* isolate, v8::Local<S> str, char* buffer, size_t capacity) {
    if constexpr (requires { str->WriteUtf8V2(isolate, buffer, capacity, S::WriteFlags::kReplaceInvalidUtf8); }) {
        return str->WriteUtf8V2(isolate, buffer, capacity, S::WriteFlags::kReplaceInvalidUtf8);
    } else {
        return str->WriteUtf8(isolate, buffer, capacity, S::WriteFlags::kReplaceInvalidUtf8);
    }
}

// Length of a string in UTF-8 bytes (Utf8LengthV2 in 13.x, Utf8Length in 15.x)
template <typename S = v8::String>
inline size_t Utf8Length(v8::Isolate* isolate, v8::Local<S> str) {
    if constexpr (requires { str->Utf8LengthV2(isolate); }) {
        return str->Utf8LengthV2(isolate);
    } else {
        return str->Utf8Length(isolate);
    }
}

}  // namespace frogjs
