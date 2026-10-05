#include <v8.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <limits.h>

using namespace v8;

// POSIX path utilities. Semantics follow Node.js `path.posix`.

static std::string NormalizeString(const std::string& path, bool allowAboveRoot) {
    std::vector<std::string> segments;
    size_t start = 0;

    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string segment = path.substr(start, end - start);
        start = end + 1;

        if (segment.empty() || segment == ".") continue;

        if (segment == "..") {
            if (!segments.empty() && segments.back() != "..") {
                segments.pop_back();
            } else if (allowAboveRoot) {
                segments.push_back("..");
            }
            continue;
        }

        segments.push_back(segment);
    }

    std::string result;
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) result += '/';
        result += segments[i];
    }
    return result;
}

static std::string PathNormalize(const std::string& path) {
    if (path.empty()) return ".";

    bool isAbsolute = path[0] == '/';
    bool trailingSeparator = path.back() == '/';

    std::string result = NormalizeString(path, !isAbsolute);

    if (result.empty()) {
        if (isAbsolute) return "/";
        return trailingSeparator ? "./" : ".";
    }
    if (trailingSeparator) result += '/';

    return isAbsolute ? "/" + result : result;
}

static std::string PathJoin(const std::vector<std::string>& paths) {
    std::string joined;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        if (!joined.empty()) joined += '/';
        joined += p;
    }
    return joined.empty() ? "." : PathNormalize(joined);
}

static std::string GetCwd() {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer);
    }
    return "/";
}

static std::string PathResolve(const std::vector<std::string>& paths) {
    std::string resolved;
    bool resolvedAbsolute = false;

    for (int i = static_cast<int>(paths.size()) - 1; i >= -1 && !resolvedAbsolute; i--) {
        std::string p = i >= 0 ? paths[i] : GetCwd();
        if (p.empty()) continue;

        resolved = resolved.empty() ? p : p + "/" + resolved;
        resolvedAbsolute = p[0] == '/';
    }

    resolved = NormalizeString(resolved, !resolvedAbsolute);

    if (resolvedAbsolute) return "/" + resolved;
    return resolved.empty() ? "." : resolved;
}

static std::string PathDirname(const std::string& path) {
    if (path.empty()) return ".";

    bool hasRoot = path[0] == '/';
    int end = -1;
    bool matchedSlash = true;

    for (int i = static_cast<int>(path.size()) - 1; i >= 1; i--) {
        if (path[i] == '/') {
            if (!matchedSlash) {
                end = i;
                break;
            }
        } else {
            matchedSlash = false;
        }
    }

    if (end == -1) return hasRoot ? "/" : ".";
    if (hasRoot && end == 1) return "//";
    return path.substr(0, end);
}

static std::string PathBasename(const std::string& path, const std::string& ext) {
    int start = 0;
    int end = -1;
    bool matchedSlash = true;

    if (!ext.empty() && ext.size() <= path.size()) {
        if (ext == path) return "";

        int extIdx = static_cast<int>(ext.size()) - 1;
        int firstNonSlashEnd = -1;

        for (int i = static_cast<int>(path.size()) - 1; i >= 0; i--) {
            char c = path[i];
            if (c == '/') {
                if (!matchedSlash) {
                    start = i + 1;
                    break;
                }
            } else {
                if (firstNonSlashEnd == -1) {
                    matchedSlash = false;
                    firstNonSlashEnd = i + 1;
                }
                if (extIdx >= 0) {
                    // Try to match the explicit extension
                    if (c == ext[extIdx]) {
                        if (--extIdx == -1) end = i;
                    } else {
                        // Extension does not match, use the full base name
                        extIdx = -1;
                        end = firstNonSlashEnd;
                    }
                }
            }
        }

        if (start == end) {
            end = firstNonSlashEnd;
        } else if (end == -1) {
            end = static_cast<int>(path.size());
        }
        return path.substr(start, end - start);
    }

    for (int i = static_cast<int>(path.size()) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            if (!matchedSlash) {
                start = i + 1;
                break;
            }
        } else if (end == -1) {
            matchedSlash = false;
            end = i + 1;
        }
    }

    if (end == -1) return "";
    return path.substr(start, end - start);
}

static std::string PathExtname(const std::string& path) {
    int startDot = -1;
    int startPart = 0;
    int end = -1;
    bool matchedSlash = true;
    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    int preDotState = 0;

    for (int i = static_cast<int>(path.size()) - 1; i >= 0; i--) {
        char c = path[i];
        if (c == '/') {
            if (!matchedSlash) {
                startPart = i + 1;
                break;
            }
            continue;
        }
        if (end == -1) {
            matchedSlash = false;
            end = i + 1;
        }
        if (c == '.') {
            if (startDot == -1) {
                startDot = i;
            } else if (preDotState != 1) {
                preDotState = 1;
            }
        } else if (startDot != -1) {
            preDotState = -1;
        }
    }

    if (startDot == -1 || end == -1 ||
        // We saw a non-dot character immediately before the dot
        preDotState == 0 ||
        // The (right-most) trimmed path component is exactly '..'
        (preDotState == 1 && startDot == end - 1 && startDot == startPart + 1)) {
        return "";
    }
    return path.substr(startDot, end - startDot);
}

