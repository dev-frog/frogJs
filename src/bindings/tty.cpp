#include <cstdlib>
#include <map>
#include <mutex>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <uv.h>
#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The tty module: isatty(), and ReadStream/WriteStream for terminals, with
// raw mode, window sizes and Node.js's rules for the color depth. It also
// gives process.stdin/stdout/stderr these methods when they are terminals.
static const char* kTtySource = R"JS(
(function (native, stream, fs, process) {
'use strict';

function codeError(Base, code, message) {
  const err = new Base(message);
  err.code = code;
  return err;
}

function isatty(fd) {
  return Number.isInteger(fd) && fd >= 0 && fd <= 2147483647 && native.isatty(fd);
}

// ---- Color depth, as Node.js decides it ----

const COLORS_2 = 1, COLORS_16 = 4, COLORS_256 = 8, COLORS_16m = 24;
const TERM_ENVS = {
  'eterm': COLORS_16, 'cons25': COLORS_16, 'console': COLORS_16, 'cygwin': COLORS_16, 'dtterm': COLORS_16,
  'gnome': COLORS_16, 'hurd': COLORS_16, 'jfbterm': COLORS_16, 'konsole': COLORS_16, 'kterm': COLORS_16,
  'mlterm': COLORS_16, 'mosh': COLORS_16m, 'putty': COLORS_16, 'st': COLORS_16, 'rxvt-unicode-24bit': COLORS_16m,
  'terminator': COLORS_16m, 'xterm-kitty': COLORS_16m,
};
const TERM_ENVS_REG_EXP = [/ansi/, /color/, /linux/, /direct/, /^con[0-9]*x[0-9]/, /^rxvt/, /^screen/, /^xterm/, /^vt100/, /^vt220/];
const CI_ENVS = ['APPVEYOR', 'BUILDKITE', 'CIRCLECI', 'DRONE', 'GITLAB_CI', 'TRAVIS'];

function getColorDepth(env = process.env) {
  if (env.FORCE_COLOR !== undefined) {
    switch (env.FORCE_COLOR) {
      case '':
      case '1':
      case 'true':
        return COLORS_16;
      case '2':
        return COLORS_256;
      case '3':
        return COLORS_16m;
      default:
        return COLORS_2;
    }
  }
  if (env.NODE_DISABLE_COLORS !== undefined || env.NO_COLOR !== undefined || env.TERM === 'dumb') return COLORS_2;
  if (env.TMUX) return COLORS_16m;
  if (env.CI) {
    if ('GITHUB_ACTIONS' in env || 'GITEA_ACTIONS' in env) return COLORS_16m;
    if (CI_ENVS.some((name) => name in env) || env.CI_NAME === 'codeship') return COLORS_256;
    return COLORS_2;
  }
  if ('TEAMCITY_VERSION' in env) return /^(9\.(0*[1-9]\d*)\.|\d{2,}\.)/.test(env.TEAMCITY_VERSION) ? COLORS_16 : COLORS_2;
  switch (env.TERM_PROGRAM) {
    case 'iTerm.app':
      if (!env.TERM_PROGRAM_VERSION || /^[0-2]\./.test(env.TERM_PROGRAM_VERSION)) return COLORS_256;
      return COLORS_16m;
    case 'HyperTerm':
    case 'MacTerm':
      return COLORS_16m;
    case 'Apple_Terminal':
      return COLORS_256;
  }
  if (env.COLORTERM === 'truecolor' || env.COLORTERM === '24bit') return COLORS_16m;
  if (env.TERM) {
    if (/truecolor/.test(env.TERM)) return COLORS_16m;
    if (/^xterm-256/.test(env.TERM)) return COLORS_256;
    const term = env.TERM.toLowerCase();
    if (TERM_ENVS[term]) return TERM_ENVS[term];
    if (TERM_ENVS_REG_EXP.some((pattern) => pattern.test(term))) return COLORS_16;
  }
  if (env.COLORTERM) return COLORS_16;
  return COLORS_2;
}

function hasColors(count, env) {
  if (env === undefined && (count === undefined || (typeof count === 'object' && count !== null))) {
    env = count;
    count = 16;
  } else if (!Number.isInteger(count) || count < 2) {
    throw codeError(RangeError, 'ERR_OUT_OF_RANGE', `The value of "count" is out of range. It must be >= 2. Received ${count}`);
  }
  return count <= 2 ** getColorDepth(env);
}

// ---- Cursor control (also readline's) ----

const CSI = '\x1b[';

function done(callback) {
  if (typeof callback === 'function') process.nextTick(callback, null);
  return true;
}

function cursorTo(target, x, y, callback) {
  if (typeof y === 'function') {
    callback = y;
    y = undefined;
  }
  if (Number.isNaN(x)) throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'x' is invalid. Received NaN`);
  if (Number.isNaN(y)) throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'y' is invalid. Received NaN`);
  if (target == null || (typeof x !== 'number' && typeof y !== 'number')) return done(callback);
  if (typeof x !== 'number') throw codeError(TypeError, 'ERR_INVALID_CURSOR_POS', 'Cannot set cursor row without setting its column');
  const data = typeof y !== 'number' ? `${CSI}${x + 1}G` : `${CSI}${y + 1};${x + 1}H`;
  return target.write(data, callback);
}

function moveCursor(target, dx, dy, callback) {
  if (target == null || !(dx || dy)) return done(callback);
  let data = '';
  if (dx < 0) data += `${CSI}${-dx}D`;
  else if (dx > 0) data += `${CSI}${dx}C`;
  if (dy < 0) data += `${CSI}${-dy}A`;
  else if (dy > 0) data += `${CSI}${dy}B`;
  return target.write(data, callback);
}

// dir: -1 to the left of the cursor, 1 to the right, 0 the whole line
function clearLine(target, dir, callback) {
  if (target == null) return done(callback);
  const type = dir < 0 ? `${CSI}1K` : dir > 0 ? `${CSI}0K` : `${CSI}2K`;
  return target.write(type, callback);
}

function clearScreenDown(target, callback) {
  if (target == null) return done(callback);
  return target.write(`${CSI}0J`, callback);
}

// The methods of a terminal output stream: WriteStream's, which
// process.stdout and process.stderr get too when they are terminals
const writeMethods = {
  getWindowSize() { return native.getWindowSize(this.fd) || [80, 24]; },
  getColorDepth(env) { return getColorDepth(env); },
  hasColors(count, env) { return hasColors(count, env); },
  cursorTo(x, y, callback) { return cursorTo(this, x, y, callback); },
  moveCursor(dx, dy, callback) { return moveCursor(this, dx, dy, callback); },
  clearLine(dir, callback) { return clearLine(this, dir, callback); },
  clearScreenDown(callback) { return clearScreenDown(this, callback); },
};
const sizeAccessors = {
  columns: { configurable: true, enumerable: true, get() { return (native.getWindowSize(this.fd) || [80, 24])[0]; } },
  rows: { configurable: true, enumerable: true, get() { return (native.getWindowSize(this.fd) || [80, 24])[1]; } },
};

function addWriteMethods(target) {
  for (const [name, method] of Object.entries(writeMethods)) {
    Object.defineProperty(target, name, { value: method, writable: true, configurable: true, enumerable: false });
  }
  Object.defineProperties(target, sizeAccessors);
}

function addRawMode(target, fd) {
  target.isRaw = false;
  target.setRawMode = function (flag) {
    flag = Boolean(flag);
    if (!native.setRawMode(fd, flag)) {
      throw codeError(Error, 'ERR_TTY_INIT_FAILED', `TTY initialization failed: uv_tty_set_mode`);
    }
    this.isRaw = flag;
    return this;
  };
}

class ReadStream extends fs.ReadStream {
  constructor(fd, options) {
    if (!isatty(fd)) throw codeError(Error, 'ERR_TTY_INIT_FAILED', 'TTY initialization failed: uv_tty_init returned EINVAL (invalid argument)');
    super(null, { ...options, fd, autoClose: false, highWaterMark: 64 * 1024 });
    this.isTTY = true;
    addRawMode(this, fd);
  }
}

class WriteStream extends stream.Writable {
  constructor(fd) {
    if (!isatty(fd)) throw codeError(Error, 'ERR_TTY_INIT_FAILED', 'TTY initialization failed: uv_tty_init returned EINVAL (invalid argument)');
    super({
      write(chunk, encoding, callback) {
        try {
          fs.writeSync(fd, chunk);
          callback();
        } catch (err) {
          callback(err);
        }
      },
    });
    this.fd = fd;
    this.isTTY = true;
  }
}
for (const [name, method] of Object.entries(writeMethods)) {
  Object.defineProperty(WriteStream.prototype, name, { value: method, writable: true, configurable: true });
}
Object.defineProperties(WriteStream.prototype, sizeAccessors);

// process.stdout and process.stderr get the terminal methods when they are terminals
for (const [name, fd] of [['stdout', 1], ['stderr', 2]]) {
  const target = process[name];
  if (target && target.isTTY && target.fd === fd) addWriteMethods(target);
}
const stdinDescriptor = Object.getOwnPropertyDescriptor(process, 'stdin');
if (stdinDescriptor && stdinDescriptor.get) {
  let withRawMode;
  Object.defineProperty(process, 'stdin', {
    configurable: true,
    enumerable: true,
    get() {
      const stdin = stdinDescriptor.get.call(process);
      if (stdin !== withRawMode && stdin.isTTY) {
        addRawMode(stdin, 0);
        withRawMode = stdin;
      }
      return stdin;
    },
  });
}

const tty = { isatty, ReadStream, WriteStream };
// For readline, which has the same cursor functions
Object.defineProperty(tty, Symbol.for('frogjs.tty.cursor'), { value: { cursorTo, moveCursor, clearLine, clearScreenDown } });
return tty;
})
)JS";

