#include "frogjs/errors.h"
#include "frogjs/process.h"
#include "frogjs/util.h"
#include "frogjs/worker.h"
#include <uv.h>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace v8;

namespace frogjs {

namespace {

// Rejected promises without a handler so far
thread_local std::vector<std::pair<Global<Promise>, Global<Value>>> tls_rejections;
thread_local bool tls_repl_active = false;

// Exit code when an 'uncaughtException' listener itself throws (as in Node.js)
const int kExceptionInFatalHandler = 7;

// Print "file:line", the offending source line and a caret under the error
void PrintSourceExcerpt(Isolate* isolate, Local<Context> context, Local<Message> message) {
    String::Utf8Value filename(isolate, message->GetScriptResourceName());
    int linenum = message->GetLineNumber(context).FromMaybe(0);
    fprintf(stderr, "%s:%d\n", *filename ? *filename : "<unknown>", linenum);

    Local<String> source_line_str;
    if (!message->GetSourceLine(context).ToLocal(&source_line_str)) return;
    String::Utf8Value source_line(isolate, source_line_str);
    if (*source_line == nullptr) return;

    std::string line(*source_line);
    int start = message->GetStartColumn(context).FromMaybe(0);
    int end = message->GetEndColumn(context).FromMaybe(start + 1);
    fprintf(stderr, "%s\n", line.c_str());

    // Keep tabs so the caret lines up with the source above it
    std::string underline;
    for (int i = 0; i < start && i < (int)line.size(); i++) {
        underline += line[i] == '\t' ? '\t' : ' ';
    }
    underline.append(end > start ? end - start : 1, '^');
    fprintf(stderr, "%s\n\n", underline.c_str());
}

// Errors print as their stack plus any extra properties (err.code, ...);
// anything else thrown is shown the way util.inspect would show it
void PrintValue(Isolate* isolate, Local<Context> context, Local<Value> exception) {
    fprintf(stderr, "%s\n", Inspect(isolate, context, exception).c_str());
    fflush(stderr);
}

// The steps after printing are shared: try the listeners first, print if none
// took the error, then end the process unless this is the REPL
bool Dispatch(Isolate* isolate, Local<Value> exception, TryCatch* try_catch, bool show_source) {
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    if (ForwardWorkerError(isolate, exception)) return false;

    {
        TryCatch listener_catch(isolate);
        Local<Value> argv[2] = {exception, String::NewFromUtf8Literal(isolate, "uncaughtException")};
        bool handled = EmitProcessEvent(isolate, context, "uncaughtException", 2, argv);
        if (listener_catch.HasTerminated()) return false;
        if (listener_catch.HasCaught()) {
            PrintValue(isolate, context, listener_catch.Exception());
            if (!tls_repl_active) ExitProcess(isolate, kExceptionInFatalHandler);
            return tls_repl_active;
        }
        if (handled) return true;
    }

    if (try_catch != nullptr) {
        ReportException(isolate, try_catch, show_source);
    } else {
        PrintValue(isolate, context, exception);
    }
    if (tls_repl_active) return true;
    ExitProcess(isolate, 1);
    return false;
}

void OnPromiseReject(PromiseRejectMessage message) {
    Local<Promise> promise = message.GetPromise();
    Isolate* isolate = Isolate::GetCurrent();
    if (message.GetEvent() == kPromiseRejectWithNoHandler) {
        tls_rejections.emplace_back(Global<Promise>(isolate, promise), Global<Value>(isolate, message.GetValue()));
    } else if (message.GetEvent() == kPromiseHandlerAddedAfterReject) {
        for (auto it = tls_rejections.begin(); it != tls_rejections.end(); ++it) {
            if (it->first == promise) {
                tls_rejections.erase(it);
                break;
            }
        }
    }
}

// The error reported for a rejection whose reason isn't an Error (as in Node.js)
Local<Value> RejectionError(Isolate* isolate, Local<Context> context, Local<Value> reason) {
    if (reason->IsNativeError()) return reason;
    std::string text = "This error originated either by throwing inside of an async function without a catch block, "
                       "or by rejecting a promise which was not handled with .catch(). The promise rejected with the "
                       "reason \"" + Inspect(isolate, context, reason) + "\".";
    Local<Value> error = Exception::Error(String::NewFromUtf8(isolate, text.c_str()).ToLocalChecked());
    error.As<Object>()->Set(context, String::NewFromUtf8Literal(isolate, "code"),
        String::NewFromUtf8Literal(isolate, "ERR_UNHANDLED_REJECTION")).Check();
    return error;
}

}  // namespace

void ReportException(Isolate* isolate, TryCatch* try_catch, bool show_source) {
    // Termination (process.exit() or terminate() in a worker) is not an error
    if (try_catch->HasTerminated()) return;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Value> exception = try_catch->Exception();

    Local<Message> message = try_catch->Message();
    if (show_source && !message.IsEmpty()) {
        PrintSourceExcerpt(isolate, context, message);
    }
    PrintValue(isolate, context, exception);
}

bool HandleUncaught(Isolate* isolate, TryCatch* try_catch) {
    if (try_catch->HasTerminated()) return false;
    return Dispatch(isolate, try_catch->Exception(), try_catch, !tls_repl_active);
}

bool HandleUncaughtValue(Isolate* isolate, Local<Value> exception) {
    return Dispatch(isolate, exception, nullptr, false);
}

MaybeLocal<Value> MakeCallback(Isolate* isolate, Local<Context> context, Local<Function> callback,
                               Local<Value> receiver, int argc, Local<Value> argv[]) {
    EscapableHandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    Local<Value> result;
    if (!callback->Call(context, receiver, argc, argv).ToLocal(&result)) {
        if (try_catch.HasCaught()) HandleUncaught(isolate, &try_catch);
        return MaybeLocal<Value>();
    }
    return handle_scope.Escape(result);
}

Local<Value> UVException(Isolate* isolate, int status, const char* syscall, const char* path, const char* dest) {
    EscapableHandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    std::string message = std::string(uv_err_name(status)) + ": " + uv_strerror(status) + ", " + syscall;
    if (path != nullptr) message += std::string(" '") + path + "'";
    if (dest != nullptr) message += std::string(" -> '") + dest + "'";

    Local<Object> error = Exception::Error(String::NewFromUtf8(isolate, message.c_str()).ToLocalChecked()).As<Object>();
    auto set = [&](Local<String> key, Local<Value> value) { error->Set(context, key, value).Check(); };
    set(String::NewFromUtf8Literal(isolate, "errno"), Integer::New(isolate, status));
    set(String::NewFromUtf8Literal(isolate, "code"), String::NewFromUtf8(isolate, uv_err_name(status)).ToLocalChecked());
    set(String::NewFromUtf8Literal(isolate, "syscall"), String::NewFromUtf8(isolate, syscall).ToLocalChecked());
    if (path != nullptr) set(String::NewFromUtf8Literal(isolate, "path"), String::NewFromUtf8(isolate, path).ToLocalChecked());
    if (dest != nullptr) set(String::NewFromUtf8Literal(isolate, "dest"), String::NewFromUtf8(isolate, dest).ToLocalChecked());
    return handle_scope.Escape(error);
}

void SetReplActive(bool active) {
    tls_repl_active = active;
}

void TrackPromiseRejections(Isolate* isolate) {
    isolate->SetPromiseRejectCallback(OnPromiseReject);
}

void DrainTasks(Isolate* isolate) {
    isolate->PerformMicrotaskCheckpoint();
    while (!tls_rejections.empty()) {
        HandleScope handle_scope(isolate);
        Local<Context> context = isolate->GetCurrentContext();
        auto pending = std::move(tls_rejections);
        tls_rejections.clear();
        for (auto& entry : pending) {
            Local<Promise> promise = entry.first.Get(isolate);
            Local<Value> reason = entry.second.Get(isolate);
            bool handled = false;
            {
                TryCatch listener_catch(isolate);
                Local<Value> argv[2] = {reason, promise};
                handled = EmitProcessEvent(isolate, context, "unhandledRejection", 2, argv);
                if (listener_catch.HasTerminated()) return;
                if (listener_catch.HasCaught()) {
                    HandleUncaught(isolate, &listener_catch);
                    handled = true;
                }
            }
            if (!handled) HandleUncaughtValue(isolate, RejectionError(isolate, context, reason));
        }
        isolate->PerformMicrotaskCheckpoint();
    }
}

void ClearPromiseRejections() {
    tls_rejections.clear();
}

}  // namespace frogjs
