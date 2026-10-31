#pragma once

#include <string>
#include <v8.h>

namespace frogjs {

// util.inspect(value, { colors }) as a UTF-8 string. Falls back to String(value)
// if the util module is unavailable or inspect throws.
std::string Inspect(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> value, bool colors = false);

// util.format(...args) into *out, as used by console.log. Returns false if
// formatting threw (e.g. a throwing getter); the exception is left pending.
bool Format(v8::Isolate* isolate, v8::Local<v8::Context> context,
            const v8::FunctionCallbackInfo<v8::Value>& args, std::string* out);

}  // namespace frogjs
