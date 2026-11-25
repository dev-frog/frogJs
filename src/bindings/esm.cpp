#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <v8.h>
#include "frogjs/errors.h"
#include "frogjs/esm.h"
#include "frogjs/js_module.h"
#include "frogjs/runtime.h"
#include "frogjs/v8_compat.h"

using namespace v8;

// ES modules. The loader (modules.cpp) resolves a specifier to a record
// { format, url, filename, name, source }; this file turns records into V8
// modules: source text modules for 'module', synthetic modules for
// 'commonjs', 'json' and 'builtin'. A graph is loaded completely (every
// import resolved and compiled) before it is instantiated, so V8's resolve
// callback only looks up what is already there.

namespace {

struct ModuleRecord {
    Global<Module> module;
    Global<Object> descriptor;  // what the loader returned, for synthetic modules
    std::string url;
    // The module each import resolved to, by specifier and type attribute
    std::unordered_map<std::string, ModuleRecord*> requests;
    std::vector<std::string> export_names;
};

// Modules by URL (one instance each), and by V8's identity hash to find the
// record of a Module V8 hands to a callback
thread_local std::unordered_map<std::string, std::unique_ptr<ModuleRecord>> tls_records;
thread_local std::unordered_multimap<int, ModuleRecord*> tls_by_hash;
thread_local Global<Object> tls_loader;
thread_local Global<Promise> tls_main_promise;
thread_local Global<Module> tls_main_module;

Local<String> Str(Isolate* isolate, const char* text) {
    return String::NewFromUtf8(isolate, text).ToLocalChecked();
}

Local<String> Str(Isolate* isolate, const std::string& text) {
    return String::NewFromUtf8(isolate, text.data(), NewStringType::kNormal, static_cast<int>(text.size())).ToLocalChecked();
}

std::string ToStdString(Isolate* isolate, Local<Value> value) {
    String::Utf8Value utf8(isolate, value);
    return *utf8 == nullptr ? std::string() : std::string(*utf8, utf8.length());
}

std::string GetString(Isolate* isolate, Local<Context> context, Local<Object> object, const char* key) {
    Local<Value> value;
    if (!object->Get(context, Str(isolate, key)).ToLocal(&value) || !value->IsString()) return std::string();
    return ToStdString(isolate, value);
}

ModuleRecord* FindRecord(Isolate* isolate, Local<Module> module) {
    auto range = tls_by_hash.equal_range(module->GetIdentityHash());
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->module.Get(isolate) == module) return it->second;
    }
    return nullptr;
}

void ThrowError(Isolate* isolate, const char* code, const std::string& message, bool type_error = false) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<String> text = Str(isolate, message);
    Local<Object> error = (type_error ? Exception::TypeError(text) : Exception::Error(text)).As<Object>();
    error->Set(context, Str(isolate, "code"), Str(isolate, code)).Check();
    isolate->ThrowException(error);
}

MaybeLocal<Value> CallLoader(Isolate* isolate, Local<Context> context, const char* name, int argc, Local<Value> argv[]) {
    if (tls_loader.IsEmpty()) {
        ThrowError(isolate, "ERR_INTERNAL_ASSERTION", "The module loader isn't set up");
        return MaybeLocal<Value>();
    }
    Local<Value> function;
    if (!tls_loader.Get(isolate)->Get(context, Str(isolate, name)).ToLocal(&function) || !function->IsFunction()) {
        return MaybeLocal<Value>();
    }
    return function.As<Function>()->Call(context, Undefined(isolate), argc, argv);
}

// Import attributes as an object. Static imports list (key, value, offset)
// triples; import() lists (key, value) pairs.
Local<Object> AttributesObject(Isolate* isolate, Local<Context> context, Local<FixedArray> attributes, int entry_size) {
    Local<Object> object = Object::New(isolate);
    for (int i = 0; i + 1 < attributes->Length(); i += entry_size) {
        Local<Value> key = frogjs::FixedArrayGet(context, attributes, i).As<Value>();
        Local<Value> value = frogjs::FixedArrayGet(context, attributes, i + 1).As<Value>();
        object->Set(context, key, value).Check();
    }
    return object;
}

