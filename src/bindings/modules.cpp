#include <v8.h>
#include <limits.h>
#include <stdlib.h>
#include <string>
#include "frogjs/errors.h"
#include "frogjs/js_module.h"
#include "frogjs/runtime.h"

using namespace v8;

// The CommonJS loader, following Node.js's algorithm: built-in names, relative
// and absolute paths, node_modules lookup, package.json main (and simple
// exports), .js and .json files, a cache that makes circular require() return
// the partial exports. ES modules aren't supported.
static const char* kModulesSource = R"JS(
(function (native, builtins, fs, path, mainFilename) {
'use strict';

const cache = Object.create(null);
const builtinNames = Object.keys(builtins);

function notFound(request, parent) {
  const stack = [];
  for (let m = parent; m; m = m.parent) if (m.filename) stack.push(m.filename);
  let message = `Cannot find module '${request}'`;
  if (stack.length > 0) message += '\nRequire stack:\n- ' + stack.join('\n- ');
  const err = new Error(message);
  err.code = 'MODULE_NOT_FOUND';
  err.requireStack = stack;
  return err;
}

function statKind(file) {
  const stats = fs.statSync(file, { throwIfNoEntry: false });
  if (!stats) return null;
  return stats.isDirectory() ? 'dir' : 'file';
}

function tryExtensions(file) {
  for (const extension of ['.js', '.json']) {
    if (statKind(file + extension) === 'file') return file + extension;
  }
  return null;
}

function readPackage(dir) {
  const file = path.join(dir, 'package.json');
  if (statKind(file) !== 'file') return null;
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (err) {
    err.message = `Error parsing ${file}: ${err.message}`;
    err.path = file;
    throw err;
  }
}

// The target of package.json "exports" for the package root: a string, or
// conditions where "require" and "default" apply. Subpath patterns aren't supported.
function exportsTarget(value, subpath) {
  if (typeof value === 'string') return subpath === '.' ? value : null;
  if (value === null || typeof value !== 'object') return null;
  if (Object.keys(value).some((key) => key.startsWith('.'))) {
    return subpath in value ? exportsTarget(value[subpath], '.') : null;
  }
  for (const condition of ['require', 'node', 'default']) {
    if (condition in value) {
      const target = exportsTarget(value[condition], subpath);
      if (target !== null) return target;
    }
  }
  return null;
}

function tryDirectory(dir) {
  const pkg = readPackage(dir);
  if (pkg && typeof pkg.main === 'string' && pkg.main !== '') {
    const main = path.resolve(dir, pkg.main);
    const found = tryPath(main, true);
    if (found) return found;
  }
  for (const name of ['index.js', 'index.json']) {
    const file = path.join(dir, name);
    if (statKind(file) === 'file') return file;
  }
  return null;
}

function tryPath(file, noDirectoryMain) {
  const kind = statKind(file);
  if (kind === 'file') return file;
  const withExtension = tryExtensions(file);
  if (withExtension) return withExtension;
  if (kind === 'dir') {
    if (noDirectoryMain) {
      for (const name of ['index.js', 'index.json']) {
        const index = path.join(file, name);
        if (statKind(index) === 'file') return index;
      }
      return null;
    }
    return tryDirectory(file);
  }
  return null;
}

function nodeModulePaths(from) {
  const paths = [];
  let dir = from;
  for (;;) {
    if (path.basename(dir) !== 'node_modules') paths.push(path.join(dir, 'node_modules'));
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return paths;
}

function resolvePackage(request, from) {
  const parts = request.split('/');
  const nameLength = request.startsWith('@') ? 2 : 1;
  const name = parts.slice(0, nameLength).join('/');
  const subpath = '.' + (parts.length > nameLength ? '/' + parts.slice(nameLength).join('/') : '');

  for (const root of nodeModulePaths(from)) {
    const packageDir = path.join(root, name);
    if (statKind(packageDir) !== 'dir') {
      const found = tryPath(path.join(root, request));
      if (found) return found;
      continue;
    }
    const pkg = readPackage(packageDir);
    if (pkg && pkg.exports !== undefined && pkg.exports !== null) {
      const target = exportsTarget(pkg.exports, subpath);
      if (target === null) {
        const err = new Error(`Package subpath '${subpath}' is not defined by "exports" in ${path.join(packageDir, 'package.json')}`);
        err.code = 'ERR_PACKAGE_PATH_NOT_EXPORTED';
        throw err;
      }
      const resolved = path.resolve(packageDir, target);
      // A target must stay inside its package
      if (resolved !== packageDir && !resolved.startsWith(packageDir + '/')) {
        const err = new Error(`Invalid "exports" target ${JSON.stringify(target)} in ${path.join(packageDir, 'package.json')}`);
        err.code = 'ERR_INVALID_PACKAGE_TARGET';
        throw err;
      }
      const found = tryPath(resolved);
      if (found) return found;
      continue;
    }
    const found = tryPath(path.join(root, request));
    if (found) return found;
  }
  return null;
}

function resolveFilename(request, parent) {
  const from = parent && parent.path ? parent.path : process.cwd();
  let found;
  if (request.startsWith('./') || request.startsWith('../') || request === '.' || request === '..' || path.isAbsolute(request)) {
    found = tryPath(path.resolve(from, request));
  } else {
    found = resolvePackage(request, from);
  }
  if (!found) throw notFound(request, parent);
  return native.realpath(found);
}

function builtinName(request) {
  if (request.startsWith('node:')) {
    const name = request.slice(5);
    if (Object.prototype.hasOwnProperty.call(builtins, name)) return name;
    const err = new Error(`No such built-in module: ${request}`);
    err.code = 'ERR_UNKNOWN_BUILTIN_MODULE';
    throw err;
  }
  return Object.prototype.hasOwnProperty.call(builtins, request) ? request : null;
}

function checkRequest(request) {
  if (typeof request !== 'string') {
    const err = new TypeError(`The "id" argument must be of type string. Received ${request === null ? 'null' : typeof request}`);
    err.code = 'ERR_INVALID_ARG_TYPE';
    throw err;
  }
  if (request === '') {
    const err = new TypeError("The argument 'id' must be a non-empty string. Received ''");
    err.code = 'ERR_INVALID_ARG_VALUE';
    throw err;
  }
}

class Module {
  constructor(id, parent) {
    this.id = id;
    this.path = path.dirname(id);
    this.exports = {};
    this.filename = null;
    this.loaded = false;
    this.children = [];
    this.paths = [];
    Object.defineProperty(this, 'parent', { value: parent, enumerable: false, writable: true });
    if (parent) parent.children.push(this);
  }

  require(request) {
    return requireFrom(this, request);
  }

  load(filename) {
    this.filename = filename;
    this.path = path.dirname(filename);
    this.paths = nodeModulePaths(this.path);
    if (filename.endsWith('.json')) {
      this.loadJson(filename);
    } else {
      this.compile(fs.readFileSync(filename, 'utf8'), filename);
    }
    this.loaded = true;
  }

  loadJson(filename) {
    try {
      this.exports = JSON.parse(stripBom(fs.readFileSync(filename, 'utf8')));
    } catch (err) {
      err.message = `${filename}: ${err.message}`;
      throw err;
    }
  }

  compile(source, filename) {
    const wrapper = native.compile(stripBom(source).replace(/^#!/, '//'), filename);
    wrapper.call(this.exports, this.exports, makeRequire(this), this, filename, this.path);
  }
}

function stripBom(text) {
  return text.charCodeAt(0) === 0xFEFF ? text.slice(1) : text;
}

function requireFrom(parent, request) {
  checkRequest(request);
  const builtin = builtinName(request);
  if (builtin !== null) return builtins[builtin];

  const filename = resolveFilename(request, parent);
  const cached = cache[filename];
  if (cached) {
    // Circular require: the partial exports, as they are now
    return cached.exports;
  }

  const module = new Module(filename, parent);
  cache[filename] = module;
  let loaded = false;
  try {
    module.load(filename);
    loaded = true;
  } finally {
    // A failed module can be required again
    if (!loaded) {
      delete cache[filename];
      if (parent) {
        const index = parent.children.indexOf(module);
        if (index >= 0) parent.children.splice(index, 1);
      }
    }
  }
  return module.exports;
}

let mainModule;

function makeRequire(module) {
  const require = (request) => requireFrom(module, request);
  require.resolve = (request) => {
    checkRequest(request);
    return builtinName(request) !== null ? request : resolveFilename(request, module);
  };
  require.cache = cache;
  require.main = mainModule;
  return require;
}

function runMain(filename) {
  const absolute = native.realpath(path.resolve(filename));
  const module = new Module('.', null);
  mainModule = module;
  module.filename = absolute;
  cache[absolute] = module;
  module.load(absolute);
}

builtins.module = {
  builtinModules: builtinNames.concat('module'),
  createRequire(from) {
    const module = new Module(String(from), null);
    module.path = path.dirname(String(from));
    module.filename = String(from);
    return makeRequire(module);
  },
};

// For --eval, stdin, the REPL and worker eval code: relative to the current directory
const contextModule = new Module(path.join(process.cwd(), mainFilename), null);
contextModule.filename = mainFilename;
contextModule.path = process.cwd();
contextModule.paths = nodeModulePaths(contextModule.path);

return { runMain, globalRequire: makeRequire(contextModule), contextModule };
})
)JS";

static thread_local Global<Function> tls_run_main;

// realpath(file): the canonical path, or file itself if it can't be resolved
static void RealPath(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    String::Utf8Value file(isolate, args[0]);
    char resolved[PATH_MAX];
    if (*file != nullptr && realpath(*file, resolved) != nullptr) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, resolved).ToLocalChecked());
    } else {
        args.GetReturnValue().Set(args[0]);
    }
}

