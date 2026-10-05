#include <v8.h>
#include <uv.h>
#include <string>
#include <limits.h>

using namespace v8;

// Defined in process.cpp
Local<String> GetPlatformString(Isolate* isolate);

static void ThrowUVError(Isolate* isolate, const char* syscall, int err) {
    std::string error = std::string(syscall) + " failed: " + uv_strerror(err);
    isolate->ThrowException(Exception::Error(
        String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked()));
}

static void ReturnString(const FunctionCallbackInfo<Value>& args, const char* value) {
    args.GetReturnValue().Set(
        String::NewFromUtf8(args.GetIsolate(), value).ToLocalChecked());
}

// Read a string from a libuv getter that takes (buffer, size*)
template <typename Getter>
static void ReturnUVString(const FunctionCallbackInfo<Value>& args, const char* syscall, Getter getter) {
    char buffer[PATH_MAX];
    size_t size = sizeof(buffer);
    int err = getter(buffer, &size);
    if (err != 0) {
        ThrowUVError(args.GetIsolate(), syscall, err);
        return;
    }
    ReturnString(args, buffer);
}

// os.platform()
void OSPlatform(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(GetPlatformString(args.GetIsolate()));
}

// os.arch()
void OSArch(const FunctionCallbackInfo<Value>& args) {
#if defined(__aarch64__) || defined(__arm64__)
    ReturnString(args, "arm64");
#elif defined(__x86_64__)
    ReturnString(args, "x64");
#elif defined(__i386__)
    ReturnString(args, "ia32");
#elif defined(__arm__)
    ReturnString(args, "arm");
#else
    uv_utsname_t name;
    int err = uv_os_uname(&name);
    if (err != 0) {
        ThrowUVError(args.GetIsolate(), "uv_os_uname", err);
        return;
    }
    ReturnString(args, name.machine);
#endif
}

// os.cpus()
void OSCpus(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    uv_cpu_info_t* cpu_infos;
    int count;
    int err = uv_cpu_info(&cpu_infos, &count);
    if (err != 0) {
        ThrowUVError(isolate, "uv_cpu_info", err);
        return;
    }

    auto key = [isolate](const char* name) {
        return String::NewFromUtf8(isolate, name).ToLocalChecked();
    };

    Local<Array> cpus = Array::New(isolate, count);
    for (int i = 0; i < count; i++) {
        const uv_cpu_info_t& info = cpu_infos[i];

        Local<Object> times = Object::New(isolate);
        times->Set(context, key("user"), Number::New(isolate, static_cast<double>(info.cpu_times.user))).Check();
        times->Set(context, key("nice"), Number::New(isolate, static_cast<double>(info.cpu_times.nice))).Check();
        times->Set(context, key("sys"), Number::New(isolate, static_cast<double>(info.cpu_times.sys))).Check();
        times->Set(context, key("idle"), Number::New(isolate, static_cast<double>(info.cpu_times.idle))).Check();
        times->Set(context, key("irq"), Number::New(isolate, static_cast<double>(info.cpu_times.irq))).Check();

        Local<Object> cpu = Object::New(isolate);
        cpu->Set(context, key("model"), key(info.model)).Check();
        cpu->Set(context, key("speed"), Integer::New(isolate, info.speed)).Check();
        cpu->Set(context, key("times"), times).Check();

        cpus->Set(context, i, cpu).Check();
    }

    uv_free_cpu_info(cpu_infos, count);
    args.GetReturnValue().Set(cpus);
}

// os.hostname()
void OSHostname(const FunctionCallbackInfo<Value>& args) {
    ReturnUVString(args, "uv_os_gethostname", uv_os_gethostname);
}

// os.release() and os.type()
static void ReturnUnameField(const FunctionCallbackInfo<Value>& args, bool release) {
    uv_utsname_t name;
    int err = uv_os_uname(&name);
    if (err != 0) {
        ThrowUVError(args.GetIsolate(), "uv_os_uname", err);
        return;
    }
    ReturnString(args, release ? name.release : name.sysname);
}

void OSRelease(const FunctionCallbackInfo<Value>& args) {
    ReturnUnameField(args, true);
}

void OSType(const FunctionCallbackInfo<Value>& args) {
    ReturnUnameField(args, false);
}

// os.totalmem()
void OSTotalmem(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(
        Number::New(args.GetIsolate(), static_cast<double>(uv_get_total_memory())));
}

// os.freemem()
void OSFreemem(const FunctionCallbackInfo<Value>& args) {
#if UV_VERSION_HEX >= 0x012d00  // uv_get_available_memory added in libuv 1.45.0
    uint64_t free_memory = uv_get_available_memory();
#else
    uint64_t free_memory = uv_get_free_memory();
#endif
    args.GetReturnValue().Set(
        Number::New(args.GetIsolate(), static_cast<double>(free_memory)));
}

// os.homedir()
void OSHomedir(const FunctionCallbackInfo<Value>& args) {
    ReturnUVString(args, "uv_os_homedir", uv_os_homedir);
}

// os.tmpdir()
void OSTmpdir(const FunctionCallbackInfo<Value>& args) {
    ReturnUVString(args, "uv_os_tmpdir", uv_os_tmpdir);
}

// Set up global os object
void SetupOS(Isolate* isolate, Local<Context> context) {
    Local<Object> os = Object::New(isolate);

    struct { const char* name; FunctionCallback callback; } functions[] = {
        {"platform", OSPlatform},
        {"arch", OSArch},
        {"cpus", OSCpus},
        {"hostname", OSHostname},
        {"release", OSRelease},
        {"type", OSType},
        {"totalmem", OSTotalmem},
        {"freemem", OSFreemem},
        {"homedir", OSHomedir},
        {"tmpdir", OSTmpdir},
    };

    for (const auto& fn : functions) {
        os->Set(
            context,
            String::NewFromUtf8(isolate, fn.name).ToLocalChecked(),
            FunctionTemplate::New(isolate, fn.callback)->GetFunction(context).ToLocalChecked()
        ).Check();
    }

    // os.EOL
    os->Set(
        context,
        String::NewFromUtf8(isolate, "EOL").ToLocalChecked(),
        String::NewFromUtf8(isolate, "\n").ToLocalChecked()
    ).Check();

    // Attach os to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "os").ToLocalChecked(),
        os
    ).Check();
}
