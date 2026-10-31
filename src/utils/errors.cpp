#include "frogjs/errors.h"
#include "frogjs/util.h"
#include "frogjs/worker.h"
#include <cstdio>
#include <string>

using namespace v8;

namespace frogjs {

// Print "file:line", the offending source line and a caret under the error
static void PrintSourceExcerpt(Isolate* isolate, Local<Context> context, Local<Message> message) {
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

void ReportException(Isolate* isolate, TryCatch* try_catch, bool show_source) {
    // Termination (process.exit() or terminate() in a worker) is not an error
    if (try_catch->HasTerminated()) return;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Value> exception = try_catch->Exception();
    if (ForwardWorkerError(isolate, exception)) return;

    Local<Message> message = try_catch->Message();
    if (show_source && !message.IsEmpty()) {
        PrintSourceExcerpt(isolate, context, message);
    }

    // Errors print as their stack plus any extra properties (err.code, ...);
    // anything else thrown is shown the way util.inspect would show it
    fprintf(stderr, "%s\n", Inspect(isolate, context, exception).c_str());
    fflush(stderr);
}

}  // namespace frogjs
