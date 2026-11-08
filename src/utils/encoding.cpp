#include "frogjs/encoding.h"
#include "frogjs/buffer.h"
#include "frogjs/v8_compat.h"
#include <algorithm>
#include <cctype>
#include <vector>

using namespace v8;

namespace frogjs {

namespace {

const char kHexDigits[] = "0123456789abcdef";
const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kBase64UrlChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Value of each byte as a hex digit and as a base64 character (both alphabets:
// '+' and '-' are 62, '/' and '_' are 63), or -1
struct DecodeTables {
    int8_t hex[256];
    int8_t base64[256];
    constexpr DecodeTables() : hex(), base64() {
        for (int c = 0; c < 256; c++) {
            hex[c] = -1;
            base64[c] = -1;
            if (c >= '0' && c <= '9') hex[c] = static_cast<int8_t>(c - '0');
            if (c >= 'a' && c <= 'f') hex[c] = static_cast<int8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') hex[c] = static_cast<int8_t>(c - 'A' + 10);
            if (c >= 'A' && c <= 'Z') base64[c] = static_cast<int8_t>(c - 'A');
            if (c >= 'a' && c <= 'z') base64[c] = static_cast<int8_t>(c - 'a' + 26);
            if (c >= '0' && c <= '9') base64[c] = static_cast<int8_t>(c - '0' + 52);
        }
        base64[static_cast<int>('+')] = base64[static_cast<int>('-')] = 62;
        base64[static_cast<int>('/')] = base64[static_cast<int>('_')] = 63;
    }
};
constexpr DecodeTables kTables;

inline int HexValue(uint16_t c) {
    return c < 256 ? kTables.hex[c] : -1;
}

inline int Base64Value(uint16_t c) {
    return c < 256 ? kTables.base64[c] : -1;
}

std::vector<uint16_t> CodeUnits(Isolate* isolate, Local<String> str) {
    std::vector<uint16_t> units(str->Length());
    if (!units.empty()) WriteTwoByte(isolate, str, units.data());
    return units;
}

std::string Base64Encode(const unsigned char* data, size_t length, bool url) {
    const char* chars = url ? kBase64UrlChars : kBase64Chars;
    std::string out((length + 2) / 3 * 4, '\0');
    size_t written = 0;
    size_t i = 0;
    for (; i + 2 < length; i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out[written++] = chars[(n >> 18) & 63];
        out[written++] = chars[(n >> 12) & 63];
        out[written++] = chars[(n >> 6) & 63];
        out[written++] = chars[n & 63];
    }
    size_t rest = length - i;
    if (rest > 0) {
        uint32_t n = data[i] << 16;
        if (rest == 2) n |= data[i + 1] << 8;
        out[written++] = chars[(n >> 18) & 63];
        out[written++] = chars[(n >> 12) & 63];
        if (rest == 2) out[written++] = chars[(n >> 6) & 63];
        if (!url) {
            out[written++] = '=';
            if (rest == 1) out[written++] = '=';
        }
    }
    out.resize(written);
    return out;
}

}  // namespace

bool ParseEncoding(const std::string& name, Encoding* encoding) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "utf8" || lower == "utf-8") *encoding = Encoding::kUtf8;
    else if (lower == "hex") *encoding = Encoding::kHex;
    else if (lower == "base64") *encoding = Encoding::kBase64;
    else if (lower == "base64url") *encoding = Encoding::kBase64Url;
    else if (lower == "latin1" || lower == "binary") *encoding = Encoding::kLatin1;
    else if (lower == "ascii") *encoding = Encoding::kAscii;
    else return false;
    return true;
}

bool GetEncodingArg(Isolate* isolate, Local<Value> value, Encoding fallback, Encoding* encoding) {
    if (value.IsEmpty() || value->IsNullOrUndefined()) {
        *encoding = fallback;
        return true;
    }
    String::Utf8Value name(isolate, value);
    if (*name != nullptr && ParseEncoding(*name, encoding)) return true;

    std::string message = std::string("Unknown encoding: ") + (*name != nullptr ? *name : "");
    Local<Value> error = Exception::TypeError(String::NewFromUtf8(isolate, message.c_str()).ToLocalChecked());
    Local<Context> context = isolate->GetCurrentContext();
    error.As<Object>()->Set(context, String::NewFromUtf8Literal(isolate, "code"),
        String::NewFromUtf8Literal(isolate, "ERR_UNKNOWN_ENCODING")).Check();
    isolate->ThrowException(error);
    return false;
}

namespace {

// One-byte strings (almost all of them) are read without widening. out must
// hold MaxDecodedLength() bytes. Returns the bytes written.
template <typename Unit>
size_t DecodeHex(const Unit* units, size_t length, char* out) {
    size_t written = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        int high = HexValue(units[i]);
        int low = HexValue(units[i + 1]);
        if (high < 0 || low < 0) break;
        out[written++] = static_cast<char>((high << 4) | low);
    }
    return written;
}

template <typename Unit>
size_t DecodeBase64(const Unit* units, size_t length, char* out) {
    size_t written = 0;
    uint32_t bits = 0;
    int count = 0;
    for (size_t i = 0; i < length; i++) {
        if (units[i] == '=') break;
        int value = Base64Value(units[i]);
        if (value < 0) continue;
        bits = (bits << 6) | static_cast<uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out[written++] = static_cast<char>((bits >> count) & 0xFF);
        }
    }
    return written;
}

}  // namespace