std::string RequestKey(Isolate* isolate, Local<Context> context, Local<String> specifier, Local<FixedArray> attributes,
                       int entry_size) {
    std::string key = ToStdString(isolate, specifier);
    for (int i = 0; i + 1 < attributes->Length(); i += entry_size) {
        key += '\0';
        key += ToStdString(isolate, frogjs::FixedArrayGet(context, attributes, i).As<Value>());
        key += '=';
        key += ToStdString(isolate, frogjs::FixedArrayGet(context, attributes, i + 1).As<Value>());
    }
    return key;
}

void Forget(Isolate* isolate, const std::vector<std::string>& urls) {
    for (const std::string& url : urls) {
        auto it = tls_records.find(url);
        if (it == tls_records.end()) continue;
        Local<Module> module = it->second->module.Get(isolate);
        auto range = tls_by_hash.equal_range(module->GetIdentityHash());
        for (auto hash_it = range.first; hash_it != range.second; ++hash_it) {
            if (hash_it->second == it->second.get()) {
                tls_by_hash.erase(hash_it);
                break;
            }
        }
        tls_records.erase(it);
    }
}

MaybeLocal<Value> EvaluateSyntheticValue(Local<Context> context, Local<Module> module) {
    Isolate* isolate = Isolate::GetCurrent();
    ModuleRecord* record = FindRecord(isolate, module);
    if (record == nullptr) {
        ThrowError(isolate, "ERR_INTERNAL_ASSERTION", "Unknown synthetic module");
        return MaybeLocal<Value>();
    }
    Local<Value> argv[1] = {record->descriptor.Get(isolate)};
    Local<Value> exports;
    if (!CallLoader(isolate, context, "syntheticExports", 1, argv).ToLocal(&exports)) return MaybeLocal<Value>();
    if (module->SetSyntheticModuleExport(isolate, Str(isolate, "default"), exports).IsNothing()) return MaybeLocal<Value>();
    for (const std::string& name : record->export_names) {
        Local<Value> value = Undefined(isolate);
        if (name == "module.exports") {
            value = exports;
        } else if (exports->IsObject()) {
            // A getter that throws leaves that export undefined, as in Node.js
            TryCatch try_catch(isolate);
            if (!exports.As<Object>()->Get(context, Str(isolate, name)).ToLocal(&value)) value = Undefined(isolate);
        }
        if (module->SetSyntheticModuleExport(isolate, Str(isolate, name), value).IsNothing()) return MaybeLocal<Value>();
    }
    return Undefined(isolate);
}

MaybeLocal<Promise> EvaluateSyntheticPromise(Local<Context> context, Local<Module> module) {
    Local<Value> result;
    if (!EvaluateSyntheticValue(context, module).ToLocal(&result)) return MaybeLocal<Promise>();
    Local<Promise::Resolver> resolver;
    if (!Promise::Resolver::New(context).ToLocal(&resolver)) return MaybeLocal<Promise>();
    resolver->Resolve(context, Undefined(Isolate::GetCurrent())).Check();
    return resolver->GetPromise();
}

ModuleRecord* Register(Isolate* isolate, const std::string& url, Local<Module> module, Local<Object> descriptor,
                       std::vector<std::string>* created) {
    auto record = std::make_unique<ModuleRecord>();
    record->module.Reset(isolate, module);
    record->descriptor.Reset(isolate, descriptor);
    record->url = url;
    ModuleRecord* raw = record.get();
    tls_records[url] = std::move(record);
    tls_by_hash.emplace(module->GetIdentityHash(), raw);
    created->push_back(url);
    return raw;
}

