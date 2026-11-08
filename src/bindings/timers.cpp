#include <v8.h>
#include <uv.h>
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/loop.h"
#include <map>

using namespace v8;

// The timer API is JavaScript (Timeout and Immediate objects, argument checks)
// over one libuv timer per timeout and one check/idle handle pair for the
// immediates of a thread.
static const char* kTimersSource = R"JS(
(function (native) {
'use strict';

const MAX_DELAY = 2147483647;

function checkCallback(callback) {
  if (typeof callback !== 'function') {
    const shown = callback === null || callback === undefined ? String(callback)
      : typeof callback === 'object' ? 'an instance of ' + (callback.constructor && callback.constructor.name)
      : `type ${typeof callback} (${String(callback)})`;
    const err = new TypeError(`The "callback" argument must be of type function. Received ${shown}`);
    err.code = 'ERR_INVALID_ARG_TYPE';
    throw err;
  }
}

// Delays outside 1..2^31-1 become 1, as in Node.js
function normalizeDelay(delay) {
  const ms = Number(delay);
  return ms >= 1 && ms <= MAX_DELAY ? Math.trunc(ms) : 1;
}

// Timeouts whose number was asked for, so clearTimeout(number) can find them
const numbered = new Map();

class Timeout {
  constructor(callback, delay, repeat, args) {
    this._callback = callback;
    this._args = args;
    this._delay = normalizeDelay(delay);
    this._repeat = repeat;
    this._ref = true;
    this._start();
  }

  _start() {
    this._id = native.start(this, this._delay, this._repeat);
    if (!this._ref) native.setRef(this._id, false);
  }

  _fire() {
    if (!this._repeat) {
      numbered.delete(this._id);
      this._id = 0;
    }
    this._callback.apply(this, this._args);
  }

  ref() {
    this._ref = true;
    if (this._id) native.setRef(this._id, true);
    return this;
  }

  unref() {
    this._ref = false;
    if (this._id) native.setRef(this._id, false);
    return this;
  }

  hasRef() {
    return this._ref;
  }

  // Start the delay over (also restarts a timeout that has fired)
  refresh() {
    if (this._id) native.again(this._id, this._delay);
    else if (this._callback) this._start();
    return this;
  }

  close() {
    clearTimer(this);
    return this;
  }

  [Symbol.toPrimitive]() {
    if (this._id) numbered.set(this._id, this);
    return this._id;
  }
}

function clearTimer(timer) {
  if (timer === null || timer === undefined) return;
  const timeout = timer instanceof Timeout ? timer : numbered.get(Number(timer));
  if (!timeout) return;
  timeout._callback = null;
  if (timeout._id) {
    numbered.delete(timeout._id);
    native.clear(timeout._id);
    timeout._id = 0;
  }
}

function setTimeout(callback, delay, ...args) {
  checkCallback(callback);
  return new Timeout(callback, delay, false, args);
}

function setInterval(callback, delay, ...args) {
  checkCallback(callback);
  return new Timeout(callback, delay, true, args);
}

// Immediates run after the I/O of the current loop iteration. One added while
// they run waits for the next iteration.
let immediates = [];
let refedImmediates = 0;

class Immediate {
  constructor(callback, args) {
    this._callback = callback;
    this._args = args;
    this._ref = true;
    refedImmediates++;
    immediates.push(this);
    native.scheduleImmediates(refedImmediates > 0);
  }

  ref() {
    if (!this._ref && this._callback) {
      refedImmediates++;
      native.scheduleImmediates(true);
    }
    this._ref = true;
    return this;
  }

  unref() {
    if (this._ref && this._callback) {
      refedImmediates--;
      native.scheduleImmediates(refedImmediates > 0);
    }
    this._ref = false;
    return this;
  }

  hasRef() {
    return this._ref;
  }
}

function runImmediates() {
  const queue = immediates;
  immediates = [];
  // A throwing callback must not lose the rest: they go back on the queue
  for (let i = 0; i < queue.length; i++) {
    const immediate = queue[i];
    if (!immediate._callback) continue;
    const callback = immediate._callback;
    immediate._callback = null;
    if (immediate._ref) refedImmediates--;
    try {
      callback.apply(immediate, immediate._args);
    } catch (err) {
      immediates = queue.slice(i + 1).concat(immediates);
      if (immediates.length > 0) native.scheduleImmediates(refedImmediates > 0);
      throw err;
    }
  }
  if (immediates.length > 0) native.scheduleImmediates(refedImmediates > 0);
  else native.stopImmediates();
}

function setImmediate(callback, ...args) {
  checkCallback(callback);
  return new Immediate(callback, args);
}

function clearImmediate(immediate) {
  if (!(immediate instanceof Immediate) || !immediate._callback) return;
  immediate._callback = null;
  if (immediate._ref) refedImmediates--;
  const index = immediates.indexOf(immediate);
  if (index >= 0) immediates.splice(index, 1);
  if (immediates.length === 0) native.stopImmediates();
  else native.scheduleImmediates(refedImmediates > 0);
}

native.setRunners((timeout) => timeout._fire(), runImmediates);

return { setTimeout, setInterval, clearTimeout: clearTimer, clearInterval: clearTimer, setImmediate, clearImmediate };
})
)JS";

