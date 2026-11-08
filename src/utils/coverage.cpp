#ifdef FROGJS_COVERAGE

#include "frogjs/coverage.h"
#include "frogjs/loop.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>
#include <v8-inspector.h>

using namespace v8;

namespace frogjs {

namespace {

// Keeps the responses of the inspector session; only the last one matters
class Channel : public v8_inspector::V8Inspector::Channel {
public:
    std::string last;

    void sendResponse(int, std::unique_ptr<v8_inspector::StringBuffer> message) override {
        v8_inspector::StringView view = message->string();
        last.clear();
        last.reserve(view.length());
        for (size_t i = 0; i < view.length(); i++) {
            // Sources aren't part of the response, so the text is ASCII in practice;
            // anything else can only be inside a URL and is replaced
            int c = view.is8Bit() ? view.characters8()[i] : view.characters16()[i];
            last += c < 128 ? static_cast<char>(c) : '?';
        }
    }
    void sendNotification(std::unique_ptr<v8_inspector::StringBuffer>) override {}
    void flushProtocolNotifications() override {}
};

struct Session {
    v8_inspector::V8InspectorClient client;
    Channel channel;
    std::unique_ptr<v8_inspector::V8Inspector> inspector;
    std::unique_ptr<v8_inspector::V8InspectorSession> session;
};

Session* g_session = nullptr;
bool g_written = false;

void Send(const std::string& message) {
    v8_inspector::StringView view(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    g_session->session->dispatchProtocolMessage(view);
}

}  // namespace

void EnableCoverage(Isolate* isolate, Local<Context> context) {
    if (IsWorkerThread() || g_session != nullptr || getenv("FROGJS_COVERAGE_DIR") == nullptr) return;

    g_session = new Session;
    g_session->inspector = v8_inspector::V8Inspector::create(isolate, &g_session->client);
    const uint8_t name[] = "frogjs";
    g_session->inspector->contextCreated(v8_inspector::V8ContextInfo(context, 1, v8_inspector::StringView(name, 6)));
    g_session->session = g_session->inspector->connect(1, &g_session->channel, v8_inspector::StringView(),
        v8_inspector::V8Inspector::kFullyTrusted);
    Send(R"({"id":1,"method":"Profiler.enable"})");
    Send(R"({"id":2,"method":"Profiler.startPreciseCoverage","params":{"callCount":true,"detailed":true}})");
}

void WriteCoverage(Isolate* isolate) {
    if (g_session == nullptr || g_written || IsWorkerThread()) return;
    g_written = true;
    HandleScope handle_scope(isolate);
    Send(R"({"id":3,"method":"Profiler.takePreciseCoverage"})");

    std::string path = std::string(getenv("FROGJS_COVERAGE_DIR")) + "/js-" + std::to_string(getpid()) + ".json";
    FILE* file = fopen(path.c_str(), "w");
    if (file != nullptr) {
        fwrite(g_session->channel.last.data(), 1, g_session->channel.last.size(), file);
        fclose(file);
    }
}

}  // namespace frogjs

#endif  // FROGJS_COVERAGE