// The record for a descriptor, with its whole graph loaded. Null (with an
// exception pending) if a module can't be read, compiled or resolved.
ModuleRecord* Load(Isolate* isolate, Local<Context> context, Local<Object> descriptor, std::vector<std::string>* created) {
    std::string url = GetString(isolate, context, descriptor, "url");
    auto existing = tls_records.find(url);
    if (existing != tls_records.end()) return existing->second.get();

    std::string format = GetString(isolate, context, descriptor, "format");
    if (format != "module") {
        // CommonJS, JSON and built-ins: 'default' plus the names known now
        std::vector<std::string> names;
        if (format != "json") {
            Local<Value> argv[1] = {descriptor};
            Local<Value> list;
            if (!CallLoader(isolate, context, "exportNames", 1, argv).ToLocal(&list)) return nullptr;
            if (list->IsArray()) {
                Local<Array> array = list.As<Array>();
                for (uint32_t i = 0; i < array->Length(); i++) {
                    std::string name = ToStdString(isolate, array->Get(context, i).ToLocalChecked());
                    if (name != "default") names.push_back(name);
                }
            }
        }
        std::vector<Local<String>> export_names = {Str(isolate, "default")};
        for (const std::string& name : names) export_names.push_back(Str(isolate, name));
        Local<Module> module = frogjs::CreateSyntheticModule(isolate, Str(isolate, url), export_names,
                                                             EvaluateSyntheticPromise, EvaluateSyntheticValue);
        ModuleRecord* record = Register(isolate, url, module, descriptor, created);
        record->export_names = std::move(names);
        return record;
    }

    // Source text: from the descriptor (data: URLs, --eval) or the file
    Local<String> source;
    Local<Value> inline_source;
    if (descriptor->Get(context, Str(isolate, "source")).ToLocal(&inline_source) && inline_source->IsString()) {
        source = inline_source.As<String>();
    } else {
        std::string filename = GetString(isolate, context, descriptor, "filename");
        std::string text;
        std::string error;
        if (!frogjs::ReadScriptFile(filename, &text, &error)) {
            ThrowError(isolate, "ERR_MODULE_NOT_FOUND", error);
            return nullptr;
        }
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
        if (!String::NewFromUtf8(isolate, text.data(), NewStringType::kNormal, static_cast<int>(text.size())).ToLocal(&source)) {
            ThrowError(isolate, "ERR_FS_FILE_TOO_LARGE", "'" + filename + "' is too large to load", true);
            return nullptr;
        }
    }

    ScriptOrigin origin(Str(isolate, url), 0, 0, false, -1, Local<Value>(), false, false, true);
    ScriptCompiler::Source compiler_source(source, origin);
    Local<Module> module;
    if (!ScriptCompiler::CompileModule(isolate, &compiler_source).ToLocal(&module)) return nullptr;
    ModuleRecord* record = Register(isolate, url, module, descriptor, created);

    Local<FixedArray> requests = module->GetModuleRequests();
    Local<Value> referrer = Str(isolate, url);
    for (int i = 0; i < requests->Length(); i++) {
        Local<ModuleRequest> request = frogjs::FixedArrayGet(context, requests, i).As<ModuleRequest>();
        Local<String> specifier = request->GetSpecifier();
        Local<FixedArray> attributes = request->GetImportAttributes();
        Local<Value> argv[3] = {specifier, referrer, AttributesObject(isolate, context, attributes, 3)};
        Local<Value> resolved;
        if (!CallLoader(isolate, context, "esmResolve", 3, argv).ToLocal(&resolved) || !resolved->IsObject()) return nullptr;
        ModuleRecord* dependency = Load(isolate, context, resolved.As<Object>(), created);
        if (dependency == nullptr) return nullptr;
        record->requests[RequestKey(isolate, context, specifier, attributes, 3)] = dependency;
    }
    return record;
}

MaybeLocal<Module> ResolveCallback(Local<Context> context, Local<String> specifier, Local<FixedArray> attributes,
                                   Local<Module> referrer) {
    Isolate* isolate = Isolate::GetCurrent();
    ModuleRecord* record = FindRecord(isolate, referrer);
    if (record != nullptr) {
        auto it = record->requests.find(RequestKey(isolate, context, specifier, attributes, 3));
        if (it != record->requests.end()) return it->second->module.Get(isolate);
    }
    ThrowError(isolate, "ERR_MODULE_NOT_FOUND", "Cannot find module '" + ToStdString(isolate, specifier) + "'");
    return MaybeLocal<Module>();
}