std::string DecodeString(Isolate* isolate, Local<String> str, Encoding encoding) {
    std::string out;
    size_t length = str->Length();
    switch (encoding) {
        case Encoding::kUtf8: {
            out.resize(Utf8Length(isolate, str));
            if (!out.empty()) WriteUtf8(isolate, str, &out[0], out.size());
            break;
        }
        case Encoding::kLatin1:
        case Encoding::kAscii:
            // Code units above 255 keep their low byte
            out.resize(length);
            if (length > 0) WriteOneByte(isolate, str, reinterpret_cast<uint8_t*>(&out[0]));
            break;
        case Encoding::kHex:
        case Encoding::kBase64:
        case Encoding::kBase64Url:
            out.resize(MaxDecodedLength(length, encoding));
            out.resize(DecodeInto(isolate, str, encoding, out.empty() ? nullptr : &out[0]));
            break;
    }
    return out;
}

size_t MaxDecodedLength(size_t string_length, Encoding encoding) {
    if (encoding == Encoding::kHex) return string_length / 2;
    return string_length / 4 * 3 + 3;
}

size_t DecodeInto(Isolate* isolate, Local<String> str, Encoding encoding, char* out) {
    size_t length = str->Length();
    if (length == 0) return 0;
    // A small stack buffer covers most strings without an allocation
    uint8_t small[1024];
    if (str->IsOneByte()) {
        std::vector<uint8_t> large;
        uint8_t* units = small;
        if (length > sizeof(small)) {
            large.resize(length);
            units = large.data();
        }
        WriteOneByte(isolate, str, units);
        return encoding == Encoding::kHex ? DecodeHex(units, length, out) : DecodeBase64(units, length, out);
    }
    std::vector<uint16_t> units = CodeUnits(isolate, str);
    return encoding == Encoding::kHex ? DecodeHex(units.data(), length, out) : DecodeBase64(units.data(), length, out);
}

// Strings have a maximum length; going past it must throw, not abort
static Local<String> ThrowStringTooLong(Isolate* isolate) {
    Local<Value> error = Exception::Error(String::NewFromUtf8Literal(isolate,
        "Cannot create a string longer than the maximum string length"));
    error.As<Object>()->Set(isolate->GetCurrentContext(), String::NewFromUtf8Literal(isolate, "code"),
        String::NewFromUtf8Literal(isolate, "ERR_STRING_TOO_LONG")).Check();
    isolate->ThrowException(error);
    return Local<String>();
}

MaybeLocal<String> EncodeBytes(Isolate* isolate, const char* data, size_t length, Encoding encoding) {
    const size_t max = static_cast<size_t>(String::kMaxLength);
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    if (length > max * 3) return ThrowStringTooLong(isolate);

    Local<String> result;
    std::string out;
    switch (encoding) {
        case Encoding::kUtf8:
            if (String::NewFromUtf8(isolate, data, NewStringType::kNormal, static_cast<int>(length)).ToLocal(&result)) {
                return result;
            }
            return ThrowStringTooLong(isolate);
        case Encoding::kLatin1:
            if (length <= max && String::NewFromOneByte(isolate, bytes, NewStringType::kNormal,
                    static_cast<int>(length)).ToLocal(&result)) {
                return result;
            }
            return ThrowStringTooLong(isolate);
        case Encoding::kAscii: {
            if (length > max) return ThrowStringTooLong(isolate);
            std::vector<uint8_t> masked(bytes, bytes + length);
            for (uint8_t& byte : masked) byte &= 0x7F;
            if (String::NewFromOneByte(isolate, masked.data(), NewStringType::kNormal,
                    static_cast<int>(length)).ToLocal(&result)) {
                return result;
            }
            return ThrowStringTooLong(isolate);
        }
        case Encoding::kHex:
            if (length > max / 2) return ThrowStringTooLong(isolate);
            out.resize(length * 2);
            for (size_t i = 0; i < length; i++) {
                out[i * 2] = kHexDigits[bytes[i] >> 4];
                out[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
            }
            break;
        case Encoding::kBase64:
        case Encoding::kBase64Url:
            if (length > max / 4 * 3) return ThrowStringTooLong(isolate);
            out = Base64Encode(bytes, length, encoding == Encoding::kBase64Url);
            break;
    }
    if (String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(out.data()), NewStringType::kNormal,
            static_cast<int>(out.size())).ToLocal(&result)) {
        return result;
    }
    return ThrowStringTooLong(isolate);
}

bool GetBinaryData(Local<Value> value, std::string* out) {
    if (value->IsArrayBufferView()) {
        Local<ArrayBufferView> view = value.As<ArrayBufferView>();
        out->resize(view->ByteLength());
        if (!out->empty()) view->CopyContents(&(*out)[0], out->size());
        return true;
    }
    if (value->IsArrayBuffer()) {
        std::shared_ptr<BackingStore> store = value.As<ArrayBuffer>()->GetBackingStore();
        out->assign(static_cast<const char*>(store->Data()), store->ByteLength());
        return true;
    }
    return false;
}

}  // namespace frogjs
