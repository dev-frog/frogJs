#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <v8.h>
#include "frogjs/js_module.h"

using namespace v8;

// UTS #46 from the ICU that V8 is linked with. ICU's C API is stable, but its
// symbols carry the version (uidna_openUTS46_78) and V8 doesn't ship ICU's
// headers, so the functions are looked up at run time. Without them the
// JavaScript below approximates the mapping.
namespace {

struct UIDNAInfo {
    int16_t size;
    int8_t isTransitionalDifferent;
    int8_t reservedB3;
    uint32_t errors;
    int32_t reservedI2;
    int32_t reservedI3;
};

using OpenUTS46 = void* (*)(uint32_t options, int* error);
using NameToUTF8 = int32_t (*)(const void* idna, const char* name, int32_t length, char* dest, int32_t capacity,
                               UIDNAInfo* info, int* error);

struct Idna {
    void* handle = nullptr;
    NameToUTF8 to_ascii = nullptr;
    NameToUTF8 to_unicode = nullptr;
};

// The URL Standard's settings: nontransitional, CheckBidi and CheckJoiners,
// but not CheckHyphens or the DNS length limits
const uint32_t kOptions = 0x4 | 0x8 | 0x10 | 0x20;  // CHECK_BIDI | CHECK_CONTEXTJ | NONTRANSITIONAL_TO_ASCII/UNICODE
const uint32_t kIgnoredErrors = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20;  // empty label, lengths, hyphens

void* FindSymbol(const std::string& name) {
    if (void* symbol = dlsym(RTLD_DEFAULT, name.c_str())) return symbol;
    for (int version = 99; version >= 50; version--) {
        if (void* symbol = dlsym(RTLD_DEFAULT, (name + "_" + std::to_string(version)).c_str())) return symbol;
    }
    return nullptr;
}

const Idna& GetIdna() {
    static Idna idna = [] {
        Idna result;
        auto open = reinterpret_cast<OpenUTS46>(FindSymbol("uidna_openUTS46"));
        auto to_ascii = reinterpret_cast<NameToUTF8>(FindSymbol("uidna_nameToASCII_UTF8"));
        auto to_unicode = reinterpret_cast<NameToUTF8>(FindSymbol("uidna_nameToUnicodeUTF8"));
        if (open == nullptr || to_ascii == nullptr || to_unicode == nullptr) return result;
        int error = 0;
        void* handle = open(kOptions, &error);
        if (handle == nullptr || error > 0) return result;
        result.handle = handle;
        result.to_ascii = to_ascii;
        result.to_unicode = to_unicode;
        return result;
    }();
    return idna;
}

// toASCII(domain) / toUnicode(domain): the result, or null on an error
void Convert(const FunctionCallbackInfo<Value>& args, bool ascii) {
    Isolate* isolate = args.GetIsolate();
    const Idna& idna = GetIdna();
    String::Utf8Value input(isolate, args[0]);
    if (*input == nullptr) return;
    std::string out(input.length() * 4 + 64, '\0');
    UIDNAInfo info = {sizeof(UIDNAInfo), 0, 0, 0, 0, 0};
    int error = 0;
    NameToUTF8 convert = ascii ? idna.to_ascii : idna.to_unicode;
    int32_t length = convert(idna.handle, *input, input.length(), out.data(), static_cast<int32_t>(out.size()), &info, &error);
    if (error > 0 || length < 0 || length > static_cast<int32_t>(out.size())) {
        args.GetReturnValue().SetNull();
        return;
    }
    if (ascii && (info.errors & ~kIgnoredErrors) != 0) {
        args.GetReturnValue().SetNull();
        return;
    }
    args.GetReturnValue().Set(String::NewFromUtf8(isolate, out.data(), NewStringType::kNormal, length).ToLocalChecked());
}

void ToASCII(const FunctionCallbackInfo<Value>& args) { Convert(args, true); }
void ToUnicode(const FunctionCallbackInfo<Value>& args) { Convert(args, false); }

}  // namespace

