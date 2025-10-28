#include <v8.h>
#include <uv.h>
#include <map>

using namespace v8;

// Global timer ID counter
static int g_next_timer_id = 1;

// Map to keep track of active timers
static std::map<int, uv_timer_t*> g_active_timers;

struct TimerData {
    Persistent<Function> callback;
    Isolate* isolate;
    int timer_id;
    bool repeat;
};

// Timer callback that executes JavaScript function
void TimerCallback(uv_timer_t* handle) {
    TimerData* data = static_cast<TimerData*>(handle->data);
    Isolate* isolate = data->isolate;

    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Context::Scope context_scope(context);

    // Get the JavaScript callback
    Local<Function> callback = Local<Function>::New(isolate, data->callback);

    // Call the JavaScript function
    TryCatch try_catch(isolate);
    callback->Call(context, context->Global(), 0, nullptr);

    if (try_catch.HasCaught()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        fprintf(stderr, "Timer callback error: %s\n", *error);
    }

    // If it's not a repeating timer, clean up
    if (!data->repeat) {
        data->callback.Reset();
        g_active_timers.erase(data->timer_id);
        uv_close((uv_handle_t*)handle, [](uv_handle_t* handle) {
            TimerData* data = static_cast<TimerData*>(handle->data);
            delete data;
            delete (uv_timer_t*)handle;
        });
    }
}

// setTimeout(callback, delay)
void SetTimeout(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[0]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "setTimeout requires a function and delay").ToLocalChecked());
        return;
    }

    Local<Function> callback = Local<Function>::Cast(args[0]);
    int delay = args[1]->Int32Value(isolate->GetCurrentContext()).ToChecked();

    // Create timer data
    TimerData* data = new TimerData;
    data->isolate = isolate;
    data->callback.Reset(isolate, callback);
    data->repeat = false;
    data->timer_id = g_next_timer_id++;

    // Create and start timer
    uv_timer_t* timer = new uv_timer_t;
    timer->data = data;
    uv_timer_init(uv_default_loop(), timer);
    uv_timer_start(timer, TimerCallback, delay, 0);

    // Store in active timers map
    g_active_timers[data->timer_id] = timer;

    // Return timer ID
    args.GetReturnValue().Set(Integer::New(isolate, data->timer_id));
}

// setInterval(callback, delay)
void SetInterval(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 2 || !args[0]->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "setInterval requires a function and delay").ToLocalChecked());
        return;
    }

    Local<Function> callback = Local<Function>::Cast(args[0]);
    int delay = args[1]->Int32Value(isolate->GetCurrentContext()).ToChecked();

    // Create timer data
    TimerData* data = new TimerData;
    data->isolate = isolate;
    data->callback.Reset(isolate, callback);
    data->repeat = true;
    data->timer_id = g_next_timer_id++;

    // Create and start repeating timer
    uv_timer_t* timer = new uv_timer_t;
    timer->data = data;
    uv_timer_init(uv_default_loop(), timer);
    uv_timer_start(timer, TimerCallback, delay, delay);

    // Store in active timers map
    g_active_timers[data->timer_id] = timer;

    // Return timer ID
    args.GetReturnValue().Set(Integer::New(isolate, data->timer_id));
}

// clearTimeout(id) / clearInterval(id)
void ClearTimer(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1) {
        return;
    }

    int timer_id = args[0]->Int32Value(isolate->GetCurrentContext()).ToChecked();

    // Find and stop the timer
    auto it = g_active_timers.find(timer_id);
    if (it != g_active_timers.end()) {
        uv_timer_t* timer = it->second;
        TimerData* data = static_cast<TimerData*>(timer->data);

        uv_timer_stop(timer);
        data->callback.Reset();
        g_active_timers.erase(it);

        uv_close((uv_handle_t*)timer, [](uv_handle_t* handle) {
            TimerData* data = static_cast<TimerData*>(handle->data);
            delete data;
            delete (uv_timer_t*)handle;
        });
    }
}

// Set up timer bindings in global scope
void SetupTimers(Isolate* isolate, Local<Context> context) {
    Local<Object> global = context->Global();

    // setTimeout
    global->Set(
        context,
        String::NewFromUtf8(isolate, "setTimeout").ToLocalChecked(),
        FunctionTemplate::New(isolate, SetTimeout)->GetFunction(context).ToLocalChecked()
    ).Check();

    // setInterval
    global->Set(
        context,
        String::NewFromUtf8(isolate, "setInterval").ToLocalChecked(),
        FunctionTemplate::New(isolate, SetInterval)->GetFunction(context).ToLocalChecked()
    ).Check();

    // clearTimeout
    global->Set(
        context,
        String::NewFromUtf8(isolate, "clearTimeout").ToLocalChecked(),
        FunctionTemplate::New(isolate, ClearTimer)->GetFunction(context).ToLocalChecked()
    ).Check();

    // clearInterval
    global->Set(
        context,
        String::NewFromUtf8(isolate, "clearInterval").ToLocalChecked(),
        FunctionTemplate::New(isolate, ClearTimer)->GetFunction(context).ToLocalChecked()
    ).Check();
}