// compile(source, filename): the module function. The wrapper's first line is
// hidden with a line offset of -1, so errors have the file's line numbers.
static void Compile(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> header = String::NewFromUtf8Literal(isolate, "(function (exports, require, module, __filename, __dirname) {\n");
    Local<String> footer = String::NewFromUtf8Literal(isolate, "\n})");
    Local<String> source = String::Concat(isolate, String::Concat(isolate, header, args[0].As<String>()), footer);

    ScriptOrigin origin(args[1], -1);
    Local<Script> script;
    Local<Value> result;
    if (!Script::Compile(context, source, &origin).ToLocal(&script) || !script->Run(context).ToLocal(&result)) return;
    args.GetReturnValue().Set(result);
}

namespace frogjs {

bool RunMainModule(Isolate* isolate, Local<Context> context, const std::string& filename) {
    HandleScope handle_scope(isolate);
    TryCatch try_catch(isolate);
    if (tls_run_main.IsEmpty()) return false;
    Local<Value> argv[1] = {String::NewFromUtf8(isolate, filename.c_str()).ToLocalChecked()};
    if (tls_run_main.Get(isolate)->Call(context, Undefined(isolate), 1, argv).IsEmpty()) {
        return try_catch.HasCaught() && HandleUncaught(isolate, &try_catch);
    }
    return true;
}

void ResetModuleState() {
    tls_run_main.Reset();
}

}  // namespace frogjs