// ============================================
// JavaScript bindings
// ============================================

static bool GetStringArg(const FunctionCallbackInfo<Value>& args, int index,
                         const char* name, std::string& out) {
    Isolate* isolate = args.GetIsolate();
    if (index >= args.Length() || !args[index]->IsString()) {
        std::string error = std::string("The \"") + name + "\" argument must be of type string";
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked()));
        return false;
    }
    String::Utf8Value value(isolate, args[index]);
    out = *value;
    return true;
}

static bool GetStringArgs(const FunctionCallbackInfo<Value>& args, std::vector<std::string>& out) {
    for (int i = 0; i < args.Length(); i++) {
        std::string value;
        if (!GetStringArg(args, i, "path", value)) return false;
        out.push_back(value);
    }
    return true;
}

static void ReturnString(const FunctionCallbackInfo<Value>& args, const std::string& value) {
    args.GetReturnValue().Set(
        String::NewFromUtf8(args.GetIsolate(), value.c_str()).ToLocalChecked());
}

// path.join(...paths)
void PathJoinCallback(const FunctionCallbackInfo<Value>& args) {
    std::vector<std::string> paths;
    if (!GetStringArgs(args, paths)) return;
    ReturnString(args, PathJoin(paths));
}

// path.resolve(...paths)
void PathResolveCallback(const FunctionCallbackInfo<Value>& args) {
    std::vector<std::string> paths;
    if (!GetStringArgs(args, paths)) return;
    ReturnString(args, PathResolve(paths));
}

// path.normalize(path)
void PathNormalizeCallback(const FunctionCallbackInfo<Value>& args) {
    std::string path;
    if (!GetStringArg(args, 0, "path", path)) return;
    ReturnString(args, PathNormalize(path));
}

// path.dirname(path)
void PathDirnameCallback(const FunctionCallbackInfo<Value>& args) {
    std::string path;
    if (!GetStringArg(args, 0, "path", path)) return;
    ReturnString(args, PathDirname(path));
}

// path.basename(path[, ext])
void PathBasenameCallback(const FunctionCallbackInfo<Value>& args) {
    std::string path;
    if (!GetStringArg(args, 0, "path", path)) return;

    std::string ext;
    if (args.Length() > 1 && !args[1]->IsUndefined()) {
        if (!GetStringArg(args, 1, "ext", ext)) return;
    }
    ReturnString(args, PathBasename(path, ext));
}

// path.extname(path)
void PathExtnameCallback(const FunctionCallbackInfo<Value>& args) {
    std::string path;
    if (!GetStringArg(args, 0, "path", path)) return;
    ReturnString(args, PathExtname(path));
}

// path.isAbsolute(path)
void PathIsAbsoluteCallback(const FunctionCallbackInfo<Value>& args) {
    std::string path;
    if (!GetStringArg(args, 0, "path", path)) return;
    args.GetReturnValue().Set(!path.empty() && path[0] == '/');
}

// Set up global path object
void SetupPath(Isolate* isolate, Local<Context> context) {
    Local<Object> path = Object::New(isolate);

    struct { const char* name; FunctionCallback callback; } functions[] = {
        {"join", PathJoinCallback},
        {"resolve", PathResolveCallback},
        {"normalize", PathNormalizeCallback},
        {"dirname", PathDirnameCallback},
        {"basename", PathBasenameCallback},
        {"extname", PathExtnameCallback},
        {"isAbsolute", PathIsAbsoluteCallback},
    };

    for (const auto& fn : functions) {
        path->Set(
            context,
            String::NewFromUtf8(isolate, fn.name).ToLocalChecked(),
            FunctionTemplate::New(isolate, fn.callback)->GetFunction(context).ToLocalChecked()
        ).Check();
    }

    // path.sep and path.delimiter
    path->Set(
        context,
        String::NewFromUtf8(isolate, "sep").ToLocalChecked(),
        String::NewFromUtf8(isolate, "/").ToLocalChecked()
    ).Check();

    path->Set(
        context,
        String::NewFromUtf8(isolate, "delimiter").ToLocalChecked(),
        String::NewFromUtf8(isolate, ":").ToLocalChecked()
    ).Check();

    // Attach path to global
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "path").ToLocalChecked(),
        path
    ).Check();
}