namespace {

// The terminal settings before raw mode, restored by setRawMode(false) and at exit
std::mutex g_raw_mutex;
std::map<int, struct termios> g_original_modes;

void RestoreModes() {
    std::lock_guard<std::mutex> lock(g_raw_mutex);
    for (const auto& entry : g_original_modes) tcsetattr(entry.first, TCSANOW, &entry.second);
    g_original_modes.clear();
}

void IsATTY(const FunctionCallbackInfo<Value>& args) {
    int fd = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(-1);
    args.GetReturnValue().Set(fd >= 0 && uv_guess_handle(fd) == UV_TTY);
}

// getWindowSize(fd): [columns, rows], or undefined if fd isn't a terminal
void GetWindowSize(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    int fd = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
    struct winsize size;
    if (fd < 0 || ioctl(fd, TIOCGWINSZ, &size) != 0 || size.ws_col == 0) return;
    Local<Value> items[2] = {Integer::New(isolate, size.ws_col), Integer::New(isolate, size.ws_row)};
    args.GetReturnValue().Set(Array::New(isolate, items, 2));
}

// setRawMode(fd, enable): keystrokes arrive one by one, unechoed, with no
// line editing or signals (as libuv's UV_TTY_MODE_RAW)
void SetRawMode(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    int fd = args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1);
    bool enable = args[1]->BooleanValue(isolate);
    std::lock_guard<std::mutex> lock(g_raw_mutex);
    auto saved = g_original_modes.find(fd);
    if (!enable) {
        if (saved != g_original_modes.end()) {
            tcsetattr(fd, TCSANOW, &saved->second);
            g_original_modes.erase(saved);
        }
        args.GetReturnValue().Set(true);
        return;
    }
    struct termios original;
    if (fd < 0 || tcgetattr(fd, &original) != 0) {
        args.GetReturnValue().Set(false);
        return;
    }
    if (saved == g_original_modes.end()) {
        static bool registered = false;
        if (!registered) {
            atexit(RestoreModes);
            registered = true;
        }
        g_original_modes[fd] = original;
    }
    struct termios raw = saved == g_original_modes.end() ? original : saved->second;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag |= ONLCR;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    args.GetReturnValue().Set(tcsetattr(fd, TCSADRAIN, &raw) == 0);
}

}  // namespace

// Set up the tty module (requires SetupProcess, SetupStream and SetupFileSystem first)
void SetupTTY(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "isatty", IsATTY);
    frogjs::SetFunction(isolate, context, native, "getWindowSize", GetWindowSize);
    frogjs::SetFunction(isolate, context, native, "setRawMode", SetRawMode);
    Local<Value> argv[4] = {native, frogjs::GetBuiltin(isolate, context, "stream"), frogjs::GetBuiltin(isolate, context, "fs"),
                            frogjs::GetBuiltin(isolate, context, "process")};
    Local<Value> module;
    if (!frogjs::RunJsModule(isolate, context, "tty", kTtySource, 4, argv).ToLocal(&module)) return;
    frogjs::RegisterBuiltin(isolate, context, "tty", module);
}
