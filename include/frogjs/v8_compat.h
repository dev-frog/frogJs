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

}  // namespace frogjs
