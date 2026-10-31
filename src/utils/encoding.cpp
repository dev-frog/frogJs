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

int HexValue(uint16_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Both alphabets decode: '+' and '-' are 62, '/' and '_' are 63
int Base64Value(uint16_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

std::vector<uint16_t> CodeUnits(Isolate* isolate, Local<String> str) {
    std::vector<uint16_t> units(str->Length());
    if (!units.empty()) WriteTwoByte(isolate, str, units.data());
    return units;
}

std::string Base64Encode(const unsigned char* data, size_t length, bool url) {
    const char* chars = url ? kBase64UrlChars : kBase64Chars;
    std::string out;
    out.reserve((length + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < length; i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += chars[(n >> 6) & 63];
        out += chars[n & 63];
    }
    size_t rest = length - i;
    if (rest > 0) {
        uint32_t n = data[i] << 16;
        if (rest == 2) n |= data[i + 1] << 8;
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        if (rest == 2) out += chars[(n >> 6) & 63];
        if (!url) out.append(rest == 1 ? "==" : "=");
    }
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

std::string DecodeString(Isolate* isolate, Local<String> str, Encoding encoding) {
    std::string out;
    switch (encoding) {
        case Encoding::kUtf8: {
            String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) out.assign(*utf8, utf8.length());
            break;
        }
        case Encoding::kLatin1:
        case Encoding::kAscii:
            for (uint16_t unit : CodeUnits(isolate, str)) out += static_cast<char>(unit & 0xFF);
            break;
        case Encoding::kHex: {
            std::vector<uint16_t> units = CodeUnits(isolate, str);
            for (size_t i = 0; i + 1 < units.size(); i += 2) {
                int high = HexValue(units[i]);
                int low = HexValue(units[i + 1]);
                if (high < 0 || low < 0) break;
                out += static_cast<char>((high << 4) | low);
            }
            break;
        }
        case Encoding::kBase64:
        case Encoding::kBase64Url: {
            uint32_t bits = 0;
            int count = 0;
            for (uint16_t unit : CodeUnits(isolate, str)) {
                if (unit == '=') break;
                int value = Base64Value(unit);
                if (value < 0) continue;
                bits = (bits << 6) | static_cast<uint32_t>(value);
                count += 6;
                if (count >= 8) {
                    count -= 8;
                    out += static_cast<char>((bits >> count) & 0xFF);
                }
            }
            break;
        }
    }
    return out;
}

Local<String> EncodeBytes(Isolate* isolate, const char* data, size_t length, Encoding encoding) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    std::string out;
    switch (encoding) {
        case Encoding::kUtf8:
            return String::NewFromUtf8(isolate, data, NewStringType::kNormal, static_cast<int>(length)).ToLocalChecked();
        case Encoding::kLatin1:
            return String::NewFromOneByte(isolate, bytes, NewStringType::kNormal, static_cast<int>(length)).ToLocalChecked();
        case Encoding::kAscii: {
            std::vector<uint8_t> masked(bytes, bytes + length);
            for (uint8_t& byte : masked) byte &= 0x7F;
            return String::NewFromOneByte(isolate, masked.data(), NewStringType::kNormal,
                static_cast<int>(length)).ToLocalChecked();
        }
        case Encoding::kHex:
            out.reserve(length * 2);
            for (size_t i = 0; i < length; i++) {
                out += kHexDigits[bytes[i] >> 4];
                out += kHexDigits[bytes[i] & 0x0F];
            }
            break;
        case Encoding::kBase64:
        case Encoding::kBase64Url:
            out = Base64Encode(bytes, length, encoding == Encoding::kBase64Url);
            break;
    }
    return String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(out.data()),
        NewStringType::kNormal, static_cast<int>(out.size())).ToLocalChecked();
}

bool GetBinaryData(Local<Value> value, std::string* out) {
    if (IsBuffer(value)) {
        BufferData* data = GetBufferData(value.As<Object>());
        if (data != nullptr && data->data != nullptr) out->assign(data->data, data->length);
        else out->clear();
        return true;
    }
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