// "does not provide an export named 'x'" for a CommonJS module: as in
// Node.js, say why (its names are guessed from the source) and what works
void ExplainMissingCommonJSExport(Isolate* isolate, Local<Context> context, Local<Value> exception) {
    if (!exception->IsNativeError()) return;
    Local<Object> error = exception.As<Object>();
    Local<Value> message_value;
    if (!error->Get(context, Str(isolate, "message")).ToLocal(&message_value)) return;
    std::string message = ToStdString(isolate, message_value);
    const std::string prefix = "The requested module '";
    const std::string middle = "' does not provide an export named '";
    size_t middle_at = message.find(middle);
    if (message.compare(0, prefix.size(), prefix) != 0 || middle_at == std::string::npos || message.back() != '\'') return;
    std::string specifier = message.substr(prefix.size(), middle_at - prefix.size());
    std::string name = message.substr(middle_at + middle.size(), message.size() - middle_at - middle.size() - 1);
    bool commonjs = false;
    for (const auto& entry : tls_records) {
        for (const auto& request : entry.second->requests) {
            if (request.first.substr(0, request.first.find('\0')) != specifier) continue;
            Local<Object> descriptor = request.second->descriptor.Get(isolate);
            if (GetString(isolate, context, descriptor, "format") == "commonjs") commonjs = true;
        }
    }
    if (!commonjs) return;
    std::string explained = "Named export '" + name + "' not found. The requested module '" + specifier +
        "' is a CommonJS module, which may not support all module.exports as named exports.\n"
        "CommonJS modules can always be imported via the default export, for example using:\n\n"
        "import pkg from '" + specifier + "';\nconst { " + name + " } = pkg;\n";
    Local<Value> stack;
    if (error->Get(context, Str(isolate, "stack")).ToLocal(&stack) && stack->IsString()) {
        std::string text = ToStdString(isolate, stack);
        size_t at = text.find(message);
        if (at != std::string::npos) text.replace(at, message.size(), explained);
        (void)error->Set(context, Str(isolate, "stack"), Str(isolate, text));
    }
    (void)error->Set(context, Str(isolate, "message"), Str(isolate, explained));
}

// Load and instantiate a graph. On failure nothing loaded for it is kept, so
// importing it again tries again.
MaybeLocal<Module> Instantiate(Isolate* isolate, Local<Context> context, Local<Value> descriptor) {
    if (!descriptor->IsObject()) {
        ThrowError(isolate, "ERR_INVALID_ARG_TYPE", "A module descriptor must be an object", true);
        return MaybeLocal<Module>();
    }
    std::vector<std::string> created;
    ModuleRecord* record = Load(isolate, context, descriptor.As<Object>(), &created);
    if (record == nullptr) {
        Forget(isolate, created);
        return MaybeLocal<Module>();
    }
    Local<Module> module = record->module.Get(isolate);
    if (module->GetStatus() == Module::kUninstantiated) {
        TryCatch try_catch(isolate);
        if (!module->InstantiateModule(context, ResolveCallback).FromMaybe(false)) {
            bool rethrow = try_catch.HasCaught() && !try_catch.HasTerminated();
            if (rethrow) ExplainMissingCommonJSExport(isolate, context, try_catch.Exception());
            Forget(isolate, created);
            if (rethrow) try_catch.ReThrow();
            return MaybeLocal<Module>();
        }
    }
    return module;
}

// importModule(descriptor): [evaluation promise, namespace]
void ImportModule(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    Local<Module> module;
    if (!Instantiate(isolate, context, args[0]).ToLocal(&module)) return;
    Local<Promise> promise;
    if (!module->Evaluate(context).ToLocal(&promise)) return;
    Local<Value> items[2] = {promise, module->GetModuleNamespace()};
    args.GetReturnValue().Set(Array::New(isolate, items, 2));
}

