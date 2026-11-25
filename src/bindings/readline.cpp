#include <memory>
#include <string>
#include <vector>
#include <v8.h>
#include "frogjs/js_module.h"
#include "frogjs/line_editor.h"
#include "frogjs/v8_compat.h"

using namespace v8;

// The readline module. On a terminal, lines are edited by the REPL's line
// editor (history, completion, Emacs keys), fed the input stream's bytes; on
// anything else input is split into lines in JavaScript. readline/promises
// has the same Interface with a question() that returns a promise.
static const char* kReadlineSource = R"JS(
(function (native, EventEmitter, events, StringDecoder, tty, process) {
'use strict';

const kCursor = Symbol.for('frogjs.tty.cursor');
const { cursorTo, moveCursor, clearLine, clearScreenDown } = tty[kCursor];

function codeError(Base, code, message) {
  const err = new Base(message);
  err.code = code;
  return err;
}

function describe(value) {
  if (value === null || value === undefined) return String(value);
  if (typeof value === 'function') return `function ${value.name}`;
  if (typeof value === 'object') return value.constructor && value.constructor.name ? `an instance of ${value.constructor.name}` : 'an object';
  return `type ${typeof value} (${JSON.stringify(value)})`;
}

class AbortError extends Error {
  constructor(message = 'The operation was aborted', options = undefined) {
    super(message, options);
    this.code = 'ABORT_ERR';
    this.name = 'AbortError';
  }
}

// ---- Keypress events: Node.js's decoder of terminal input ----

const kEscape = '\x1b';
const kKeypressDecoder = Symbol('keypress-decoder');
const kEscapeDecoder = Symbol('escape-decoder');

const charLengthAt = (str, i) => (str.codePointAt(i) > 0xffff ? 2 : 1);

const keyNames = {
  '[P': 'f1', '[Q': 'f2', '[R': 'f3', '[S': 'f4', 'OP': 'f1', 'OQ': 'f2', 'OR': 'f3', 'OS': 'f4',
  '[11~': 'f1', '[12~': 'f2', '[13~': 'f3', '[14~': 'f4', '[200~': 'paste-start', '[201~': 'paste-end',
  '[[A': 'f1', '[[B': 'f2', '[[C': 'f3', '[[D': 'f4', '[[E': 'f5',
  '[15~': 'f5', '[17~': 'f6', '[18~': 'f7', '[19~': 'f8', '[20~': 'f9', '[21~': 'f10', '[23~': 'f11', '[24~': 'f12',
  '[A': 'up', '[B': 'down', '[C': 'right', '[D': 'left', '[E': 'clear', '[F': 'end', '[H': 'home',
  'OA': 'up', 'OB': 'down', 'OC': 'right', 'OD': 'left', 'OE': 'clear', 'OF': 'end', 'OH': 'home',
  '[1~': 'home', '[2~': 'insert', '[3~': 'delete', '[4~': 'end', '[5~': 'pageup', '[6~': 'pagedown',
  '[[5~': 'pageup', '[[6~': 'pagedown', '[7~': 'home', '[8~': 'end',
};
const shiftedKeys = {
  '[a': 'up', '[b': 'down', '[c': 'right', '[d': 'left', '[e': 'clear', '[2$': 'insert', '[3$': 'delete',
  '[5$': 'pageup', '[6$': 'pagedown', '[7$': 'home', '[8$': 'end', '[Z': 'tab',
};
const ctrlKeys = {
  'Oa': 'up', 'Ob': 'down', 'Oc': 'right', 'Od': 'left', 'Oe': 'clear', '[2^': 'insert', '[3^': 'delete',
  '[5^': 'pageup', '[6^': 'pagedown', '[7^': 'home', '[8^': 'end',
};

function* emitKeys(stream) {
  for (;;) {
    let ch = yield;
    let s = ch;
    let escaped = false;
    const key = { sequence: null, name: undefined, ctrl: false, meta: false, shift: false };
    if (ch === kEscape) {
      escaped = true;
      s += (ch = yield);
      if (ch === kEscape) s += (ch = yield);
    }
    if (escaped && (ch === 'O' || ch === '[')) {
      let code = ch;
      let modifier = 0;
      if (ch === 'O') {
        s += (ch = yield);
        if (ch >= '0' && ch <= '9') {
          modifier = (ch >> 0) - 1;
          s += (ch = yield);
        }
        code += ch;
      } else {
        s += (ch = yield);
        if (ch === '[') {
          code += ch;
          s += (ch = yield);
        }
        const cmdStart = s.length - 1;
        if (ch >= '0' && ch <= '9') {
          s += (ch = yield);
          if (ch >= '0' && ch <= '9') {
            s += (ch = yield);
            if (ch >= '0' && ch <= '9') s += (ch = yield);
          }
        }
        if (ch === ';') {
          s += (ch = yield);
          if (ch >= '0' && ch <= '9') s += yield;
        }
        const cmd = s.slice(cmdStart);
        let match;
        if ((match = /^(?:(\d\d?)(?:;(\d))?([~^$])|(\d{3}~))$/.exec(cmd))) {
          if (match[4]) {
            code += match[4];
          } else {
            code += match[1] + match[3];
            modifier = (match[2] || 1) - 1;
          }
        } else if ((match = /^((\d;)?(\d))?([A-Za-z])$/.exec(cmd))) {
          code += match[4];
          modifier = (match[3] || 1) - 1;
        } else {
          code += cmd;
        }
      }
      key.ctrl = !!(modifier & 4);
      key.meta = !!(modifier & 10);
      key.shift = !!(modifier & 1);
      key.code = code;
      if (code in keyNames) {
        key.name = keyNames[code];
      } else if (code in shiftedKeys) {
        key.name = shiftedKeys[code];
        key.shift = true;
      } else if (code in ctrlKeys) {
        key.name = ctrlKeys[code];
        key.ctrl = true;
      } else {
        key.name = 'undefined';
      }
    } else if (ch === '\r') {
      key.name = 'return';
      key.meta = escaped;
    } else if (ch === '\n') {
      key.name = 'enter';
      key.meta = escaped;
    } else if (ch === '\t') {
      key.name = 'tab';
      key.meta = escaped;
    } else if (ch === '\b' || ch === '\x7f') {
      key.name = 'backspace';
      key.meta = escaped;
    } else if (ch === kEscape) {
      key.name = 'escape';
      key.meta = escaped;
    } else if (ch === ' ') {
      key.name = 'space';
      key.meta = escaped;
    } else if (!escaped && ch <= '\x1a') {
      key.name = String.fromCharCode(ch.charCodeAt(0) + 'a'.charCodeAt(0) - 1);
      key.ctrl = true;
    } else if (/^[0-9A-Za-z]$/.test(ch)) {
      key.name = ch.toLowerCase();
      key.shift = /^[A-Z]$/.test(ch);
      key.meta = escaped;
    } else if (escaped) {
      key.name = ch.length ? undefined : 'escape';
      key.meta = true;
    }
    key.sequence = s;
    if (s.length !== 0 && (key.name !== undefined || escaped)) {
      stream.emit('keypress', escaped ? undefined : s, key);
    } else if (charLengthAt(s, 0) === s.length) {
      stream.emit('keypress', s, key);
    }
  }
}

// emitKeypressEvents(stream): 'keypress' events (str, key) for its input
function emitKeypressEvents(stream, iface = {}) {
  if (stream[kKeypressDecoder]) return;
  stream[kKeypressDecoder] = new StringDecoder('utf8');
  stream[kEscapeDecoder] = emitKeys(stream);
  stream[kEscapeDecoder].next();
  const triggerEscape = () => stream[kEscapeDecoder].next('');
  const { escapeCodeTimeout = 500 } = iface;
  let timeoutId;

  function onData(input) {
    if (stream.listenerCount('keypress') > 0) {
      const string = stream[kKeypressDecoder].write(input);
      if (!string) return;
      clearTimeout(timeoutId);
      let length = 0;
      for (const character of string) {
        length += character.length;
        try {
          stream[kEscapeDecoder].next(character);
          // A lone escape at the end: the escape key, unless more follows soon
          if (length === string.length && character === kEscape) timeoutId = setTimeout(triggerEscape, escapeCodeTimeout);
        } catch (err) {
          stream[kEscapeDecoder] = emitKeys(stream);
          stream[kEscapeDecoder].next();
          throw err;
        }
      }
    } else {
      stream.removeListener('data', onData);
      stream.on('newListener', onNewListener);
    }
  }
  function onNewListener(event) {
    if (event === 'keypress') {
      stream.on('data', onData);
      stream.removeListener('newListener', onNewListener);
    }
  }
  if (stream.listenerCount('keypress') > 0) stream.on('data', onData);
  else stream.on('newListener', onNewListener);
}

// ---- Interface ----

const lineEnding = /\r?\n|\r(?!\n)/g;

class Interface extends EventEmitter {
  constructor(input, output, completer, terminal) {
    super();
    let options = input;
    if (!(input && typeof input.on === 'function' && !('input' in input))) {
      options = input || {};
    } else {
      options = { input, output, completer, terminal };
    }
    ({ input, output, completer, terminal } = options);
    if (!input || typeof input.on !== 'function') {
      throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "input" argument must be an instance of Stream. Received ${describe(input)}`);
    }
    if (completer !== undefined && typeof completer !== 'function') {
      throw codeError(TypeError, 'ERR_INVALID_ARG_VALUE', `The argument 'completer' is invalid. Received ${describe(completer)}`);
    }
    const signal = options.signal;
    if (signal !== undefined && (signal === null || typeof signal !== 'object' || !('aborted' in signal))) {
      throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "options.signal" property must be an instance of AbortSignal. Received ${describe(signal)}`);
    }
    const crlfDelay = options.crlfDelay === undefined ? 100 : Math.max(100, Number(options.crlfDelay));
    const historySize = options.historySize === undefined ? 30 : options.historySize;
    if (typeof historySize !== 'number' || Number.isNaN(historySize) || historySize < 0) {
      throw codeError(RangeError, 'ERR_INVALID_ARG_VALUE', `The argument 'historySize' must be a positive number. Received ${describe(historySize)}`);
    }

    this.input = input;
    this.output = output;
    this.terminal = terminal === undefined ? !!(output && output.isTTY) : !!terminal;
    this.completer = completer;
    this.crlfDelay = crlfDelay;
    this.historySize = historySize;
    this.removeHistoryDuplicates = !!options.removeHistoryDuplicates;
    this.history = Array.isArray(options.history) ? options.history.slice() : [];
    this.line = '';
    this.cursor = 0;
    this.closed = false;
    this.paused = false;
    this._prompt = options.prompt === undefined ? '> ' : String(options.prompt);
    this._questionCallback = null;
    this._sawReturnAt = 0;
    this._buffer = '';
    this._editor = null;
    this._decoder = new StringDecoder('utf8');

    // A terminal: the line editor, on the input's file descriptor
    if (this.terminal && typeof input.fd === 'number' && tty.isatty(input.fd)) {
      this._editor = native.createEditor(input.fd);
      for (const line of this.history.slice().reverse()) this._editor.addHistory(line);
      if (completer) {
        this._editor.setCompleter((line) => {
          let result;
          completer(line, (err, value) => { result = err ? undefined : value; });
          if (result === undefined) result = completer(line);
          if (!Array.isArray(result) || !Array.isArray(result[0])) return null;
          const [hits, substring] = result;
          return [hits.map(String), line.length - String(substring).length];
        });
      }
    }

    // As in Node.js the terminal stays in raw mode until close()
    if (this._editor) this._editor.start('', false);

    this._onData = (data) => this._ondata(data);
    this._onEnd = () => this._onend();
    input.on('data', this._onData);
    input.on('end', this._onEnd);
    if (this.terminal && typeof input.setRawMode === 'function' && !this._editor) input.setRawMode(true);
    if (typeof input.resume === 'function') input.resume();

    if (signal) {
      const onAbort = () => this.close();
      if (signal.aborted) process.nextTick(onAbort);
      else {
        signal.addEventListener('abort', onAbort, { once: true });
        this.once('close', () => signal.removeEventListener('abort', onAbort));
      }
    }
  }

  get columns() {
    return this.output && this.output.columns ? this.output.columns : Infinity;
  }

  setPrompt(prompt) {
    this._prompt = String(prompt);
    if (this._editor) this._editor.setPrompt(this._prompt);
  }

  getPrompt() {
    return this._prompt;
  }

  prompt(preserveCursor) {
    if (this.paused) this.resume();
    if (this.closed) throw codeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed');
    if (this._editor) {
      this._editor.start(this._prompt);
      this.line = '';
      this.cursor = 0;
    } else if (this.output && typeof this.output.write === 'function') {
      void preserveCursor;
      this.output.write(this._prompt);
    }
  }

  question(query, options, cb) {
    if (typeof options === 'function') {
      cb = options;
      options = {};
    }
    options = options || {};
    if (this.closed) throw codeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed');
    if (typeof cb !== 'function') return;
    const signal = options.signal;
    if (signal) {
      if (signal.aborted) return;
      const onAbort = () => {
        if (this._questionCallback === wrapped) this._questionCancel();
      };
      signal.addEventListener('abort', onAbort, { once: true });
      const original = cb;
      cb = (answer) => {
        signal.removeEventListener('abort', onAbort);
        original(answer);
      };
    }
    const wrapped = cb;
    if (this._questionCallback) {
      this.prompt();
      return;
    }
    this._oldPrompt = this._prompt;
    this.setPrompt(query);
    this._questionCallback = wrapped;
    this.prompt();
  }

  _questionCancel() {
    if (this._questionCallback) {
      this._questionCallback = null;
      this.setPrompt(this._oldPrompt);
      if (this._editor) this._editor.start('', false);
    }
  }

  pause() {
    if (this.paused) return this;
    if (typeof this.input.pause === 'function') this.input.pause();
    this.paused = true;
    this.emit('pause');
    return this;
  }

  resume() {
    if (!this.paused) return this;
    if (typeof this.input.resume === 'function') this.input.resume();
    this.paused = false;
    this.emit('resume');
    return this;
  }

  write(data, key) {
    if (this.paused) this.resume();
    if (this.closed) throw codeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed');
    if (this._editor) {
      if (key && key.ctrl && key.name === 'c') this._feed(Buffer.from([3]));
      else if (key && key.ctrl && key.name === 'd') this._feed(Buffer.from([4]));
      else if (data !== undefined && data !== null) this._feed(Buffer.from(String(data)));
    } else if (data !== undefined && data !== null) {
      this._normalWrite(typeof data === 'string' ? data : this._decoder.write(data));
    }
  }

  close() {
    if (this.closed) return;
    this.pause();
    if (this._editor) {
      this._editor.stop();
      this._editor.destroy();
      this._editor = null;
    } else if (this.terminal && typeof this.input.setRawMode === 'function') {
      this.input.setRawMode(false);
    }
    this.input.removeListener('data', this._onData);
    this.input.removeListener('end', this._onEnd);
    this.closed = true;
    this.emit('close');
  }

  [Symbol.dispose || Symbol.for('nodejs.dispose')]() {
    this.close();
  }

  getCursorPos() {
    const columns = this.columns;
    const offset = this._prompt.length + this.cursor;
    return columns === Infinity ? { rows: 0, cols: offset } : { rows: Math.floor(offset / columns), cols: offset % columns };
  }

  _ondata(data) {
    if (this._editor) {
      this._feed(data);
      return;
    }
    this._normalWrite(typeof data === 'string' ? data : this._decoder.write(data));
  }

  _onend() {
    const rest = this._decoder.end();
    if (rest) this._normalWrite(rest);
    if (this._buffer.length > 0) {
      const line = this._buffer;
      this._buffer = '';
      this._onLine(line);
    }
    this.close();
  }

  // Lines from plain input; "\r\n" split over two chunks is one line end
  _normalWrite(text) {
    if (text === '') return;
    if (this._sawReturnAt && Date.now() - this._sawReturnAt <= this.crlfDelay && text[0] === '\n') text = text.slice(1);
    this._sawReturnAt = text.endsWith('\r') ? Date.now() : 0;
    const parts = (this._buffer + text).split(lineEnding);
    this._buffer = parts.pop();
    for (const line of parts) {
      if (this.closed) break;
      this._onLine(line);
    }
  }

  _feed(data) {
    const editor = this._editor;
    const bytes = typeof data === 'string' ? Buffer.from(data) : data;
    // Keys typed before prompt() are edited too, after what is on the line
    if (!editor.active()) editor.start(this._prompt === this._oldPrompt || !this._questionCallback ? '' : this._prompt, false);
    for (const [type, line] of editor.feed(bytes)) {
      if (type === 'line') {
        // Keep editing (raw mode) for the next line, without a prompt until prompt()
        editor.start('', false);
        this._addHistory(line);
        this._onLine(line);
      } else if (type === 'eof') {
        this.close();
        return;
      } else if (type === 'interrupt') {
        editor.start('', false);
        if (this.listenerCount('SIGINT') > 0) {
          this.emit('SIGINT');
        } else {
          this.close();
          return;
        }
      }
      if (this.closed || this._editor !== editor) return;
    }
    this.line = editor.line();
    this.cursor = editor.cursor();
  }

  _addHistory(line) {
    if (line.length === 0 || this.historySize === 0) return;
    if (this.removeHistoryDuplicates) {
      const index = this.history.indexOf(line);
      if (index !== -1) this.history.splice(index, 1);
    }
    if (this.history[0] !== line) this.history.unshift(line);
    while (this.history.length > this.historySize) this.history.pop();
    if (this._editor) this._editor.addHistory(line);
    this.emit('history', this.history);
  }

  _onLine(line) {
    this.line = '';
    this.cursor = 0;
    if (this._questionCallback) {
      const callback = this._questionCallback;
      this._questionCallback = null;
      this.setPrompt(this._oldPrompt);
      callback(line);
    } else {
      this.emit('line', line);
    }
  }

  // Lines as they arrive; the iteration ends when the interface closes
  [Symbol.asyncIterator]() {
    const iterator = events.on(this, 'line', { close: ['close'] });
    const lines = {
      next: async () => {
        const result = await iterator.next();
        return result.done ? result : { value: result.value[0], done: false };
      },
      return: async () => {
        this.close();
        return iterator.return();
      },
      throw: (err) => iterator.throw(err),
      [Symbol.asyncIterator]() { return this; },
    };
    return lines;
  }
}

function createInterface(input, output, completer, terminal) {
  return new Interface(input, output, completer, terminal);
}

const readline = {
  Interface, createInterface, emitKeypressEvents, clearLine, clearScreenDown, cursorTo, moveCursor,
};

// ---- readline/promises ----

class PromisesInterface extends Interface {
  question(query, options = {}) {
    return new Promise((resolve, reject) => {
      if (this.closed) {
        reject(codeError(Error, 'ERR_USE_AFTER_CLOSE', 'readline was closed'));
        return;
      }
      const signal = options && options.signal;
      if (signal) {
        if (signal.aborted) {
          reject(new AbortError(undefined, { cause: signal.reason }));
          return;
        }
        signal.addEventListener('abort', () => {
          this._questionCancel();
          reject(new AbortError(undefined, { cause: signal.reason }));
        }, { once: true });
      }
      super.question(query, {}, resolve);
    });
  }
}

// readline/promises' Readline: cursor commands, queued until commit()
class Readline {
  #stream;
  #todo = [];
  #autoCommit;

  constructor(stream, options = undefined) {
    if (!stream || typeof stream.write !== 'function') {
      throw codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "stream" argument must be an instance of Writable. Received ${describe(stream)}`);
    }
    this.#stream = stream;
    this.#autoCommit = !!(options && options.autoCommit);
  }

  #add(data) {
    if (this.#autoCommit) process.nextTick(() => this.#stream.write(data));
    else this.#todo.push(data);
    return this;
  }

  cursorTo(x, y = undefined) {
    if (typeof y !== 'number') return this.#add(`\x1b[${x + 1}G`);
    return this.#add(`\x1b[${y + 1};${x + 1}H`);
  }

  moveCursor(dx, dy) {
    let data = '';
    if (dx < 0) data += `\x1b[${-dx}D`;
    else if (dx > 0) data += `\x1b[${dx}C`;
    if (dy < 0) data += `\x1b[${-dy}A`;
    else if (dy > 0) data += `\x1b[${dy}B`;
    return data ? this.#add(data) : this;
  }

  clearLine(dir) {
    return this.#add(dir < 0 ? '\x1b[1K' : dir > 0 ? '\x1b[0K' : '\x1b[2K');
  }

  clearScreenDown() {
    return this.#add('\x1b[0J');
  }

  commit() {
    const data = this.#todo.join('');
    this.#todo = [];
    return new Promise((resolve) => this.#stream.write(data, () => resolve()));
  }

  rollback() {
    this.#todo = [];
    return this;
  }
}

const promises = {
  Interface: PromisesInterface,
  Readline,
  createInterface(input, output, completer, terminal) {
    return new PromisesInterface(input, output, completer, terminal);
  },
};
readline.promises = promises;

return { readline, promises };
})
)JS";

namespace {

struct EditorHolder {
    std::unique_ptr<frogjs::LineEditor> editor;
    Global<Function> completer;
    Global<Object> wrapper;
};

// Each method gets its holder as data; destroy() frees the editor, the
// holder itself goes when the wrapper is collected
EditorHolder* Holder(const FunctionCallbackInfo<Value>& args) {
    auto* holder = frogjs::ExternalValue<EditorHolder>(args.Data());
    return holder != nullptr && holder->editor ? holder : nullptr;
}

std::string ToUtf8(Isolate* isolate, Local<Value> value) {
    String::Utf8Value utf8(isolate, value);
    return *utf8 == nullptr ? std::string() : std::string(*utf8, utf8.length());
}

// start(prompt[, draw])
void Start(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) holder->editor->Start(ToUtf8(args.GetIsolate(), args[0]), !args[1]->IsFalse());
}

void Active(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) args.GetReturnValue().Set(holder->editor->active());
}

void Stop(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) holder->editor->Stop();
}

void SetPrompt(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) holder->editor->SetPrompt(ToUtf8(args.GetIsolate(), args[0]));
}

void AddHistory(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) holder->editor->AddHistory(ToUtf8(args.GetIsolate(), args[0]));
}

void Line(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) {
        const std::string& line = holder->editor->line();
        args.GetReturnValue().Set(String::NewFromUtf8(args.GetIsolate(), line.data(), NewStringType::kNormal,
                                                      static_cast<int>(line.size())).ToLocalChecked());
    }
}

// cursor(): the cursor's position in characters (UTF-16 code units, as JavaScript counts)
void Cursor(const FunctionCallbackInfo<Value>& args) {
    if (EditorHolder* holder = Holder(args)) {
        const std::string& line = holder->editor->line();
        std::string before = line.substr(0, holder->editor->cursor());
        Local<String> text = String::NewFromUtf8(args.GetIsolate(), before.data(), NewStringType::kNormal,
                                                 static_cast<int>(before.size())).ToLocalChecked();
        args.GetReturnValue().Set(text->Length());
    }
}

// setCompleter(fn): fn(line) returns [candidates, wordStart] or null
void SetCompleter(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    EditorHolder* holder = Holder(args);
    if (holder == nullptr || !args[0]->IsFunction()) return;
    holder->completer.Reset(isolate, args[0].As<Function>());
    holder->editor->SetCompleter([holder, isolate](const std::string& line, size_t* word_start) {
        std::vector<std::string> candidates;
        HandleScope handle_scope(isolate);
        Local<Context> context = isolate->GetCurrentContext();
        TryCatch try_catch(isolate);
        Local<Value> argv[1] = {String::NewFromUtf8(isolate, line.data(), NewStringType::kNormal, static_cast<int>(line.size())).ToLocalChecked()};
        Local<Value> result;
        if (!holder->completer.Get(isolate)->Call(context, Undefined(isolate), 1, argv).ToLocal(&result) || !result->IsArray()) {
            *word_start = line.size();
            return candidates;
        }
        Local<Array> pair = result.As<Array>();
        Local<Value> hits = pair->Get(context, 0).ToLocalChecked();
        Local<Value> start = pair->Get(context, 1).ToLocalChecked();
        // The JavaScript start counts UTF-16 units; the editor counts bytes
        uint32_t units = start->Uint32Value(context).FromMaybe(0);
        Local<String> whole = argv[0].As<String>();
        std::string prefix;
        if (units <= static_cast<uint32_t>(whole->Length())) {
            std::vector<uint16_t> buffer(units);
            if (units > 0) frogjs::WriteTwoByte(isolate, whole, buffer.data());
            Local<String> head = String::NewFromTwoByte(isolate, buffer.data(), NewStringType::kNormal, static_cast<int>(units)).ToLocalChecked();
            prefix = ToUtf8(isolate, head);
        }
        *word_start = prefix.size() <= line.size() ? prefix.size() : line.size();
        if (hits->IsArray()) {
            Local<Array> list = hits.As<Array>();
            for (uint32_t i = 0; i < list->Length(); i++) candidates.push_back(ToUtf8(isolate, list->Get(context, i).ToLocalChecked()));
        }
        return candidates;
    });
}

// feed(bytes): [[type, line], ...] for the lines, Ctrl+D and Ctrl+C in them
void Feed(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    EditorHolder* holder = Holder(args);
    if (holder == nullptr || !args[0]->IsArrayBufferView()) return;
    Local<ArrayBufferView> view = args[0].As<ArrayBufferView>();
    std::vector<unsigned char> bytes(view->ByteLength());
    if (!bytes.empty()) view->CopyContents(bytes.data(), bytes.size());
    Local<Array> events = Array::New(isolate);
    uint32_t count = 0;
    for (unsigned char byte : bytes) {
        std::string line;
        frogjs::LineEditor::Event event = holder->editor->Feed(byte, &line);
        const char* type = nullptr;
        switch (event) {
            case frogjs::LineEditor::Event::kLine: type = "line"; break;
            case frogjs::LineEditor::Event::kEof: type = "eof"; break;
            case frogjs::LineEditor::Event::kInterrupt: type = "interrupt"; break;
            case frogjs::LineEditor::Event::kNone: break;
        }
        if (type == nullptr) continue;
        Local<Value> pair[2] = {String::NewFromUtf8(isolate, type).ToLocalChecked(),
                                String::NewFromUtf8(isolate, line.data(), NewStringType::kNormal, static_cast<int>(line.size())).ToLocalChecked()};
        events->Set(context, count++, Array::New(isolate, pair, 2)).Check();
        // After a line the editor waits for prompt() to start the next one
        if (event == frogjs::LineEditor::Event::kLine || event == frogjs::LineEditor::Event::kEof) break;
    }
    args.GetReturnValue().Set(events);
}

void Destroy(const FunctionCallbackInfo<Value>& args) {
    auto* holder = frogjs::ExternalValue<EditorHolder>(args.Data());
    if (holder == nullptr) return;
    holder->editor.reset();
    holder->completer.Reset();
}

// createEditor(fd): a line editor on a terminal
void CreateEditor(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    int fd = args[0]->Int32Value(context).FromMaybe(0);
    Local<Object> wrapper = Object::New(isolate);
    auto* holder = new EditorHolder();
    holder->editor = std::make_unique<frogjs::LineEditor>(fd);
    Local<Value> data = frogjs::NewExternal(isolate, holder);
    holder->wrapper.Reset(isolate, wrapper);
    holder->wrapper.SetWeak(holder, [](const WeakCallbackInfo<EditorHolder>& info) {
        EditorHolder* collected = info.GetParameter();
        collected->completer.Reset();
        collected->wrapper.Reset();
        delete collected;
    }, WeakCallbackType::kParameter);
    struct { const char* name; FunctionCallback callback; } methods[] = {
        {"start", Start}, {"active", Active}, {"stop", Stop}, {"setPrompt", SetPrompt}, {"addHistory", AddHistory},
        {"line", Line}, {"cursor", Cursor}, {"setCompleter", SetCompleter}, {"feed", Feed}, {"destroy", Destroy},
    };
    for (const auto& method : methods) {
        Local<Function> function = Function::New(context, method.callback, data).ToLocalChecked();
        wrapper->Set(context, String::NewFromUtf8(isolate, method.name).ToLocalChecked(), function).Check();
    }
    args.GetReturnValue().Set(wrapper);
}

}  // namespace

// Set up readline and readline/promises (requires SetupEvents, SetupStringDecoder and SetupTTY first)
void SetupReadline(Isolate* isolate, Local<Context> context) {
    Local<Object> native = Object::New(isolate);
    frogjs::SetFunction(isolate, context, native, "createEditor", CreateEditor);
    Local<Value> string_decoder = frogjs::GetBuiltin(isolate, context, "string_decoder");
    Local<Value> argv[6] = {
        native, frogjs::GetBuiltin(isolate, context, "events"), frogjs::GetBuiltin(isolate, context, "events"),
        string_decoder.As<Object>()->Get(context, String::NewFromUtf8Literal(isolate, "StringDecoder")).ToLocalChecked(),
        frogjs::GetBuiltin(isolate, context, "tty"), frogjs::GetBuiltin(isolate, context, "process"),
    };
    Local<Value> result;
    if (!frogjs::RunJsModule(isolate, context, "readline", kReadlineSource, 6, argv).ToLocal(&result)) return;
    Local<Object> modules = result.As<Object>();
    frogjs::RegisterBuiltin(isolate, context, "readline",
                            modules->Get(context, String::NewFromUtf8Literal(isolate, "readline")).ToLocalChecked());
    frogjs::RegisterBuiltin(isolate, context, "readline/promises",
                            modules->Get(context, String::NewFromUtf8Literal(isolate, "promises")).ToLocalChecked());
}