// Set up require() (runs last: it needs every built-in module)
void SetupModules(Isolate* isolate, Local<Context> context, const std::string& mainFilePath) {
    Local<Object> global = context->Global();
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "realpath", RealPath);
    frogjs::SetFunction(isolate, context, native, "compile", Compile);

    auto get = [&](const char* name) {
        return global->Get(context, String::NewFromUtf8(isolate, name).ToLocalChecked()).ToLocalChecked();
    };
    auto set = [&](Local<Object> target, const char* name, Local<Value> value) {
        target->Set(context, String::NewFromUtf8(isolate, name).ToLocalChecked(), value).Check();
    };

    // require() keeps working when a script replaces the globals
    Local<Object> builtins = Object::New(isolate);
    for (const char* name : {"fs", "net", "http", "path", "os", "events", "stream", "util", "crypto",
                             "child_process", "worker_threads", "process"}) {
        set(builtins, name, get(name));
    }
    Local<Object> buffer_module = Object::New(isolate);
    set(buffer_module, "Buffer", get("Buffer"));
    set(builtins, "buffer", buffer_module);
    Local<Object> timers_module = Object::New(isolate);
    for (const char* name : {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "setImmediate", "clearImmediate"}) {
        set(timers_module, name, get(name));
    }
    set(builtins, "timers", timers_module);

    // A name in brackets ([eval], [stdin], [repl], [worker eval]) isn't a file
    bool pseudo_file = mainFilePath.empty() || mainFilePath[0] == '[';
    Local<Value> argv[5] = {native, builtins, get("fs"), get("path"),
        String::NewFromUtf8(isolate, pseudo_file ? mainFilePath.c_str() : "[main]").ToLocalChecked()};
    Local<Value> loader_value;
    if (!frogjs::RunJsModule(isolate, context, "modules", kModulesSource, 5, argv).ToLocal(&loader_value)) return;
    Local<Object> loader = loader_value.As<Object>();

    tls_run_main.Reset(isolate, loader->Get(context, String::NewFromUtf8Literal(isolate, "runMain")).ToLocalChecked().As<Function>());
    set(global, "require", loader->Get(context, String::NewFromUtf8Literal(isolate, "globalRequire")).ToLocalChecked());

    if (pseudo_file) {
        // The code isn't wrapped in a module function: these are globals
        Local<Object> module = loader->Get(context, String::NewFromUtf8Literal(isolate, "contextModule")).ToLocalChecked().As<Object>();
        set(global, "module", module);
        set(global, "exports", module->Get(context, String::NewFromUtf8Literal(isolate, "exports")).ToLocalChecked());
        set(global, "__filename", String::NewFromUtf8(isolate, mainFilePath.c_str()).ToLocalChecked());
        set(global, "__dirname", String::NewFromUtf8Literal(isolate, "."));
    }
}