// URL and URLSearchParams, following the WHATWG URL Standard's parser, and the
// url module. Hosts go through a domain-to-ASCII step that lowercases,
// applies NFKC and encodes non-ASCII labels with Punycode: close to UTS #46
// for real names, without its full mapping tables.
static const char* kUrlSource = R"JS(
(function (path, util, querystring, idna) {
'use strict';
/* eslint-disable no-control-regex -- the URL Standard is written in terms of C0 control code points */

const inspectCustom = Symbol.for('nodejs.util.inspect.custom');

function codeError(Base, code, message, extra) {
  const err = new Base(message);
  err.code = code;
  if (extra) Object.assign(err, extra);
  return err;
}

function invalidArgType(name, expected, value) {
  let shown;
  if (value === null || value === undefined) shown = String(value);
  else if (typeof value === 'function') shown = `function ${value.name}`;
  else if (typeof value === 'object') shown = value.constructor && value.constructor.name ? `an instance of ${value.constructor.name}` : util.inspect(value, { depth: -1 });
  else shown = `type ${typeof value} (${util.inspect(value)})`;
  return codeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "${name}" argument must be ${expected}. Received ${shown}`);
}

// ---- Percent-encoding ----

const inC0 = (c) => c < 0x20 || c > 0x7e;
const inFragment = (c) => inC0(c) || c === 0x20 || c === 0x22 || c === 0x3c || c === 0x3e || c === 0x60;
const inQuery = (c) => inC0(c) || c === 0x20 || c === 0x22 || c === 0x23 || c === 0x3c || c === 0x3e;
const inSpecialQuery = (c) => inQuery(c) || c === 0x27;
const inPath = (c) => inQuery(c) || c === 0x3f || c === 0x5e || c === 0x60 || c === 0x7b || c === 0x7d;
const inUserinfo = (c) => inPath(c) || c === 0x2f || c === 0x3a || c === 0x3b || c === 0x3d || c === 0x40 ||
  (c >= 0x5b && c <= 0x5d) || c === 0x7c;

const hex = [];
for (let i = 0; i < 256; i++) hex.push('%' + (i < 16 ? '0' : '') + i.toString(16).toUpperCase());

function utf8Bytes(cp) {
  if (cp < 0x80) return [cp];
  if (cp < 0x800) return [0xc0 | (cp >> 6), 0x80 | (cp & 0x3f)];
  if (cp < 0x10000) return [0xe0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f)];
  return [0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f), 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f)];
}

// One code point (as a string), encoded if it is in the set
function encodeCodePoint(ch, inSet) {
  const cp = ch.codePointAt(0);
  if (!inSet(cp)) return ch;
  let out = '';
  for (const byte of utf8Bytes(cp)) out += hex[byte];
  return out;
}

function encodeString(text, inSet) {
  let out = '';
  for (const ch of text) out += encodeCodePoint(ch, inSet);
  return out;
}

const isHexDigit = (ch) => ch !== undefined && /^[0-9A-Fa-f]$/.test(ch);

// Percent-decode a string into bytes, then read them as UTF-8
function percentDecodeBytes(text) {
  const utf8 = new TextEncoder().encode(text);
  const out = new Uint8Array(utf8.length);
  let length = 0;
  for (let i = 0; i < utf8.length; i++) {
    const byte = utf8[i];
    if (byte === 0x25 && i + 2 < utf8.length && isHexDigit(String.fromCharCode(utf8[i + 1])) && isHexDigit(String.fromCharCode(utf8[i + 2]))) {
      out[length++] = parseInt(String.fromCharCode(utf8[i + 1], utf8[i + 2]), 16);
      i += 2;
    } else {
      out[length++] = byte;
    }
  }
  return out.subarray(0, length);
}

const utf8Decoder = new TextDecoder('utf-8', { ignoreBOM: true });
const percentDecode = (text) => utf8Decoder.decode(percentDecodeBytes(text));

// ---- Punycode (RFC 3492) ----

const punycode = (() => {
  const base = 36, tMin = 1, tMax = 26, skew = 38, damp = 700, initialBias = 72, initialN = 128;
  const digit = (d) => d + 22 + 75 * (d < 26);
  function adapt(delta, numPoints, firstTime) {
    let k = 0;
    delta = firstTime ? Math.floor(delta / damp) : delta >> 1;
    delta += Math.floor(delta / numPoints);
    for (; delta > ((base - tMin) * tMax) >> 1; k += base) delta = Math.floor(delta / (base - tMin));
    return Math.floor(k + ((base - tMin + 1) * delta) / (delta + skew));
  }
  function encode(input) {
    const cps = Array.from(input, (ch) => ch.codePointAt(0));
    let n = initialN, delta = 0, bias = initialBias;
    let output = cps.filter((c) => c < 0x80).map((c) => String.fromCharCode(c)).join('');
    const basicLength = output.length;
    let handled = basicLength;
    if (basicLength > 0) output += '-';
    while (handled < cps.length) {
      let m = Infinity;
      for (const c of cps) if (c >= n && c < m) m = c;
      if ((m - n) * (handled + 1) > 0x7fffffff - delta) return null;
      delta += (m - n) * (handled + 1);
      n = m;
      for (const c of cps) {
        if (c < n && ++delta > 0x7fffffff) return null;
        if (c === n) {
          let q = delta;
          for (let k = base; ; k += base) {
            const t = k <= bias ? tMin : k >= bias + tMax ? tMax : k - bias;
            if (q < t) break;
            output += String.fromCharCode(digit(t + ((q - t) % (base - t))));
            q = Math.floor((q - t) / (base - t));
          }
          output += String.fromCharCode(digit(q));
          bias = adapt(delta, handled + 1, handled === basicLength);
          delta = 0;
          handled++;
        }
      }
      delta++;
      n++;
    }
    return output;
  }
  function decode(input) {
    const output = [];
    let n = initialN, i = 0, bias = initialBias;
    let basic = input.lastIndexOf('-');
    if (basic < 0) basic = 0;
    for (let j = 0; j < basic; j++) {
      if (input.charCodeAt(j) >= 0x80) return null;
      output.push(input.charCodeAt(j));
    }
    for (let index = basic > 0 ? basic + 1 : 0; index < input.length;) {
      const oldi = i;
      for (let w = 1, k = base; ; k += base) {
        if (index >= input.length) return null;
        const code = input.charCodeAt(index++);
        const d = code - 48 < 10 ? code - 22 : code - 65 < 26 ? code - 65 : code - 97 < 26 ? code - 97 : base;
        if (d >= base || d > Math.floor((0x7fffffff - i) / w)) return null;
        i += d * w;
        const t = k <= bias ? tMin : k >= bias + tMax ? tMax : k - bias;
        if (d < t) break;
        if (w > Math.floor(0x7fffffff / (base - t))) return null;
        w *= base - t;
      }
      const out = output.length + 1;
      bias = adapt(i - oldi, out, oldi === 0);
      if (Math.floor(i / out) > 0x7fffffff - n) return null;
      n += Math.floor(i / out);
      i %= out;
      if (n > 0x10ffff) return null;
      output.splice(i++, 0, n);
    }
    return String.fromCodePoint(...output);
  }
  return { encode, decode };
})();

// ---- Hosts ----

const forbiddenHost = /[\u0000\u0009\u000A\u000D #/:<>?@[\\\]^|]/;
const forbiddenDomain = /[\u0000- #%/:<>?@[\\\]^|\u007F]/;

// Code points UTS #46 ignores, and ones it disallows outright
// Each class is a list of single code points, not characters to combine
// eslint-disable-next-line no-misleading-character-class
const idnaIgnored = /[\u00AD\u034F\u180B-\u180D\u180F\u200B\u2060\u2064\uFE00-\uFE0F\uFEFF\u{1BCA0}-\u{1BCA3}\u{E0100}-\u{E01EF}]/gu;
// eslint-disable-next-line no-misleading-character-class
const idnaDisallowed = /[\u200C\u200D\uFFFD\uFDD0-\uFDEF\uFFFE\uFFFF]|[\u{1FFFE}\u{1FFFF}\u{2FFFE}\u{2FFFF}\u{EFFFE}\u{EFFFF}\u{10FFFE}\u{10FFFF}]/u;
const idnaDots = /[\u3002\uFF0E\uFF61]/g;

// Without ICU: lowercase, NFKC and Punycode, close to UTS #46 for real names
function approximateToASCII(domain) {
  let mapped = '';
  for (const ch of domain.replace(idnaIgnored, '').replace(idnaDots, '.')) {
    const m = ch === 'Σ' ? 'σ' : ch.normalize('NFKC');
    if (m.includes('.') && ch !== '.') return null;
    mapped += m;
  }
  mapped = mapped.toLowerCase().normalize('NFC');
  if (idnaDisallowed.test(mapped)) return null;
  const labels = mapped.split('.');
  for (let i = 0; i < labels.length; i++) {
    const label = labels[i];
    if (/^\p{M}/u.test(label)) return null;
    if (label.startsWith('xn--')) {
      if (/[^\x00-\x7f]/.test(label)) return null;
      const decoded = punycode.decode(label.slice(4));
      if (decoded === null || (decoded !== '' && /^[\x00-\x7f]*$/.test(decoded))) return null;
    } else if (/[^\x00-\x7f]/.test(label)) {
      const encoded = punycode.encode(label);
      if (encoded === null) return null;
      labels[i] = 'xn--' + encoded;
    }
  }
  return labels.join('.');
}

function domainToASCIIInternal(domain) {
  const result = idna ? idna.toASCII(domain) : approximateToASCII(domain);
  if (result === null || result === undefined || result === '' || forbiddenDomain.test(result)) return null;
  return result;
}

function domainToUnicodeInternal(domain) {
  if (idna) {
    const result = idna.toUnicode(domain);
    if (typeof result === 'string') return result;
  }
  return domain.split('.').map((label) => {
    const lower = label.toLowerCase();
    if (!lower.startsWith('xn--')) return lower;
    const decoded = punycode.decode(lower.slice(4));
    return decoded === null ? lower : decoded;
  }).join('.');
}

function parseIPv4Number(text) {
  if (text === '') return null;
  let radix = 10;
  if (text.length >= 2 && (text.startsWith('0x') || text.startsWith('0X'))) {
    radix = 16;
    text = text.slice(2);
  } else if (text.length >= 2 && text[0] === '0') {
    radix = 8;
    text = text.slice(1);
  }
  if (text === '') return 0;
  const valid = radix === 10 ? /^[0-9]+$/ : radix === 16 ? /^[0-9A-Fa-f]+$/ : /^[0-7]+$/;
  if (!valid.test(text)) return null;
  return parseInt(text, radix);
}

function endsInANumber(text) {
  const parts = text.split('.');
  if (parts[parts.length - 1] === '') {
    if (parts.length === 1) return false;
    parts.pop();
  }
  const last = parts[parts.length - 1];
  if (last !== '' && /^[0-9]+$/.test(last)) return true;
  return parseIPv4Number(last) !== null;
}

// The serialized address, or null
function parseIPv4(text) {
  const parts = text.split('.');
  if (parts[parts.length - 1] === '' && parts.length > 1) parts.pop();
  if (parts.length > 4) return null;
  const numbers = [];
  for (const part of parts) {
    const number = parseIPv4Number(part);
    if (number === null) return null;
    numbers.push(number);
  }
  for (let i = 0; i < numbers.length - 1; i++) if (numbers[i] > 255) return null;
  if (numbers[numbers.length - 1] >= 256 ** (5 - numbers.length)) return null;
  let address = numbers[numbers.length - 1];
  for (let i = 0; i < numbers.length - 1; i++) address += numbers[i] * 256 ** (3 - i);
  const octets = [];
  for (let i = 0; i < 4; i++) {
    octets.unshift(address % 256);
    address = Math.floor(address / 256);
  }
  return octets.join('.');
}

// Eight 16-bit pieces, or null
function parseIPv6(input) {
  const address = [0, 0, 0, 0, 0, 0, 0, 0];
  const cps = Array.from(input);
  let pieceIndex = 0;
  let compress = null;
  let pointer = 0;
  const at = () => cps[pointer];
  const isDigit = (ch) => ch !== undefined && ch >= '0' && ch <= '9';
  if (at() === ':') {
    if (cps[pointer + 1] !== ':') return null;
    pointer += 2;
    pieceIndex++;
    compress = pieceIndex;
  }
  while (at() !== undefined) {
    if (pieceIndex === 8) return null;
    if (at() === ':') {
      if (compress !== null) return null;
      pointer++;
      pieceIndex++;
      compress = pieceIndex;
      continue;
    }
    let value = 0;
    let length = 0;
    while (length < 4 && isHexDigit(at())) {
      value = value * 16 + parseInt(at(), 16);
      pointer++;
      length++;
    }
    if (at() === '.') {
      if (length === 0) return null;
      pointer -= length;
      if (pieceIndex > 6) return null;
      let numbersSeen = 0;
      while (at() !== undefined) {
        let piece = null;
        if (numbersSeen > 0) {
          if (at() === '.' && numbersSeen < 4) pointer++;
          else return null;
        }
        if (!isDigit(at())) return null;
        while (isDigit(at())) {
          const number = at().charCodeAt(0) - 48;
          if (piece === null) piece = number;
          else if (piece === 0) return null;
          else piece = piece * 10 + number;
          if (piece > 255) return null;
          pointer++;
        }
        address[pieceIndex] = address[pieceIndex] * 0x100 + piece;
        numbersSeen++;
        if (numbersSeen === 2 || numbersSeen === 4) pieceIndex++;
      }
      if (numbersSeen !== 4) return null;
      break;
    } else if (at() === ':') {
      pointer++;
      if (at() === undefined) return null;
    } else if (at() !== undefined) {
      return null;
    }
    address[pieceIndex] = value;
    pieceIndex++;
  }
  if (compress !== null) {
    let swaps = pieceIndex - compress;
    pieceIndex = 7;
    while (pieceIndex !== 0 && swaps > 0) {
      const other = compress + swaps - 1;
      [address[pieceIndex], address[other]] = [address[other], address[pieceIndex]];
      pieceIndex--;
      swaps--;
    }
  } else if (pieceIndex !== 8) {
    return null;
  }
  return address;
}

function serializeIPv6(address) {
  // The first longest run of two or more zero pieces becomes "::"
  let bestStart = -1, bestLength = 1;
  for (let i = 0; i < 8;) {
    if (address[i] !== 0) { i++; continue; }
    let j = i;
    while (j < 8 && address[j] === 0) j++;
    if (j - i > bestLength) { bestStart = i; bestLength = j - i; }
    i = j;
  }
  let out = '';
  let ignore0 = false;
  for (let i = 0; i < 8; i++) {
    if (ignore0 && address[i] === 0) continue;
    ignore0 = false;
    if (i === bestStart) {
      out += i === 0 ? '::' : ':';
      ignore0 = true;
      continue;
    }
    out += address[i].toString(16);
    if (i !== 7) out += ':';
  }
  return out;
}

// The serialized host, or null
function parseHost(input, isOpaque) {
  if (input.startsWith('[')) {
    if (!input.endsWith(']')) return null;
    const address = parseIPv6(input.slice(1, -1));
    return address === null ? null : `[${serializeIPv6(address)}]`;
  }
  if (isOpaque) {
    if (forbiddenHost.test(input)) return null;
    return encodeString(input, inC0);
  }
  const domain = percentDecode(input);
  const ascii = domainToASCIIInternal(domain);
  if (ascii === null) return null;
  if (endsInANumber(ascii)) return parseIPv4(ascii);
  return ascii;
}

// ---- The URL record and its parser ----

const specialPorts = { __proto__: null, 'ftp': 21, 'file': null, 'http': 80, 'https': 443, 'ws': 80, 'wss': 443 };
const isSpecialScheme = (scheme) => scheme in specialPorts;
const defaultPort = (scheme) => (scheme in specialPorts ? specialPorts[scheme] : null);
const isAlpha = (ch) => ch !== undefined && /^[A-Za-z]$/.test(ch);
const isWindowsDriveLetter = (text) => text.length === 2 && isAlpha(text[0]) && (text[1] === ':' || text[1] === '|');
const isNormalizedDriveLetter = (text) => text.length === 2 && isAlpha(text[0]) && text[1] === ':';
const isSingleDot = (text) => text === '.' || text.toLowerCase() === '%2e';
const isDoubleDot = (text) => {
  const lower = text.toLowerCase();
  return lower === '..' || lower === '.%2e' || lower === '%2e.' || lower === '%2e%2e';
};

function startsWithDriveLetter(cps, pointer) {
  if (cps.length - pointer < 2) return false;
  if (!isAlpha(cps[pointer]) || (cps[pointer + 1] !== ':' && cps[pointer + 1] !== '|')) return false;
  if (cps.length - pointer === 2) return true;
  const third = cps[pointer + 2];
  return third === '/' || third === '\\' || third === '?' || third === '#';
}

const newRecord = () => ({ scheme: '', username: '', password: '', host: null, port: null, path: [], query: null, fragment: null });
const hasOpaquePath = (url) => typeof url.path === 'string';
const includesCredentials = (url) => url.username !== '' || url.password !== '';
const cannotHaveCredentials = (url) => url.host === null || url.host === '' || url.scheme === 'file';

function shortenPath(url) {
  if (url.scheme === 'file' && url.path.length === 1 && isNormalizedDriveLetter(url.path[0])) return;
  url.path.pop();
}

const FAILURE = null;

// The basic URL parser. With a state override it updates url and returns it
// (FAILURE if the value was rejected).
function basicParse(input, base, url, stateOverride) {
  if (!url) {
    url = newRecord();
    input = input.replace(/^[\u0000- ]+|[\u0000- ]+$/g, '');
  }
  input = input.replace(/[\t\n\r]/g, '');
  let state = stateOverride || 'schemeStart';
  let buffer = '';
  let atSignSeen = false, insideBrackets = false, passwordTokenSeen = false;
  const cps = Array.from(input);
  let pointer = 0;

  for (;;) {
    const c = cps[pointer];
    const special = isSpecialScheme(url.scheme);
    switch (state) {
      case 'schemeStart':
        if (isAlpha(c)) {
          buffer += c.toLowerCase();
          state = 'scheme';
        } else if (!stateOverride) {
          state = 'noScheme';
          pointer--;
        } else {
          return FAILURE;
        }
        break;

      case 'scheme':
        if (c !== undefined && /^[A-Za-z0-9+\-.]$/.test(c)) {
          buffer += c.toLowerCase();
        } else if (c === ':') {
          if (stateOverride) {
            if (isSpecialScheme(url.scheme) !== isSpecialScheme(buffer)) return url;
            if ((includesCredentials(url) || url.port !== null) && buffer === 'file') return url;
            if (url.scheme === 'file' && url.host === '') return url;
          }
          url.scheme = buffer;
          if (stateOverride) {
            if (url.port === defaultPort(url.scheme)) url.port = null;
            return url;
          }
          buffer = '';
          if (url.scheme === 'file') {
            state = 'file';
          } else if (isSpecialScheme(url.scheme) && base && base.scheme === url.scheme) {
            state = 'specialRelativeOrAuthority';
          } else if (isSpecialScheme(url.scheme)) {
            state = 'specialAuthoritySlashes';
          } else if (cps[pointer + 1] === '/') {
            state = 'pathOrAuthority';
            pointer++;
          } else {
            url.path = '';
            state = 'opaquePath';
          }
        } else if (!stateOverride) {
          buffer = '';
          state = 'noScheme';
          pointer = -1;
        } else {
          return FAILURE;
        }
        break;

      case 'noScheme':
        if (!base || (hasOpaquePath(base) && c !== '#')) return FAILURE;
        if (hasOpaquePath(base) && c === '#') {
          url.scheme = base.scheme;
          url.path = base.path;
          url.query = base.query;
          url.fragment = '';
          state = 'fragment';
        } else if (base.scheme !== 'file') {
          state = 'relative';
          pointer--;
        } else {
          state = 'file';
          pointer--;
        }
        break;

      case 'specialRelativeOrAuthority':
        if (c === '/' && cps[pointer + 1] === '/') {
          state = 'specialAuthorityIgnoreSlashes';
          pointer++;
        } else {
          state = 'relative';
          pointer--;
        }
        break;

      case 'pathOrAuthority':
        if (c === '/') {
          state = 'authority';
        } else {
          state = 'path';
          pointer--;
        }
        break;

      case 'relative':
        url.scheme = base.scheme;
        if (c === '/' || (isSpecialScheme(url.scheme) && c === '\\')) {
          state = 'relativeSlash';
        } else {
          url.username = base.username;
          url.password = base.password;
          url.host = base.host;
          url.port = base.port;
          url.path = base.path.slice();
          url.query = base.query;
          if (c === '?') {
            url.query = '';
            state = 'query';
          } else if (c === '#') {
            url.fragment = '';
            state = 'fragment';
          } else if (c !== undefined) {
            url.query = null;
            shortenPath(url);
            state = 'path';
            pointer--;
          }
        }
        break;

      case 'relativeSlash':
        if (special && (c === '/' || c === '\\')) {
          state = 'specialAuthorityIgnoreSlashes';
        } else if (c === '/') {
          state = 'authority';
        } else {
          url.username = base.username;
          url.password = base.password;
          url.host = base.host;
          url.port = base.port;
          state = 'path';
          pointer--;
        }
        break;

      case 'specialAuthoritySlashes':
        if (c === '/' && cps[pointer + 1] === '/') {
          state = 'specialAuthorityIgnoreSlashes';
          pointer++;
        } else {
          state = 'specialAuthorityIgnoreSlashes';
          pointer--;
        }
        break;

      case 'specialAuthorityIgnoreSlashes':
        if (c !== '/' && c !== '\\') {
          state = 'authority';
          pointer--;
        }
        break;

      case 'authority':
        if (c === '@') {
          if (atSignSeen) buffer = '%40' + buffer;
          atSignSeen = true;
          for (const ch of buffer) {
            if (ch === ':' && !passwordTokenSeen) {
              passwordTokenSeen = true;
              continue;
            }
            const encoded = encodeCodePoint(ch, inUserinfo);
            if (passwordTokenSeen) url.password += encoded;
            else url.username += encoded;
          }
          buffer = '';
        } else if (c === undefined || c === '/' || c === '?' || c === '#' || (special && c === '\\')) {
          if (atSignSeen && buffer === '') return FAILURE;
          pointer -= Array.from(buffer).length + 1;
          buffer = '';
          state = 'host';
        } else {
          buffer += c;
        }
        break;

      case 'host':
      case 'hostname':
        if (stateOverride && url.scheme === 'file') {
          pointer--;
          state = 'fileHost';
        } else if (c === ':' && !insideBrackets) {
          if (buffer === '') return FAILURE;
          if (stateOverride === 'hostname') return FAILURE;
          const host = parseHost(buffer, !special);
          if (host === null) return FAILURE;
          url.host = host;
          buffer = '';
          state = 'port';
        } else if (c === undefined || c === '/' || c === '?' || c === '#' || (special && c === '\\')) {
          pointer--;
          if (special && buffer === '') return FAILURE;
          if (stateOverride && buffer === '' && (includesCredentials(url) || url.port !== null)) return FAILURE;
          const host = parseHost(buffer, !special);
          if (host === null) return FAILURE;
          url.host = host;
          buffer = '';
          state = 'pathStart';
          if (stateOverride) return url;
        } else {
          if (c === '[') insideBrackets = true;
          if (c === ']') insideBrackets = false;
          buffer += c;
        }
        break;

      case 'port':
        if (c !== undefined && c >= '0' && c <= '9') {
          buffer += c;
        } else if (c === undefined || c === '/' || c === '?' || c === '#' || (special && c === '\\') || stateOverride) {
          if (buffer !== '') {
            const port = parseInt(buffer, 10);
            if (port > 65535) return FAILURE;
            url.port = port === defaultPort(url.scheme) ? null : port;
            buffer = '';
            if (stateOverride) return url;
          }
          if (stateOverride) return FAILURE;
          state = 'pathStart';
          pointer--;
        } else {
          return FAILURE;
        }
        break;

      case 'file':
        url.scheme = 'file';
        url.host = '';
        if (c === '/' || c === '\\') {
          state = 'fileSlash';
        } else if (base && base.scheme === 'file') {
          url.host = base.host;
          url.path = base.path.slice();
          url.query = base.query;
          if (c === '?') {
            url.query = '';
            state = 'query';
          } else if (c === '#') {
            url.fragment = '';
            state = 'fragment';
          } else if (c !== undefined) {
            url.query = null;
            if (!startsWithDriveLetter(cps, pointer)) shortenPath(url);
            else url.path = [];
            state = 'path';
            pointer--;
          }
        } else {
          state = 'path';
          pointer--;
        }
        break;

      case 'fileSlash':
        if (c === '/' || c === '\\') {
          state = 'fileHost';
        } else {
          if (base && base.scheme === 'file') {
            url.host = base.host;
            if (!startsWithDriveLetter(cps, pointer) && base.path.length > 0 && isNormalizedDriveLetter(base.path[0])) {
              url.path.push(base.path[0]);
            }
          }
          state = 'path';
          pointer--;
        }
        break;

      case 'fileHost':
        if (c === undefined || c === '/' || c === '\\' || c === '?' || c === '#') {
          pointer--;
          if (!stateOverride && isWindowsDriveLetter(buffer)) {
            state = 'path';
          } else if (buffer === '') {
            url.host = '';
            if (stateOverride) return url;
            state = 'pathStart';
          } else {
            let host = parseHost(buffer, !special);
            if (host === null) return FAILURE;
            if (host === 'localhost') host = '';
            url.host = host;
            if (stateOverride) return url;
            buffer = '';
            state = 'pathStart';
          }
        } else {
          buffer += c;
        }
        break;

      case 'pathStart':
        if (special) {
          state = 'path';
          if (c !== '/' && c !== '\\') pointer--;
        } else if (!stateOverride && c === '?') {
          url.query = '';
          state = 'query';
        } else if (!stateOverride && c === '#') {
          url.fragment = '';
          state = 'fragment';
        } else if (c !== undefined) {
          state = 'path';
          if (c !== '/') pointer--;
        } else if (stateOverride && url.host === null) {
          url.path.push('');
        }
        break;

      case 'path':
        if (c === undefined || c === '/' || (special && c === '\\') || (!stateOverride && (c === '?' || c === '#'))) {
          const slash = c === '/' || (special && c === '\\');
          if (isDoubleDot(buffer)) {
            shortenPath(url);
            if (!slash) url.path.push('');
          } else if (isSingleDot(buffer) && !slash) {
            url.path.push('');
          } else if (!isSingleDot(buffer)) {
            if (url.scheme === 'file' && url.path.length === 0 && isWindowsDriveLetter(buffer)) {
              buffer = buffer[0] + ':';
            }
            url.path.push(buffer);
          }
          buffer = '';
          if (c === '?') {
            url.query = '';
            state = 'query';
          }
          if (c === '#') {
            url.fragment = '';
            state = 'fragment';
          }
        } else {
          buffer += encodeCodePoint(c, inPath);
        }
        break;

      case 'opaquePath':
        if (c === '?') {
          url.query = '';
          state = 'query';
        } else if (c === '#') {
          url.fragment = '';
          state = 'fragment';
        } else if (c === ' ') {
          const next = cps[pointer + 1];
          url.path += next === '?' || next === '#' ? '%20' : ' ';
        } else if (c !== undefined) {
          url.path += encodeCodePoint(c, inC0);
        }
        break;

      case 'query':
        if ((!stateOverride && c === '#') || c === undefined) {
          url.query += encodeString(buffer, special ? inSpecialQuery : inQuery);
          buffer = '';
          if (c === '#') {
            url.fragment = '';
            state = 'fragment';
          }
        } else {
          buffer += c;
        }
        break;

      case 'fragment':
        if (c !== undefined) url.fragment += encodeCodePoint(c, inFragment);
        break;

      default:
        throw new Error('URL parser: unknown state ' + state);
    }
    if (pointer >= cps.length) break;
    pointer++;
  }
  return url;
}

function serializePath(url) {
  if (hasOpaquePath(url)) return url.path;
  let out = '';
  for (const segment of url.path) out += '/' + segment;
  return out;
}

function serializeURL(url, excludeFragment) {
  let out = url.scheme + ':';
  if (url.host !== null) {
    out += '//';
    if (includesCredentials(url)) {
      out += url.username;
      if (url.password !== '') out += ':' + url.password;
      out += '@';
    }
    out += url.host;
    if (url.port !== null) out += ':' + url.port;
  }
  if (url.host === null && !hasOpaquePath(url) && url.path.length > 1 && url.path[0] === '') out += '/.';
  out += serializePath(url);
  if (url.query !== null) out += '?' + url.query;
  if (!excludeFragment && url.fragment !== null) out += '#' + url.fragment;
  return out;
}

function serializeOrigin(url) {
  if (url.scheme === 'blob') {
    const inner = basicParse(serializePath(url), null);
    if (inner && (inner.scheme === 'http' || inner.scheme === 'https')) return serializeOrigin(inner);
    return 'null';
  }
  if (isSpecialScheme(url.scheme) && url.scheme !== 'file') {
    return url.scheme + '://' + url.host + (url.port !== null ? ':' + url.port : '');
  }
  return 'null';
}

function stripTrailingSpacesFromOpaquePath(url) {
  if (!hasOpaquePath(url) || url.fragment !== null || url.query !== null) return;
  url.path = url.path.replace(/ +$/, '');
}

// ---- application/x-www-form-urlencoded ----

function formEncode(text) {
  let out = '';
  for (const byte of new TextEncoder().encode(text)) {
    if (byte === 0x20) out += '+';
    else if (byte === 0x2a || byte === 0x2d || byte === 0x2e || byte === 0x5f ||
             (byte >= 0x30 && byte <= 0x39) || (byte >= 0x41 && byte <= 0x5a) || (byte >= 0x61 && byte <= 0x7a)) {
      out += String.fromCharCode(byte);
    } else {
      out += hex[byte];
    }
  }
  return out;
}

function formParse(input) {
  const list = [];
  for (const sequence of input.split('&')) {
    if (sequence === '') continue;
    const equals = sequence.indexOf('=');
    const name = equals >= 0 ? sequence.slice(0, equals) : sequence;
    const value = equals >= 0 ? sequence.slice(equals + 1) : '';
    list.push([percentDecode(name.replace(/\+/g, ' ')), percentDecode(value.replace(/\+/g, ' '))]);
  }
  return list;
}

const formSerialize = (list) => list.map(([name, value]) => formEncode(name) + '=' + formEncode(value)).join('&');

// ---- URLSearchParams ----

const toUSV = (value) => String(value).toWellFormed();
const kList = Symbol('list');
const kURL = Symbol('url');
// Internal slots: not enumerable, so inspect() and Object.keys() don't see them
const hide = (object, key, value) => Object.defineProperty(object, key, { value, writable: true, configurable: true });

function missingArgs(...names) {
  const shown = names.map((name) => `"${name}"`).join(' and ');
  return codeError(TypeError, 'ERR_MISSING_ARGS', `The ${shown} argument${names.length > 1 ? 's' : ''} must be specified`);
}

function checkThis(self, Class) {
  if (self === null || typeof self !== 'object' || !(kList in self && Class === URLSearchParams)) {
    throw codeError(TypeError, 'ERR_INVALID_THIS', `Value of "this" must be of type ${Class.name}`);
  }
}

class URLSearchParams {
  constructor(init = undefined) {
    hide(this, kList, []);
    hide(this, kURL, null);
    if (init === undefined || init === null) {
      if (init === null) this[kList] = formParse('null');
      return;
    }
    if (typeof init === 'object' || typeof init === 'function') {
      const method = init[Symbol.iterator];
      if (method !== undefined && method !== null) {
        if (typeof method !== 'function') throw codeError(TypeError, 'ERR_ARG_NOT_ITERABLE', 'Query pairs must be iterable');
        for (const pair of init) {
          if ((typeof pair !== 'object' && typeof pair !== 'function') || pair === null || typeof pair[Symbol.iterator] !== 'function') {
            throw codeError(TypeError, 'ERR_INVALID_TUPLE', 'Each query pair must be an iterable [name, value] tuple');
          }
          const items = Array.from(pair);
          if (items.length !== 2) throw codeError(TypeError, 'ERR_INVALID_TUPLE', 'Each query pair must be an iterable [name, value] tuple');
          this[kList].push([toUSV(items[0]), toUSV(items[1])]);
        }
      } else {
        for (const key of Reflect.ownKeys(init)) {
          if (typeof key !== 'string') continue;
          const descriptor = Reflect.getOwnPropertyDescriptor(init, key);
          if (descriptor && descriptor.enumerable) this[kList].push([toUSV(key), toUSV(init[key])]);
        }
      }
      return;
    }
    let text = toUSV(init);
    if (text.startsWith('?')) text = text.slice(1);
    this[kList] = formParse(text);
  }

  get size() {
    checkThis(this, URLSearchParams);
    return this[kList].length;
  }

  append(name, value) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 2) throw missingArgs('name', 'value');
    this[kList].push([toUSV(name), toUSV(value)]);
    this.#update();
  }

  delete(name, value = undefined) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 1) throw missingArgs('name');
    name = toUSV(name);
    if (value !== undefined) {
      value = toUSV(value);
      this[kList] = this[kList].filter((pair) => pair[0] !== name || pair[1] !== value);
    } else {
      this[kList] = this[kList].filter((pair) => pair[0] !== name);
    }
    this.#update();
  }

  get(name) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 1) throw missingArgs('name');
    name = toUSV(name);
    const pair = this[kList].find((item) => item[0] === name);
    return pair ? pair[1] : null;
  }

  getAll(name) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 1) throw missingArgs('name');
    name = toUSV(name);
    return this[kList].filter((item) => item[0] === name).map((item) => item[1]);
  }

  has(name, value = undefined) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 1) throw missingArgs('name');
    name = toUSV(name);
    if (value !== undefined) {
      value = toUSV(value);
      return this[kList].some((item) => item[0] === name && item[1] === value);
    }
    return this[kList].some((item) => item[0] === name);
  }

  set(name, value) {
    checkThis(this, URLSearchParams);
    if (arguments.length < 2) throw missingArgs('name', 'value');
    name = toUSV(name);
    value = toUSV(value);
    const index = this[kList].findIndex((item) => item[0] === name);
    if (index < 0) {
      this[kList].push([name, value]);
    } else {
      this[kList][index][1] = value;
      this[kList] = this[kList].filter((item, i) => i <= index || item[0] !== name);
    }
    this.#update();
  }

  sort() {
    checkThis(this, URLSearchParams);
    // Stable, by UTF-16 code units
    this[kList] = this[kList]
      .map((pair, index) => [pair, index])
      .sort((a, b) => (a[0][0] < b[0][0] ? -1 : a[0][0] > b[0][0] ? 1 : a[1] - b[1]))
      .map((entry) => entry[0]);
    this.#update();
  }

  forEach(callback, thisArg = undefined) {
    checkThis(this, URLSearchParams);
    if (typeof callback !== 'function') throw invalidArgType('callback', 'of type function', callback);
    for (let i = 0; i < this[kList].length; i++) {
      const [name, value] = this[kList][i];
      callback.call(thisArg, value, name, this);
    }
  }

  *entries() {
    checkThis(this, URLSearchParams);
    for (let i = 0; i < this[kList].length; i++) yield [this[kList][i][0], this[kList][i][1]];
  }

  *keys() {
    checkThis(this, URLSearchParams);
    for (let i = 0; i < this[kList].length; i++) yield this[kList][i][0];
  }

  *values() {
    checkThis(this, URLSearchParams);
    for (let i = 0; i < this[kList].length; i++) yield this[kList][i][1];
  }

  toString() {
    checkThis(this, URLSearchParams);
    return formSerialize(this[kList]);
  }

  #update() {
    const url = this[kURL];
    if (url === null) return;
    const serialized = formSerialize(this[kList]);
    const record = url[kRecord];
    record.query = serialized === '' ? null : serialized;
    if (record.query === null) stripTrailingSpacesFromOpaquePath(record);
  }

  [inspectCustom](depth, options, inspect) {
    if (depth < 0) return '[Object]';
    const show = typeof inspect === 'function' ? inspect : util.inspect;
    const opts = { ...options, depth: options && options.depth != null ? options.depth - 1 : null };
    const items = this[kList].map(([name, value]) => `${show(name, opts)} => ${show(value, opts)}`);
    if (items.length === 0) return 'URLSearchParams {}';
    const oneLine = `URLSearchParams { ${items.join(', ')} }`;
    const width = (options && options.breakLength) || 80;
    if (oneLine.length <= width) return oneLine;
    return `URLSearchParams {\n  ${items.join(',\n  ')} }`;
  }
}
URLSearchParams.prototype[Symbol.iterator] = URLSearchParams.prototype.entries;
Object.defineProperty(URLSearchParams.prototype, Symbol.toStringTag, { value: 'URLSearchParams', configurable: true });

// ---- URL ----

const kRecord = Symbol('record');
const kParams = Symbol('params');

function invalidURL(input, base) {
  const extra = { input: String(input) };
  if (base !== undefined) extra.base = String(base);
  return codeError(TypeError, 'ERR_INVALID_URL', 'Invalid URL', extra);
}

function parseWithBase(input, base) {
  let baseRecord = null;
  if (base !== undefined) {
    baseRecord = basicParse(toUSV(base), null);
    if (baseRecord === FAILURE) return FAILURE;
  }
  return basicParse(toUSV(input), baseRecord);
}

function checkURL(self) {
  if (self === null || typeof self !== 'object' || !(kRecord in self)) {
    throw codeError(TypeError, 'ERR_INVALID_THIS', 'Value of "this" must be of type URL');
  }
}

function attachParams(url) {
  const params = new URLSearchParams(url[kRecord].query || '');
  params[kURL] = url;
  hide(url, kParams, params);
}

class URL {
  constructor(input = undefined, base = undefined) {
    if (arguments.length === 0) throw missingArgs('url');
    const record = parseWithBase(input, base);
    if (record === FAILURE) throw invalidURL(input, base);
    hide(this, kRecord, record);
    attachParams(this);
  }

  static canParse(input = undefined, base = undefined) {
    if (arguments.length === 0) throw missingArgs('url');
    return parseWithBase(input, base) !== FAILURE;
  }

  static parse(input = undefined, base = undefined) {
    if (arguments.length === 0) throw missingArgs('url');
    const record = parseWithBase(input, base);
    if (record === FAILURE) return null;
    const url = Object.create(URL.prototype);
    hide(url, kRecord, record);
    attachParams(url);
    return url;
  }

  get href() {
    checkURL(this);
    return serializeURL(this[kRecord]);
  }
  set href(value) {
    checkURL(this);
    const record = basicParse(toUSV(value), null);
    if (record === FAILURE) throw invalidURL(value);
    this[kRecord] = record;
    this[kParams][kList] = formParse(record.query || '');
  }

  get origin() {
    checkURL(this);
    return serializeOrigin(this[kRecord]);
  }

  get protocol() {
    checkURL(this);
    return this[kRecord].scheme + ':';
  }
  set protocol(value) {
    checkURL(this);
    this.#override(toUSV(value) + ':', 'schemeStart');
  }

  get username() {
    checkURL(this);
    return this[kRecord].username;
  }
  set username(value) {
    checkURL(this);
    if (cannotHaveCredentials(this[kRecord])) return;
    this[kRecord].username = encodeString(toUSV(value), inUserinfo);
  }

  get password() {
    checkURL(this);
    return this[kRecord].password;
  }
  set password(value) {
    checkURL(this);
    if (cannotHaveCredentials(this[kRecord])) return;
    this[kRecord].password = encodeString(toUSV(value), inUserinfo);
  }

  get host() {
    checkURL(this);
    const record = this[kRecord];
    if (record.host === null) return '';
    return record.port === null ? record.host : record.host + ':' + record.port;
  }
  set host(value) {
    checkURL(this);
    if (hasOpaquePath(this[kRecord])) return;
    this.#override(toUSV(value), 'host');
  }

  get hostname() {
    checkURL(this);
    return this[kRecord].host === null ? '' : this[kRecord].host;
  }
  set hostname(value) {
    checkURL(this);
    if (hasOpaquePath(this[kRecord])) return;
    this.#override(toUSV(value), 'hostname');
  }

  get port() {
    checkURL(this);
    return this[kRecord].port === null ? '' : String(this[kRecord].port);
  }
  set port(value) {
    checkURL(this);
    const record = this[kRecord];
    if (cannotHaveCredentials(record)) return;
    value = toUSV(value);
    if (value === '') record.port = null;
    else this.#override(value, 'port');
  }

  get pathname() {
    checkURL(this);
    return serializePath(this[kRecord]);
  }
  set pathname(value) {
    checkURL(this);
    if (hasOpaquePath(this[kRecord])) return;
    this[kRecord].path = [];
    basicParse(toUSV(value), null, this[kRecord], 'pathStart');
  }

  get search() {
    checkURL(this);
    const query = this[kRecord].query;
    return query === null || query === '' ? '' : '?' + query;
  }
  set search(value) {
    checkURL(this);
    const record = this[kRecord];
    value = toUSV(value);
    if (value === '') {
      record.query = null;
      this[kParams][kList] = [];
      stripTrailingSpacesFromOpaquePath(record);
      return;
    }
    const input = value[0] === '?' ? value.slice(1) : value;
    record.query = '';
    basicParse(input, null, record, 'query');
    this[kParams][kList] = formParse(input);
  }

  get searchParams() {
    checkURL(this);
    return this[kParams];
  }

  get hash() {
    checkURL(this);
    const fragment = this[kRecord].fragment;
    return fragment === null || fragment === '' ? '' : '#' + fragment;
  }
  set hash(value) {
    checkURL(this);
    const record = this[kRecord];
    value = toUSV(value);
    if (value === '') {
      record.fragment = null;
      stripTrailingSpacesFromOpaquePath(record);
      return;
    }
    record.fragment = '';
    basicParse(value[0] === '#' ? value.slice(1) : value, null, record, 'fragment');
  }

  toString() {
    checkURL(this);
    return this.href;
  }

  toJSON() {
    checkURL(this);
    return this.href;
  }

  // As in the standard, a rejected value keeps what was set before the
  // parser stopped ("example.com:x" as the host changes the hostname)
  #override(input, state) {
    basicParse(input, null, this[kRecord], state);
  }

  [inspectCustom](depth, options, inspect) {
    const show = typeof inspect === 'function' ? inspect : util.inspect;
    if (depth < 0) return this;
    const fields = {};
    for (const key of ['href', 'origin', 'protocol', 'username', 'password', 'host', 'hostname', 'port',
                       'pathname', 'search', 'searchParams', 'hash']) {
      fields[key] = this[key];
    }
    const shown = show(fields, { ...options, depth: options && options.depth != null ? options.depth : 2 });
    return 'URL ' + shown;
  }
}

for (const key of ['href', 'origin', 'protocol', 'username', 'password', 'host', 'hostname', 'port',
                   'pathname', 'search', 'searchParams', 'hash', 'toString', 'toJSON']) {
  const descriptor = Object.getOwnPropertyDescriptor(URL.prototype, key);
  descriptor.enumerable = true;
  Object.defineProperty(URL.prototype, key, descriptor);
}
for (const key of ['size', 'append', 'delete', 'get', 'getAll', 'has', 'set', 'sort', 'forEach', 'entries', 'keys', 'values', 'toString']) {
  const descriptor = Object.getOwnPropertyDescriptor(URLSearchParams.prototype, key);
  descriptor.enumerable = true;
  Object.defineProperty(URLSearchParams.prototype, key, descriptor);
}
Object.defineProperty(URL.prototype, Symbol.toStringTag, { value: 'URL', configurable: true });

URL.createObjectURL = function createObjectURL() {
  throw codeError(Error, 'ERR_METHOD_NOT_IMPLEMENTED', 'The URL.createObjectURL() method is not implemented');
};
URL.revokeObjectURL = function revokeObjectURL() {
  throw codeError(Error, 'ERR_METHOD_NOT_IMPLEMENTED', 'The URL.revokeObjectURL() method is not implemented');
};

const isURL = (value) => value !== null && typeof value === 'object' && kRecord in value;

// ---- File URLs ----

function fileURLToPath(value) {
  let url;
  if (typeof value === 'string') url = new URL(value);
  else if (isURL(value)) url = value;
  else throw invalidArgType('path', 'of type string or an instance of URL', value);
  if (url.protocol !== 'file:') throw codeError(TypeError, 'ERR_INVALID_URL_SCHEME', 'The URL must be of scheme file');
  if (url.hostname !== '') {
    throw codeError(TypeError, 'ERR_INVALID_FILE_URL_HOST', `File URL host must be "localhost" or empty on ${process.platform}`);
  }
  const pathname = url.pathname;
  if (/%2f/i.test(pathname)) {
    throw codeError(TypeError, 'ERR_INVALID_FILE_URL_PATH', 'File URL path must not include encoded / characters');
  }
  return percentDecode(pathname);
}

function pathToFileURL(filepath) {
  if (typeof filepath !== 'string') throw invalidArgType('path', 'of type string', filepath);
  let resolved = path.resolve(filepath);
  if (filepath.endsWith('/') && !resolved.endsWith('/')) resolved += '/';
  const encoded = resolved.replace(/%/g, '%25').replace(/\\/g, '%5C').replace(/\n/g, '%0A')
    .replace(/\r/g, '%0D').replace(/\t/g, '%09');
  const url = new URL('file://');
  url.pathname = encoded;
  return url;
}

function urlToHttpOptions(url) {
  const options = {
    __proto__: null,
    protocol: url.protocol,
    hostname: typeof url.hostname === 'string' && url.hostname.startsWith('[') ? url.hostname.slice(1, -1) : url.hostname,
    hash: url.hash,
    search: url.search,
    pathname: url.pathname,
    path: `${url.pathname || ''}${url.search || ''}`,
    href: url.href,
  };
  if (url.port !== '') options.port = Number(url.port);
  if (url.username || url.password) {
    options.auth = `${decodeURIComponent(url.username)}:${decodeURIComponent(url.password)}`;
  }
  return options;
}

function domainToASCII(domain) {
  if (arguments.length < 1) throw missingArgs('domain');
  const result = parseHost(toUSV(domain), false);
  return result === null || result.startsWith('[') ? '' : result;
}

function domainToUnicode(domain) {
  if (arguments.length < 1) throw missingArgs('domain');
  const result = parseHost(toUSV(domain), false);
  return result === null || result.startsWith('[') ? '' : domainToUnicodeInternal(result);
}

// ---- The legacy API: url.parse(), url.format(), url.resolve() ----
// A port of Node.js's lib/url.js, quirks included: code written for it
// depends on the exact shape of the result.

const protocolPattern = /^[a-z0-9.+-]+:/i;
const portPattern = /:[0-9]*$/;
const hostPattern = /^\/\/[^@/]+@[^@/]+/;
const simplePathPattern = /^(\/\/?(?!\/)[^?\s]*)(\?[^\s]*)?$/;
const hostnameMaxLen = 255;
const unsafeProtocol = new Set(['javascript', 'javascript:']);
const hostlessProtocol = new Set(['javascript', 'javascript:']);
const slashedProtocol = new Set(['http', 'http:', 'https', 'https:', 'ftp', 'ftp:', 'gopher', 'gopher:',
                                 'file', 'file:', 'ws', 'ws:', 'wss', 'wss:']);
const forbiddenHostChars = /[\0\t\n\r #%/:<>?@[\\\]^|]/;
const forbiddenHostCharsIpv6 = /[\0\t\n\r #%/<>?@\\^|]/;
const autoEscape = { '\t': '%09', '\n': '%0A', '\r': '%0D', ' ': '%20', '"': '%22', "'": '%27', '<': '%3C', '>': '%3E',
                     '\\': '%5C', '^': '%5E', '`': '%60', '{': '%7B', '|': '%7C', '}': '%7D' };

function Url() {
  this.protocol = null;
  this.slashes = null;
  this.auth = null;
  this.host = null;
  this.port = null;
  this.hostname = null;
  this.hash = null;
  this.search = null;
  this.query = null;
  this.pathname = null;
  this.path = null;
  this.href = null;
}

function urlParse(url, parseQueryString, slashesDenoteHost) {
  if (url instanceof Url) return url;
  const result = new Url();
  result.parse(url, parseQueryString, slashesDenoteHost);
  return result;
}

const isIpv6Hostname = (hostname) => hostname.length > 1 && hostname[0] === '[' && hostname[hostname.length - 1] === ']';

Url.prototype.parse = function parse(url, parseQueryString, slashesDenoteHost) {
  if (typeof url !== 'string') throw invalidArgType('url', 'of type string', url);

  // Trim whitespace, and turn backslashes before the query or hash into slashes
  let hasHash = false, hasAt = false, start = -1, end = -1, rest = '', lastPos = 0;
  for (let i = 0, inWs = false, split = false; i < url.length; ++i) {
    const code = url.charCodeAt(i);
    const isWs = code === 32 || code === 9 || code === 13 || code === 10 || code === 12 || code === 160 || code === 65279;
    if (start === -1) {
      if (isWs) continue;
      lastPos = start = i;
    } else if (inWs) {
      if (!isWs) {
        end = -1;
        inWs = false;
      }
    } else if (isWs) {
      end = i;
      inWs = true;
    }
    if (!split) {
      if (code === 64) {
        hasAt = true;
      } else if (code === 35) {
        hasHash = true;
        split = true;
      } else if (code === 63) {
        split = true;
      } else if (code === 92) {
        if (i - lastPos > 0) rest += url.slice(lastPos, i);
        rest += '/';
        lastPos = i + 1;
      }
    } else if (!hasHash && code === 35) {
      hasHash = true;
    }
  }
  if (start !== -1) {
    if (lastPos === start) {
      if (end === -1) rest = start === 0 ? url : url.slice(start);
      else rest = url.slice(start, end);
    } else if (end === -1 && lastPos < url.length) {
      rest += url.slice(lastPos);
    } else if (end !== -1 && lastPos < end) {
      rest += url.slice(lastPos, end);
    }
  }

  if (!slashesDenoteHost && !hasHash && !hasAt) {
    const simplePath = simplePathPattern.exec(rest);
    if (simplePath) {
      this.path = rest;
      this.href = rest;
      this.pathname = simplePath[1];
      if (simplePath[2]) {
        this.search = simplePath[2];
        this.query = parseQueryString ? querystring.parse(this.search.slice(1)) : this.search.slice(1);
      } else if (parseQueryString) {
        this.search = null;
        this.query = { __proto__: null };
      }
      return this;
    }
  }

  let proto = protocolPattern.exec(rest);
  let lowerProto;
  if (proto) {
    proto = proto[0];
    lowerProto = proto.toLowerCase();
    this.protocol = lowerProto;
    rest = rest.slice(proto.length);
  }

  let slashes;
  if (slashesDenoteHost || proto || hostPattern.test(rest)) {
    slashes = rest.charCodeAt(0) === 47 && rest.charCodeAt(1) === 47;
    if (slashes && !(proto && hostlessProtocol.has(lowerProto))) {
      rest = rest.slice(2);
      this.slashes = true;
    }
  }

  if (!hostlessProtocol.has(lowerProto) && (slashes || (proto && !slashedProtocol.has(proto)))) {
    let hostEnd = -1, atSign = -1, nonHost = -1;
    for (let i = 0; i < rest.length; ++i) {
      const ch = rest[i];
      if ('\t\n\r "%\';<>\\^`{|}'.includes(ch)) {
        if (nonHost === -1) nonHost = i;
      } else if (ch === '#' || ch === '/' || ch === '?') {
        if (nonHost === -1) nonHost = i;
        hostEnd = i;
      } else if (ch === '@') {
        atSign = i;
        nonHost = -1;
      }
      if (hostEnd !== -1) break;
    }
    let hostStart = 0;
    if (atSign !== -1) {
      this.auth = decodeURIComponent(rest.slice(0, atSign));
      hostStart = atSign + 1;
    }
    if (nonHost === -1) {
      this.host = rest.slice(hostStart);
      rest = '';
    } else {
      this.host = rest.slice(hostStart, nonHost);
      rest = rest.slice(nonHost);
    }

    this.parseHost();
    if (typeof this.hostname !== 'string') this.hostname = '';
    const hostname = this.hostname;
    const ipv6Hostname = isIpv6Hostname(hostname);
    if (!ipv6Hostname) {
      for (let i = 0; i < hostname.length; ++i) {
        const ch = hostname[i];
        if (ch === '/' || ch === '\\' || ch === '#' || ch === '?' || ch === ':') {
          this.hostname = hostname.slice(0, i);
          rest = `/${hostname.slice(i)}${rest}`;
          break;
        }
      }
    }
    if (this.hostname.length > hostnameMaxLen) this.hostname = '';
    else this.hostname = this.hostname.toLowerCase();

    if (this.hostname !== '') {
      if (ipv6Hostname) {
        if (forbiddenHostCharsIpv6.test(this.hostname)) throw invalidURL(url);
      } else {
        this.hostname = this.hostname.split('.').map((label) =>
          /[^\x00-\x7f]/.test(label) ? 'xn--' + punycode.encode(label.normalize('NFKC').toLowerCase()) : label).join('.');
        if (forbiddenHostChars.test(this.hostname)) throw invalidURL(url);
      }
    }
    const p = this.port ? ':' + this.port : '';
    this.host = (this.hostname || '') + p;
    if (ipv6Hostname) {
      this.hostname = this.hostname.slice(1, -1);
      if (rest[0] !== '/') rest = '/' + rest;
    }
  }

  if (!unsafeProtocol.has(lowerProto)) rest = rest.replace(/[\t\n\r "'<>\\^`{|}]/g, (ch) => autoEscape[ch]);

  let questionIdx = -1, hashIdx = -1;
  for (let i = 0; i < rest.length; ++i) {
    const code = rest.charCodeAt(i);
    if (code === 35) {
      this.hash = rest.slice(i);
      hashIdx = i;
      break;
    } else if (code === 63 && questionIdx === -1) {
      questionIdx = i;
    }
  }
  if (questionIdx !== -1) {
    if (hashIdx === -1) {
      this.search = rest.slice(questionIdx);
      this.query = rest.slice(questionIdx + 1);
    } else {
      this.search = rest.slice(questionIdx, hashIdx);
      this.query = rest.slice(questionIdx + 1, hashIdx);
    }
    if (parseQueryString) this.query = querystring.parse(this.query);
  } else if (parseQueryString) {
    this.search = null;
    this.query = { __proto__: null };
  }

  const useQuestionIdx = questionIdx !== -1 && (hashIdx === -1 || questionIdx < hashIdx);
  const firstIdx = useQuestionIdx ? questionIdx : hashIdx;
  if (firstIdx === -1) {
    if (rest.length > 0) this.pathname = rest;
  } else if (firstIdx > 0) {
    this.pathname = rest.slice(0, firstIdx);
  }
  if (slashedProtocol.has(lowerProto) && this.hostname && !this.pathname) this.pathname = '/';
  if (this.pathname || this.search) this.path = (this.pathname || '') + (this.search || '');
  this.href = this.format();
  return this;
};

Url.prototype.parseHost = function parseHost() {
  let host = this.host;
  let port = portPattern.exec(host);
  if (port) {
    port = port[0];
    if (port !== ':') this.port = port.slice(1);
    host = host.slice(0, host.length - port.length);
  }
  if (host) this.hostname = host;
};

Url.prototype.format = function format() {
  let auth = this.auth || '';
  if (auth) auth = encodeURIComponent(auth).replace(/%3A/gi, ':') + '@';
  let protocol = this.protocol || '';
  let pathname = this.pathname || '';
  let hash = this.hash || '';
  let host = '';
  let query = '';
  if (this.host) {
    host = auth + this.host;
  } else if (this.hostname) {
    host = auth + (this.hostname.includes(':') && !isIpv6Hostname(this.hostname) ? '[' + this.hostname + ']' : this.hostname);
    if (this.port) host += ':' + this.port;
  }
  if (this.query !== null && typeof this.query === 'object') query = querystring.stringify(this.query);
  let search = this.search || (query && '?' + query) || '';
  if (protocol && protocol[protocol.length - 1] !== ':') protocol += ':';
  pathname = pathname.replace(/[#?]/g, (ch) => (ch === '#' ? '%23' : '%3F'));
  if (this.slashes || slashedProtocol.has(protocol)) {
    if (this.slashes || host) {
      if (pathname && pathname[0] !== '/') pathname = '/' + pathname;
      host = '//' + host;
    } else if (protocol.length >= 4 && protocol.slice(0, 4).toLowerCase() === 'file') {
      host = '//';
    }
  }
  search = search.replace(/#/g, '%23');
  if (hash && hash[0] !== '#') hash = '#' + hash;
  if (search && search[0] !== '?') search = '?' + search;
  return protocol + host + pathname + search + hash;
};

Url.prototype.resolve = function resolve(relative) {
  return this.resolveObject(urlParse(relative, false, true)).format();
};

Url.prototype.resolveObject = function resolveObject(relative) {
  if (typeof relative === 'string') {
    const rel = new Url();
    rel.parse(relative, false, true);
    relative = rel;
  }
  const result = new Url();
  for (const key of Object.keys(this)) result[key] = this[key];
  result.hash = relative.hash;
  if (relative.href === '') {
    result.href = result.format();
    return result;
  }

  if (relative.slashes && !relative.protocol) {
    for (const key of Object.keys(relative)) if (key !== 'protocol') result[key] = relative[key];
    if (slashedProtocol.has(result.protocol) && result.hostname && !result.pathname) result.path = result.pathname = '/';
    result.href = result.format();
    return result;
  }

  if (relative.protocol && relative.protocol !== result.protocol) {
    if (!slashedProtocol.has(relative.protocol)) {
      Object.assign(result, relative);
      result.href = result.format();
      return result;
    }
    result.protocol = relative.protocol;
    if (!relative.host && !/^file:?$/.test(relative.protocol) && !hostlessProtocol.has(relative.protocol)) {
      const relPath = (relative.pathname || '').split('/');
      while (relPath.length && !(relative.host = relPath.shift()));
      if (!relative.host) relative.host = '';
      if (!relative.hostname) relative.hostname = '';
      if (relPath[0] !== '') relPath.unshift('');
      if (relPath.length < 2) relPath.unshift('');
      result.pathname = relPath.join('/');
    } else {
      result.pathname = relative.pathname;
    }
    result.search = relative.search;
    result.query = relative.query;
    result.host = relative.host || '';
    result.auth = relative.auth;
    result.hostname = relative.hostname || relative.host;
    result.port = relative.port;
    if (result.pathname || result.search) result.path = (result.pathname || '') + (result.search || '');
    result.slashes = result.slashes || relative.slashes;
    result.href = result.format();
    return result;
  }

  const isSourceAbs = result.pathname && result.pathname[0] === '/';
  const isRelAbs = relative.host || (relative.pathname && relative.pathname[0] === '/');
  let mustEndAbs = isRelAbs || isSourceAbs || (result.host && relative.pathname);
  const removeAllDots = mustEndAbs;
  let srcPath = (result.pathname && result.pathname.split('/')) || [];
  const relPath = (relative.pathname && relative.pathname.split('/')) || [];
  const noLeadingSlashes = result.protocol && !slashedProtocol.has(result.protocol);

  if (noLeadingSlashes) {
    result.hostname = '';
    result.port = null;
    if (result.host) {
      if (srcPath[0] === '') srcPath[0] = result.host;
      else srcPath.unshift(result.host);
    }
    result.host = '';
    if (relative.protocol) {
      relative.hostname = null;
      relative.port = null;
      if (relative.host) {
        if (relPath[0] === '') relPath[0] = relative.host;
        else relPath.unshift(relative.host);
      }
      relative.host = null;
    }
    mustEndAbs = mustEndAbs && (relPath[0] === '' || srcPath[0] === '');
  }

  const splitAuth = () => {
    const authInHost = result.host && result.host.indexOf('@') > 0 ? result.host.split('@') : false;
    if (authInHost) {
      result.auth = authInHost.shift();
      result.host = result.hostname = authInHost.shift();
    }
  };

  if (isRelAbs) {
    if (relative.host || relative.host === '') {
      if (result.host !== relative.host) result.auth = null;
      result.host = relative.host;
      result.port = relative.port;
    }
    if (relative.hostname || relative.hostname === '') {
      if (result.hostname !== relative.hostname) result.auth = null;
      result.hostname = relative.hostname;
    }
    result.search = relative.search;
    result.query = relative.query;
    srcPath = relPath;
  } else if (relPath.length) {
    srcPath.pop();
    srcPath = srcPath.concat(relPath);
    result.search = relative.search;
    result.query = relative.query;
  } else if (relative.search !== null && relative.search !== undefined) {
    if (noLeadingSlashes) {
      result.hostname = result.host = srcPath.shift();
      splitAuth();
    }
    result.search = relative.search;
    result.query = relative.query;
    if (result.pathname !== null || result.search !== null) {
      result.path = (result.pathname ? result.pathname : '') + (result.search ? result.search : '');
    }
    result.href = result.format();
    return result;
  }

  if (!srcPath.length) {
    result.pathname = null;
    result.path = result.search ? '/' + result.search : null;
    result.href = result.format();
    return result;
  }

  let last = srcPath.slice(-1)[0];
  const hasTrailingSlash = ((result.host || relative.host || srcPath.length > 1) && (last === '.' || last === '..')) || last === '';
  let up = 0;
  for (let i = srcPath.length - 1; i >= 0; i--) {
    last = srcPath[i];
    if (last === '.') {
      srcPath.splice(i, 1);
    } else if (last === '..') {
      srcPath.splice(i, 1);
      up++;
    } else if (up) {
      srcPath.splice(i, 1);
      up--;
    }
  }
  if (!mustEndAbs && !removeAllDots) while (up--) srcPath.unshift('..');
  if (mustEndAbs && srcPath[0] !== '' && (!srcPath[0] || srcPath[0][0] !== '/')) srcPath.unshift('');
  if (hasTrailingSlash && srcPath.join('/').slice(-1) !== '/') srcPath.push('');

  const isAbsolute = srcPath[0] === '' || (srcPath[0] && srcPath[0][0] === '/');
  if (noLeadingSlashes) {
    result.hostname = result.host = isAbsolute ? '' : srcPath.length ? srcPath.shift() : '';
    splitAuth();
  }
  mustEndAbs = mustEndAbs || (result.host && srcPath.length);
  if (mustEndAbs && !isAbsolute) srcPath.unshift('');
  if (srcPath.length > 0) {
    result.pathname = srcPath.join('/');
  } else {
    result.pathname = null;
    result.path = null;
  }
  if (result.pathname !== null || result.search !== null) {
    result.path = (result.pathname ? result.pathname : '') + (result.search ? result.search : '');
  }
  result.auth = relative.auth || result.auth;
  result.slashes = result.slashes || relative.slashes;
  result.href = result.format();
  return result;
};

// url.format(): a URL (with options), a legacy object, or a string
function urlFormat(value, options) {
  if (typeof value === 'string') {
    value = urlParse(value);
  } else if (value === null || (typeof value !== 'object' && typeof value !== 'function')) {
    throw invalidArgType('urlObject', 'one of type object or string', value);
  } else if (isURL(value)) {
    let fragment = true, unicode = false, search = true, auth = true;
    if (options) {
      if (options.fragment !== undefined) fragment = Boolean(options.fragment);
      if (options.unicode !== undefined) unicode = Boolean(options.unicode);
      if (options.search !== undefined) search = Boolean(options.search);
      if (options.auth !== undefined) auth = Boolean(options.auth);
    }
    const record = value[kRecord];
    let out = record.scheme + ':';
    if (record.host !== null) {
      out += '//';
      if (auth && includesCredentials(record)) {
        out += record.username;
        if (record.password !== '') out += ':' + record.password;
        out += '@';
      }
      out += unicode ? domainToUnicodeInternal(record.host) : record.host;
      if (record.port !== null) out += ':' + record.port;
    }
    out += serializePath(record);
    if (search && record.query !== null) out += '?' + record.query;
    if (fragment && record.fragment !== null) out += '#' + record.fragment;
    return out;
  }
  return Url.prototype.format.call(value);
}

function urlResolve(source, relative) {
  return urlParse(source, false, true).resolve(relative);
}

function urlResolveObject(source, relative) {
  if (!source) return relative;
  return urlParse(source, false, true).resolveObject(relative);
}

return {
  Url, parse: urlParse, resolve: urlResolve, resolveObject: urlResolveObject, format: urlFormat,
  URL, URLSearchParams, domainToASCII, domainToUnicode, pathToFileURL, fileURLToPath, urlToHttpOptions,
};
})
)JS";

// Set up the URL and URLSearchParams globals and the url module (requires
// SetupPath, SetupUtil and SetupQuerystring first)
void SetupURL(Isolate* isolate, Local<Context> context) {
    Local<Value> idna = Undefined(isolate);
    if (GetIdna().handle != nullptr && getenv("FROGJS_NO_ICU_IDNA") == nullptr) {
        Local<Object> native = Object::New(isolate);
        frogjs::SetFunction(isolate, context, native, "toASCII", ToASCII);
        frogjs::SetFunction(isolate, context, native, "toUnicode", ToUnicode);
        idna = native;
    }
    Local<Value> argv[4] = {frogjs::GetBuiltin(isolate, context, "path"), frogjs::GetBuiltin(isolate, context, "util"),
                            frogjs::GetBuiltin(isolate, context, "querystring"), idna};
    Local<Value> module_value;
    if (!frogjs::RunJsModule(isolate, context, "url", kUrlSource, 4, argv).ToLocal(&module_value)) return;
    Local<Object> module = module_value.As<Object>();
    frogjs::RegisterBuiltin(isolate, context, "url", module);
    Local<Object> global = context->Global();
    for (const char* name : {"URL", "URLSearchParams"}) {
        Local<String> key = String::NewFromUtf8(isolate, name).ToLocalChecked();
        global->Set(context, key, module->Get(context, key).ToLocalChecked()).Check();
    }
}