namespace {

struct TimerData {
    uv_timer_t handle;
    Global<Value> timeout;  // the Timeout object
    Isolate* isolate;
    int id;
    bool repeat;
};

// Per thread: the active timers, and the immediates' handles
thread_local int tls_next_timer_id = 1;
thread_local std::map<int, TimerData*> tls_timers;
thread_local uv_check_t* tls_immediate_check = nullptr;
thread_local uv_idle_t* tls_immediate_idle = nullptr;
thread_local Global<Function> tls_immediate_runner;
thread_local Global<Function> tls_timer_runner;
thread_local bool tls_cleanup_registered = false;

void CloseTimer(TimerData* data) {
    tls_timers.erase(data->id);
    uv_timer_stop(&data->handle);
    data->timeout.Reset();
    uv_close(reinterpret_cast<uv_handle_t*>(&data->handle), [](uv_handle_t* handle) {
        delete static_cast<TimerData*>(handle->data);
    });
}

void CloseImmediateHandles() {
    tls_immediate_runner.Reset();
    tls_timer_runner.Reset();
    if (tls_immediate_check != nullptr) {
        uv_close(reinterpret_cast<uv_handle_t*>(tls_immediate_check), [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_check_t*>(handle);
        });
        uv_close(reinterpret_cast<uv_handle_t*>(tls_immediate_idle), [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_idle_t*>(handle);
        });
        tls_immediate_check = nullptr;
        tls_immediate_idle = nullptr;
    }
}

void CleanupTimers() {
    while (!tls_timers.empty()) CloseTimer(tls_timers.begin()->second);
    CloseImmediateHandles();
    tls_cleanup_registered = false;
}

void OnTimer(uv_timer_t* handle) {
    TimerData* data = static_cast<TimerData*>(handle->data);
    Isolate* isolate = data->isolate;
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Value> argv[1] = {data->timeout.Get(isolate)};

    // A one-shot timer is finished before its callback runs, so clearing it
    // from inside the callback finds nothing
    if (!data->repeat) CloseTimer(data);
    (void)frogjs::MakeCallback(isolate, context, tls_timer_runner.Get(isolate), Undefined(isolate), 1, argv);  // reports its own errors
}

TimerData* FindTimer(const FunctionCallbackInfo<Value>& args) {
    int id = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
    auto it = tls_timers.find(id);
    return it != tls_timers.end() ? it->second : nullptr;
}

// start(timeout, delay, repeat) -> id
void Start(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    uint64_t delay = static_cast<uint64_t>(args[1]->NumberValue(context).FromMaybe(1));
    bool repeat = args[2]->IsTrue();

    TimerData* data = new TimerData;
    data->isolate = isolate;
    data->timeout.Reset(isolate, args[0]);
    data->id = tls_next_timer_id++;
    data->repeat = repeat;
    data->handle.data = data;
    uv_timer_init(frogjs::Loop(), &data->handle);
    uv_timer_start(&data->handle, OnTimer, delay, repeat ? delay : 0);
    tls_timers[data->id] = data;
    args.GetReturnValue().Set(data->id);
}

void Clear(const FunctionCallbackInfo<Value>& args) {
    if (TimerData* data = FindTimer(args)) CloseTimer(data);
}

void Again(const FunctionCallbackInfo<Value>& args) {
    if (TimerData* data = FindTimer(args)) {
        uint64_t delay = static_cast<uint64_t>(args[1]->NumberValue(args.GetIsolate()->GetCurrentContext()).FromMaybe(1));
        uv_timer_start(&data->handle, OnTimer, delay, data->repeat ? delay : 0);
    }
}

void SetRef(const FunctionCallbackInfo<Value>& args) {
    if (TimerData* data = FindTimer(args)) {
        uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&data->handle);
        if (args[1]->IsTrue()) uv_ref(handle);
        else uv_unref(handle);
    }
}