// requireModule(descriptor): the namespace, for require() of an ES module.
// A graph with top-level await can't be required: it would need to wait.
void RequireModule(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    Local<Module> module;
    if (!Instantiate(isolate, context, args[0]).ToLocal(&module)) return;
    std::string filename = GetString(isolate, context, args[0].As<Object>(), "filename");

    Module::Status status = module->GetStatus();
    if (status == Module::kEvaluated) {
        args.GetReturnValue().Set(module->GetModuleNamespace());
        return;
    }
    if (status == Module::kErrored) {
        isolate->ThrowException(module->GetException());
        return;
    }
    if (status == Module::kEvaluating) {
        ThrowError(isolate, "ERR_REQUIRE_CYCLE_MODULE",
                   "Cannot require() ES Module " + filename + " in a cycle. A cycle involving require(esm) is not allowed "
                   "to maintain invariants mandated by the ECMAScript specification. Try making at least part of the "
                   "dependency in the graph lazily loaded.");
        return;
    }
    if (module->IsGraphAsync()) {
        ThrowError(isolate, "ERR_REQUIRE_ASYNC_MODULE",
                   "require() cannot be used on an ESM graph with top-level await. Use import() instead.\n"
                   "  Requiring " + filename);
        return;
    }
    Local<Promise> promise;
    if (!module->Evaluate(context).ToLocal(&promise)) return;
    if (promise->State() == Promise::kRejected) {
        promise->MarkAsHandled();
        isolate->ThrowException(promise->Result());
        return;
    }
    args.GetReturnValue().Set(module->GetModuleNamespace());
}

void OnMainRejected(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Value> reason = args[0];
    frogjs::HandleUncaughtWithMessage(isolate, reason, Exception::CreateMessage(isolate, reason));
}

// runMainModule(descriptor): evaluate the main module. An error it throws,
// now or after a top-level await, is an uncaught exception.
void RunMainModuleCallback(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    Local<Module> module;
    if (!Instantiate(isolate, context, args[0]).ToLocal(&module)) return;
    Local<Promise> promise;
    if (!module->Evaluate(context).ToLocal(&promise)) return;
    tls_main_module.Reset(isolate, module);
    tls_main_promise.Reset(isolate, promise);
    if (promise->State() == Promise::kRejected) {
        promise->MarkAsHandled();
        Local<Value> reason = promise->Result();
        frogjs::HandleUncaughtWithMessage(isolate, reason, Exception::CreateMessage(isolate, reason));
    } else if (promise->State() == Promise::kPending) {
        Local<Function> on_rejected = Function::New(context, OnMainRejected).ToLocalChecked();
        (void)promise->Catch(context, on_rejected);
    }
}

void ContainsModuleSyntaxCallback(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    if (!args[0]->IsString()) return;
    args.GetReturnValue().Set(frogjs::ContainsModuleSyntax(isolate, isolate->GetCurrentContext(), args[0].As<String>(), args[1]));
}

MaybeLocal<Promise> ImportDynamically(Local<Context> context, Local<Data> host_defined_options, Local<Value> resource_name,
                                      Local<String> specifier, Local<FixedArray> import_attributes) {
    (void)host_defined_options;
    Isolate* isolate = Isolate::GetCurrent();
    EscapableHandleScope handle_scope(isolate);
    Local<Value> argv[3] = {specifier, resource_name, AttributesObject(isolate, context, import_attributes, 2)};
    Local<Value> result;
    if (!CallLoader(isolate, context, "importDynamic", 3, argv).ToLocal(&result) || !result->IsPromise()) {
        return MaybeLocal<Promise>();
    }
    return handle_scope.Escape(result.As<Promise>());
}

void InitializeImportMeta(Local<Context> context, Local<Module> module, Local<Object> meta) {
    Isolate* isolate = Isolate::GetCurrent();
    ModuleRecord* record = FindRecord(isolate, module);
    if (record == nullptr) return;
    Local<Value> argv[2] = {meta, Str(isolate, record->url)};
    (void)CallLoader(isolate, context, "initMeta", 2, argv);
}

}  // namespace

