#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// The string_decoder module: decode a byte stream to text without splitting
// a multibyte character between chunks. The algorithm is the one Node.js
// used in JavaScript (and that the string_decoder package still ships).
static const char* kStringDecoderSource = R"JS(
(function () {
'use strict';

function normalizeEncoding(encoding) {
  const name = String(encoding === undefined || encoding === null || encoding === '' ? 'utf8' : encoding).toLowerCase();
  switch (name) {
    case 'utf8': case 'utf-8': return 'utf8';
    case 'ucs2': case 'ucs-2': case 'utf16le': case 'utf-16le': return 'utf16le';
    case 'latin1': case 'binary': return 'latin1';
    case 'base64': case 'base64url': case 'ascii': case 'hex': return name;
    default: {
      const err = new TypeError(`Unknown encoding: ${encoding}`);
      err.code = 'ERR_UNKNOWN_ENCODING';
      throw err;
    }
  }
}

// How many bytes a UTF-8 sequence starting with byte has (1, 2, 3, 4), 0 for a
// continuation byte, -1 for an invalid one
function utf8CheckByte(byte) {
  if (byte <= 0x7F) return 0;
  if (byte >> 5 === 0x06) return 2;
  if (byte >> 4 === 0x0E) return 3;
  if (byte >> 3 === 0x1E) return 4;
  return byte >> 6 === 0x02 ? -1 : -2;
}

class StringDecoder {
  constructor(encoding) {
    this.encoding = normalizeEncoding(encoding);
    let size;
    switch (this.encoding) {
      case 'utf16le':
        this.text = utf16Text;
        this.end = utf16End;
        size = 4;
        break;
      case 'utf8':
        this.fillLast = utf8FillLast;
        size = 4;
        break;
      case 'base64':
      case 'base64url':
        this.text = base64Text;
        this.end = base64End;
        size = 3;
        break;
      default:
        this.write = simpleWrite;
        this.end = simpleEnd;
        return;
    }
    this.lastNeed = 0;
    this.lastTotal = 0;
    this.lastChar = Buffer.allocUnsafe(size);
  }

  write(buf) {
    if (typeof buf === 'string') return buf;
    buf = toBuffer(buf);
    if (buf.length === 0) return '';
    let r;
    let i;
    if (this.lastNeed) {
      r = this.fillLast(buf);
      if (r === undefined) return '';
      i = this.lastNeed;
      this.lastNeed = 0;
    } else {
      i = 0;
    }
    if (i < buf.length) return r ? r + this.text(buf, i) : this.text(buf, i);
    return r || '';
  }

  // The rest, and a fresh start: the decoder can be used again
  end(buf) {
    const r = buf && buf.length ? this.write(buf) : '';
    if (this.lastNeed) {
      this.lastNeed = 0;
      return r + '\ufffd';
    }
    return r;
  }

  // The complete characters of buf from i, keeping an incomplete last one
  text(buf, i) {
    const total = utf8CheckIncomplete(this, buf, i);
    if (!this.lastNeed) return buf.toString('utf8', i);
    this.lastTotal = total;
    const end = buf.length - (total - this.lastNeed);
    buf.copy(this.lastChar, 0, end);
    return buf.toString('utf8', i, end);
  }

  fillLast(buf) {
    if (this.lastNeed <= buf.length) {
      buf.copy(this.lastChar, this.lastTotal - this.lastNeed, 0, this.lastNeed);
      return this.lastChar.toString(this.encoding, 0, this.lastTotal);
    }
    buf.copy(this.lastChar, this.lastTotal - this.lastNeed, 0, buf.length);
    this.lastNeed -= buf.length;
    return undefined;
  }
}

function toBuffer(buf) {
  if (Buffer.isBuffer(buf)) return buf;
  if (ArrayBuffer.isView(buf)) return Buffer.from(buf.buffer, buf.byteOffset, buf.byteLength);
  const err = new TypeError(`The "buf" argument must be an instance of Buffer, TypedArray, or DataView. Received ${buf === null ? 'null' : typeof buf}`);
  err.code = 'ERR_INVALID_ARG_TYPE';
  throw err;
}

// How long the incomplete UTF-8 character at the end of buf is (sets lastNeed)
function utf8CheckIncomplete(self, buf, i) {
  let j = buf.length - 1;
  if (j < i) return 0;
  let nb = utf8CheckByte(buf[j]);
  if (nb >= 0) {
    if (nb > 0) self.lastNeed = nb - 1;
    return nb;
  }
  if (--j < i || nb === -2) return 0;
  nb = utf8CheckByte(buf[j]);
  if (nb >= 0) {
    if (nb > 0) self.lastNeed = nb - 2;
    return nb;
  }
  if (--j < i || nb === -2) return 0;
  nb = utf8CheckByte(buf[j]);
  if (nb >= 0) {
    if (nb > 0) {
      if (nb === 2) nb = 0;
      else self.lastNeed = nb - 3;
    }
    return nb;
  }
  return 0;
}

// Continuation bytes the last character needs; invalid ones become U+FFFD
function utf8CheckExtraBytes(self, buf) {
  if ((buf[0] & 0xC0) !== 0x80) {
    self.lastNeed = 0;
    return '\ufffd';
  }
  if (self.lastNeed > 1 && buf.length > 1) {
    if ((buf[1] & 0xC0) !== 0x80) {
      self.lastNeed = 1;
      return '\ufffd';
    }
    if (self.lastNeed > 2 && buf.length > 2) {
      if ((buf[2] & 0xC0) !== 0x80) {
        self.lastNeed = 2;
        return '\ufffd';
      }
    }
  }
  return undefined;
}

function utf8FillLast(buf) {
  const p = this.lastTotal - this.lastNeed;
  const r = utf8CheckExtraBytes(this, buf);
  if (r !== undefined) return r;
  if (this.lastNeed <= buf.length) {
    buf.copy(this.lastChar, p, 0, this.lastNeed);
    return this.lastChar.toString(this.encoding, 0, this.lastTotal);
  }
  buf.copy(this.lastChar, p, 0, buf.length);
  this.lastNeed -= buf.length;
  return undefined;
}

// UTF-16LE: whole code units, and surrogate pairs together
function utf16Text(buf, i) {
  if ((buf.length - i) % 2 === 0) {
    const r = buf.toString('utf16le', i);
    if (r) {
      const c = r.charCodeAt(r.length - 1);
      if (c >= 0xD800 && c <= 0xDBFF) {
        this.lastNeed = 2;
        this.lastTotal = 4;
        this.lastChar[0] = buf[buf.length - 2];
        this.lastChar[1] = buf[buf.length - 1];
        return r.slice(0, -1);
      }
    }
    return r;
  }
  this.lastNeed = 1;
  this.lastTotal = 2;
  this.lastChar[0] = buf[buf.length - 1];
  return buf.toString('utf16le', i, buf.length - 1);
}

function utf16End(buf) {
  const r = buf && buf.length ? this.write(buf) : '';
  if (this.lastNeed) {
    const end = this.lastTotal - this.lastNeed;
    this.lastNeed = 0;
    return r + this.lastChar.toString('utf16le', 0, end);
  }
  return r;
}

// base64: whole groups of three bytes
function base64Text(buf, i) {
  const n = (buf.length - i) % 3;
  if (n === 0) return buf.toString(this.encoding, i);
  this.lastNeed = 3 - n;
  this.lastTotal = 3;
  if (n === 1) {
    this.lastChar[0] = buf[buf.length - 1];
  } else {
    this.lastChar[0] = buf[buf.length - 2];
    this.lastChar[1] = buf[buf.length - 1];
  }
  return buf.toString(this.encoding, i, buf.length - n);
}

function base64End(buf) {
  const r = buf && buf.length ? this.write(buf) : '';
  if (this.lastNeed) {
    const rest = this.lastChar.toString(this.encoding, 0, 3 - this.lastNeed);
    this.lastNeed = 0;
    return r + rest;
  }
  return r;
}

function simpleWrite(buf) {
  if (typeof buf === 'string') return buf;
  return toBuffer(buf).toString(this.encoding);
}

function simpleEnd(buf) {
  return buf && buf.length ? this.write(buf) : '';
}

return { StringDecoder };
})
)JS";

// Set up the string_decoder module (requires SetupBuffer first)
void SetupStringDecoder(Isolate* isolate, Local<Context> context) {
    Local<Value> module;
    if (!frogjs::RunJsModule(isolate, context, "string_decoder", kStringDecoderSource, 0, nullptr).ToLocal(&module)) return;
    frogjs::RegisterBuiltin(isolate, context, "string_decoder", module);
}