void OnImmediateCheck(uv_check_t* handle) {
    Isolate* isolate = static_cast<Isolate*>(handle->data);
    HandleScope handle_scope(isolate);
    if (tls_immediate_runner.IsEmpty()) return;
    Local<Context> context = isolate->GetCurrentContext();
    (void)frogjs::MakeCallback(isolate, context, tls_immediate_runner.Get(isolate), Undefined(isolate), 0, nullptr);
}

// scheduleImmediates(keepAlive): run the queue after the next poll phase. The
// idle handle stops the poll from blocking and keeps the loop alive.
void ScheduleImmediates(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    if (tls_immediate_check == nullptr) {
        tls_immediate_check = new uv_check_t;
        tls_immediate_idle = new uv_idle_t;
        uv_check_init(frogjs::Loop(), tls_immediate_check);
        uv_idle_init(frogjs::Loop(), tls_immediate_idle);
        tls_immediate_check->data = isolate;
        uv_unref(reinterpret_cast<uv_handle_t*>(tls_immediate_check));
    }
    uv_check_start(tls_immediate_check, OnImmediateCheck);
    if (args[0]->IsTrue()) {
        uv_idle_start(tls_immediate_idle, [](uv_idle_t*) {});
    } else {
        uv_idle_stop(tls_immediate_idle);
    }
}

void StopImmediates(const FunctionCallbackInfo<Value>&) {
    if (tls_immediate_check != nullptr) {
        uv_check_stop(tls_immediate_check);
        uv_idle_stop(tls_immediate_idle);
    }
}

void SetRunners(const FunctionCallbackInfo<Value>& args) {
    tls_timer_runner.Reset(args.GetIsolate(), args[0].As<Function>());
    tls_immediate_runner.Reset(args.GetIsolate(), args[1].As<Function>());
}

}  // namespace

// Set up the timer functions in the global scope
void SetupTimers(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "start", Start);
    frogjs::SetFunction(isolate, context, native, "clear", Clear);
    frogjs::SetFunction(isolate, context, native, "again", Again);
    frogjs::SetFunction(isolate, context, native, "setRef", SetRef);
    frogjs::SetFunction(isolate, context, native, "scheduleImmediates", ScheduleImmediates);
    frogjs::SetFunction(isolate, context, native, "stopImmediates", StopImmediates);
    frogjs::SetFunction(isolate, context, native, "setRunners", SetRunners);

    Local<Value> argv[1] = {native};
    Local<Value> timers;
    if (!frogjs::RunJsModule(isolate, context, "timers", kTimersSource, 1, argv).ToLocal(&timers)) return;

    Local<Object> global = context->Global();
    Local<Array> names = timers.As<Object>()->GetOwnPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < names->Length(); i++) {
        Local<Value> name = names->Get(context, i).ToLocalChecked();
        global->Set(context, name, timers.As<Object>()->Get(context, name).ToLocalChecked()).Check();
    }

    if (!tls_cleanup_registered) {
        tls_cleanup_registered = true;
        frogjs::AddThreadCleanup(CleanupTimers);
    }
}
