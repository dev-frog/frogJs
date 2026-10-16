#include "frogjs/js_module.h"
#include <cstdio>
#include <string>

using namespace v8;

namespace frogjs {

MaybeLocal<Value> RunJsModule(Isolate* isolate, Local<Context> context, const char* name,
                              const char* source, int argc, Local<Value> argv[]) {
    EscapableHandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);

    std::string origin_name = std::string("frogjs:") + name;
    ScriptOrigin origin(String::NewFromUtf8(isolate, origin_name.c_str()).ToLocalChecked());
    Local<String> code = String::NewFromUtf8(isolate, source).ToLocalChecked();

    Local<Script> script;
    Local<Value> factory;
    Local<Value> result;
    if (!Script::Compile(context, code, &origin).ToLocal(&script) ||
        !script->Run(context).ToLocal(&factory) || !factory->IsFunction() ||
        !factory.As<Function>()->Call(context, Undefined(isolate), argc, argv).ToLocal(&result)) {
        if (try_catch.HasCaught()) {
            String::Utf8Value error(isolate, try_catch.Exception());
            fprintf(stderr, "Failed to initialize %s module: %s\n", name, *error);
        } else {
            fprintf(stderr, "Failed to initialize %s module\n", name);
        }
        return MaybeLocal<Value>();
    }
    return handle_scope.Escape(result);
}

void SetFunction(Isolate* isolate, Local<Context> context, Local<Object> target,
                 const char* name, FunctionCallback callback) {
    target->Set(context, String::NewFromUtf8(isolate, name).ToLocalChecked(),
        FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocalChecked()).Check();
}

}  // namespace frogjs