namespace frogjs {

bool ContainsModuleSyntax(Isolate* isolate, Local<Context> context, Local<String> source, Local<Value> filename) {
    // Errors that mean "this is an ES module", and errors that CommonJS's
    // wrapper causes but an ES module wouldn't have (as in Node.js)
    static const char* const kModuleOnly[] = {
        "Cannot use import statement outside a module",
        "Unexpected token 'export'",
        "Cannot use 'import.meta' outside a module",
    };
    static const char* const kCommonJSOnly[] = {
        "Identifier 'module' has already been declared",
        "Identifier 'exports' has already been declared",
        "Identifier 'require' has already been declared",
        "Identifier '__filename' has already been declared",
        "Identifier '__dirname' has already been declared",
        "await is only valid in async functions and the top level bodies of modules",
    };
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    Local<String> header = String::NewFromUtf8Literal(isolate, "(function (exports, require, module, __filename, __dirname) {\n");
    Local<String> wrapped = String::Concat(isolate, String::Concat(isolate, header, source), String::NewFromUtf8Literal(isolate, "\n})"));
    ScriptOrigin origin(filename, -1);
    if (!Script::Compile(context, wrapped, &origin).IsEmpty()) return false;
    if (!try_catch.HasCaught() || try_catch.HasTerminated()) return false;
    std::string message = ToStdString(isolate, try_catch.Message()->Get());
    for (const char* text : kModuleOnly) {
        if (message.find(text) != std::string::npos) return true;
    }
    bool retry = false;
    for (const char* text : kCommonJSOnly) {
        if (message.find(text) != std::string::npos) retry = true;
    }
    if (!retry) return false;
    try_catch.Reset();
    ScriptOrigin module_origin(filename, 0, 0, false, -1, Local<Value>(), false, false, true);
    ScriptCompiler::Source module_source(source, module_origin);
    return !ScriptCompiler::CompileModule(isolate, &module_source).IsEmpty();
}

void AddModuleNatives(Isolate* isolate, Local<Context> context, Local<Object> native) {
    SetFunction(isolate, context, native, "containsModuleSyntax", ContainsModuleSyntaxCallback);
    SetFunction(isolate, context, native, "importModule", ImportModule);
    SetFunction(isolate, context, native, "requireModule", RequireModule);
    SetFunction(isolate, context, native, "runMainModule", RunMainModuleCallback);
}

void SetModuleLoader(Isolate* isolate, Local<Object> loader) {
    tls_loader.Reset(isolate, loader);
    isolate->SetHostImportModuleDynamicallyCallback(ImportDynamically);
    isolate->SetHostInitializeImportMetaObjectCallback(InitializeImportMeta);
}

bool ReportStalledMainModule(Isolate* isolate) {
    if (tls_main_promise.IsEmpty()) return false;
    HandleScope handle_scope(isolate);
    if (tls_main_promise.Get(isolate)->State() != Promise::kPending) return false;
    Local<Context> context = isolate->GetCurrentContext();
    Local<Message> message = FirstStalledAwait(isolate, tls_main_module.Get(isolate));
    if (message.IsEmpty()) {
        fprintf(stderr, "Warning: Detected unsettled top-level await\n");
        return true;
    }
    std::string where = ToStdString(isolate, message->GetScriptResourceName());
    int line = message->GetLineNumber(context).FromMaybe(0);
    fprintf(stderr, "Warning: Detected unsettled top-level await at %s:%d\n", where.c_str(), line);
    Local<String> source_line;
    if (message->GetSourceLine(context).ToLocal(&source_line)) {
        std::string text = ToStdString(isolate, source_line);
        int start = message->GetStartColumn(context).FromMaybe(0);
        fprintf(stderr, "%s\n%s^\n\n\n", text.c_str(), std::string(start > 0 ? start : 0, ' ').c_str());
    }
    fflush(stderr);
    return true;
}

void ResetEsmState() {
    tls_by_hash.clear();
    tls_records.clear();
    tls_loader.Reset();
    tls_main_promise.Reset();
    tls_main_module.Reset();
}

}  // namespace frogjs
