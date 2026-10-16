#include <v8.h>
#include "frogjs/v8_compat.h"
#include "frogjs/buffer.h"
#include <cstring>
#include <algorithm>
#include <vector>

using namespace v8;

// Constants for Buffer
static const int BUFFER_DATA_FIELD = 0;
static const int BUFFER_LENGTH_FIELD = 1;

// Template behind the Buffer constructor, used by IsBuffer()
static Persistent<FunctionTemplate> g_buffer_template;

bool IsBuffer(Local<Value> value) {
    if (g_buffer_template.IsEmpty() || !value->IsObject()) {
        return false;
    }
    Isolate* isolate = Isolate::GetCurrent();
    return Local<FunctionTemplate>::New(isolate, g_buffer_template)->HasInstance(value);
}

// Get internal BufferData from object
BufferData* GetBufferData(Local<Object> buffer_obj) {
    return frogjs::ExternalValue<BufferData>(buffer_obj->GetInternalField(BUFFER_DATA_FIELD).As<Value>());
}

// Buffer constructor callback
void BufferConstructor(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.IsConstructCall()) {
        // Called with new Buffer(size)
        Local<Object> buffer_obj = args.This();

        // Default: create empty buffer
        size_t length = 0;
        char* data = nullptr;
        bool owns_data = true;

        if (args.Length() >= 1 && args[0]->IsNumber()) {
            // Buffer(size) - allocate buffer of given size
            length = args[0]->IntegerValue(context).ToChecked();
            if (length > 0) {
                data = new char[length]();  // Zero-initialize
            }
        }

        // Create BufferData
        BufferData* buffer_data = new BufferData{data, length, owns_data};

        // Set internal fields
        buffer_obj->SetInternalField(BUFFER_DATA_FIELD, frogjs::NewExternal(isolate, buffer_data));
        buffer_obj->SetInternalField(BUFFER_LENGTH_FIELD, Integer::New(isolate, length));

        // Set length property on the instance
        buffer_obj->Set(
            context,
            String::NewFromUtf8(isolate, "length").ToLocalChecked(),
            Integer::New(isolate, length)
        ).Check();
    } else {
        // Called without new - throw error
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer constructor must be called with new").ToLocalChecked());
    }
}

// Buffer.alloc(size) - static method
void BufferAlloc(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsNumber()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer.alloc requires a size parameter").ToLocalChecked());
        return;
    }

    size_t size = args[0]->IntegerValue(context).ToChecked();

    // Get Buffer constructor
    Local<Value> buffer_ctor = context->Global()->Get(
        context,
        String::NewFromUtf8(isolate, "Buffer").ToLocalChecked()
    ).ToLocalChecked();

    if (buffer_ctor->IsFunction()) {
        // Create buffer with size
        Local<Function> ctor = Local<Function>::Cast(buffer_ctor);
        Local<Value> argv[1] = {Number::New(isolate, size)};
        Local<Object> buffer = ctor->NewInstance(context, 1, argv).ToLocalChecked();
        args.GetReturnValue().Set(buffer);
    }
}

// Buffer.from(string/array) - static method
void BufferFrom(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer.from requires data parameter").ToLocalChecked());
        return;
    }

    // Get Buffer constructor
    Local<Value> buffer_ctor = context->Global()->Get(
        context,
        String::NewFromUtf8(isolate, "Buffer").ToLocalChecked()
    ).ToLocalChecked();

    if (!buffer_ctor->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer constructor not found").ToLocalChecked());
        return;
    }

    Local<Function> ctor = Local<Function>::Cast(buffer_ctor);
    Local<Object> buffer_obj;
    size_t length = 0;
    char* data = nullptr;

    if (args[0]->IsString()) {
        // Buffer.from(string)
        String::Utf8Value str(isolate, args[0]);
        length = str.length();
        data = new char[length];
        memcpy(data, *str, length);
    } else if (args[0]->IsArray()) {
        // Buffer.from(array)
        Local<Array> array = Local<Array>::Cast(args[0]);
        length = array->Length();
        data = new char[length];

        for (size_t i = 0; i < length; i++) {
            Local<Value> element = array->Get(context, i).ToLocalChecked();
            if (element->IsNumber()) {
                data[i] = static_cast<char>(element->IntegerValue(context).ToChecked());
            } else {
                data[i] = 0;
            }
        }
    } else {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer.from requires string or array").ToLocalChecked());
        return;
    }

    // Create buffer instance
    Local<Value> argv[1] = {Number::New(isolate, length)};
    buffer_obj = ctor->NewInstance(context, 1, argv).ToLocalChecked();

    // Replace the empty data with our actual data
    BufferData* old_data = GetBufferData(buffer_obj);
    if (old_data && old_data->owns_data && old_data->data) {
        delete[] old_data->data;
    }
    delete old_data;

    // Set new data
    BufferData* buffer_data = new BufferData{data, length, true};
    buffer_obj->SetInternalField(BUFFER_DATA_FIELD, frogjs::NewExternal(isolate, buffer_data));
    buffer_obj->SetInternalField(BUFFER_LENGTH_FIELD, Integer::New(isolate, length));

    // Update length property on the instance
    buffer_obj->Set(
        context,
        String::NewFromUtf8(isolate, "length").ToLocalChecked(),
        Integer::New(isolate, length)
    ).Check();

    args.GetReturnValue().Set(buffer_obj);
}

// Buffer.byteLength(string) - static method
void BufferByteLength(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer.byteLength requires a string").ToLocalChecked());
        return;
    }

    String::Utf8Value str(isolate, args[0]);
    args.GetReturnValue().Set(static_cast<int>(str.length()));
}

// buf.toString(encoding)
void BufferToString(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    Local<Object> buffer_obj = args.This();
    BufferData* buffer_data = GetBufferData(buffer_obj);

    if (buffer_data == nullptr || buffer_data->data == nullptr) {
        args.GetReturnValue().Set(String::Empty(isolate));
        return;
    }

    // For now, only support utf8 encoding (default)
    args.GetReturnValue().Set(String::NewFromUtf8(isolate,
        buffer_data->data, NewStringType::kNormal, buffer_data->length).ToLocalChecked());
}

// buf.write(string, offset)
void BufferWrite(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "write requires a string").ToLocalChecked());
        return;
    }

    Local<Object> buffer_obj = args.This();
    BufferData* buffer_data = GetBufferData(buffer_obj);

    if (buffer_data == nullptr) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Invalid buffer object").ToLocalChecked());
        return;
    }

    String::Utf8Value str(isolate, args[0]);
    size_t offset = 0;

    if (args.Length() >= 2 && args[1]->IsNumber()) {
        offset = args[1]->IntegerValue(context).ToChecked();
    }

    // Check bounds
    if (offset >= buffer_data->length) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Offset is out of bounds").ToLocalChecked());
        return;
    }

    // Calculate how much we can write
    size_t max_write = buffer_data->length - offset;
    size_t to_write = std::min(static_cast<size_t>(str.length()), max_write);

    // Copy data
    memcpy(buffer_data->data + offset, *str, to_write);

    args.GetReturnValue().Set(static_cast<int>(to_write));
}

// buf.slice(start, end)
void BufferSlice(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<Object> buffer_obj = args.This();
    BufferData* buffer_data = GetBufferData(buffer_obj);

    if (buffer_data == nullptr) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Invalid buffer object").ToLocalChecked());
        return;
    }

    size_t start = 0;
    size_t end = buffer_data->length;

    if (args.Length() >= 1 && args[0]->IsNumber()) {
        start = args[0]->IntegerValue(context).ToChecked();
    }

    if (args.Length() >= 2 && args[1]->IsNumber()) {
        end = args[1]->IntegerValue(context).ToChecked();
    }

    // Validate bounds
    if (start > buffer_data->length || end > buffer_data->length || start > end) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Slice indices out of bounds").ToLocalChecked());
        return;
    }

    size_t slice_length = end - start;

    // Get Buffer constructor
    Local<Value> buffer_ctor = context->Global()->Get(
        context,
        String::NewFromUtf8(isolate, "Buffer").ToLocalChecked()
    ).ToLocalChecked();

    if (!buffer_ctor->IsFunction()) {
        isolate->ThrowException(String::NewFromUtf8(isolate,
            "Buffer constructor not found").ToLocalChecked());
        return;
    }

    // Create new buffer instance
    Local<Function> ctor = Local<Function>::Cast(buffer_ctor);
    Local<Value> argv[1] = {Number::New(isolate, slice_length)};
    Local<Object> slice_obj = ctor->NewInstance(context, 1, argv).ToLocalChecked();

    // Replace the empty data with slice data (shared ownership)
    BufferData* old_data = GetBufferData(slice_obj);
    if (old_data && old_data->owns_data && old_data->data) {
        delete[] old_data->data;
    }
    delete old_data;

    // Create slice that points to same data (doesn't own it)
    BufferData* slice_data = new BufferData{
        buffer_data->data + start,
        slice_length,
        false  // Doesn't own data - original buffer owns it
    };
    slice_obj->SetInternalField(BUFFER_DATA_FIELD, frogjs::NewExternal(isolate, slice_data));
    slice_obj->SetInternalField(BUFFER_LENGTH_FIELD, Integer::New(isolate, slice_length));

    // Update length property on the slice instance
    slice_obj->Set(
        context,
        String::NewFromUtf8(isolate, "length").ToLocalChecked(),
        Integer::New(isolate, slice_length)
    ).Check();

    args.GetReturnValue().Set(slice_obj);
}

// Create a Buffer that takes ownership of data (allocated with new[])
static Local<Object> NewBufferFromData(Isolate* isolate, Local<Context> context, char* data, size_t length) {
    Local<Function> ctor = Local<FunctionTemplate>::New(isolate, g_buffer_template)->GetFunction(context).ToLocalChecked();
    Local<Value> argv[1] = {Number::New(isolate, 0)};
    Local<Object> buffer_obj = ctor->NewInstance(context, 1, argv).ToLocalChecked();

    delete GetBufferData(buffer_obj);  // empty placeholder from the constructor
    buffer_obj->SetInternalField(BUFFER_DATA_FIELD, frogjs::NewExternal(isolate, new BufferData{data, length, true}));
    buffer_obj->SetInternalField(BUFFER_LENGTH_FIELD, Integer::New(isolate, length));
    buffer_obj->Set(context, String::NewFromUtf8(isolate, "length").ToLocalChecked(),
        Integer::New(isolate, length)).Check();
    return buffer_obj;
}

// Buffer.isBuffer(value)
void BufferIsBuffer(const FunctionCallbackInfo<Value>& args) {
    args.GetReturnValue().Set(args.Length() > 0 && IsBuffer(args[0]));
}

// Buffer.concat(list[, totalLength])
void BufferConcat(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsArray()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,
            "The \"list\" argument must be an Array of Buffers").ToLocalChecked()));
        return;
    }

    Local<Array> list = args[0].As<Array>();
    std::vector<BufferData*> parts;
    size_t total = 0;
    for (uint32_t i = 0; i < list->Length(); i++) {
        Local<Value> item = list->Get(context, i).ToLocalChecked();
        if (!IsBuffer(item)) {
            isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,
                "The \"list\" argument must be an Array of Buffers").ToLocalChecked()));
            return;
        }
        BufferData* data = GetBufferData(item.As<Object>());
        parts.push_back(data);
        total += data->length;
    }

    size_t length = total;
    if (args.Length() >= 2 && args[1]->IsNumber()) {
        int64_t requested = args[1]->IntegerValue(context).FromMaybe(0);
        length = requested > 0 ? static_cast<size_t>(requested) : 0;
    }

    char* data = length > 0 ? new char[length]() : nullptr;
    size_t offset = 0;
    for (BufferData* part : parts) {
        if (offset >= length) break;
        size_t n = std::min(part->length, length - offset);
        if (n > 0) memcpy(data + offset, part->data, n);
        offset += n;
    }
    args.GetReturnValue().Set(NewBufferFromData(isolate, context, data, length));
}

// toByteString(value): UTF-8 encode a string, or copy a Buffer's raw bytes, into a
// byte string (one char per byte, codes 0-255)
void ToByteString(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);

    if (args.Length() > 0 && IsBuffer(args[0])) {
        BufferData* data = GetBufferData(args[0].As<Object>());
        if (data == nullptr || data->data == nullptr || data->length == 0) {
            args.GetReturnValue().Set(String::Empty(isolate));
            return;
        }
        args.GetReturnValue().Set(String::NewFromOneByte(isolate, (const uint8_t*)data->data,
            NewStringType::kNormal, data->length).ToLocalChecked());
        return;
    }

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,
            "The \"chunk\" argument must be of type string or Buffer").ToLocalChecked()));
        return;
    }

    String::Utf8Value utf8(isolate, args[0]);
    args.GetReturnValue().Set(String::NewFromOneByte(isolate, (const uint8_t*)*utf8,
        NewStringType::kNormal, utf8.length()).ToLocalChecked());
}

// byteStringToUtf8(bytes): decode a byte string as UTF-8 (invalid sequences become U+FFFD)
void ByteStringToUtf8(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> str;
    if (args.Length() < 1 || !args[0]->ToString(context).ToLocal(&str) || str->Length() == 0) {
        args.GetReturnValue().Set(String::Empty(isolate));
        return;
    }

    std::vector<uint8_t> bytes(str->Length());
    frogjs::WriteOneByte(isolate, str, bytes.data());
    args.GetReturnValue().Set(String::NewFromUtf8(isolate, (const char*)bytes.data(),
        NewStringType::kNormal, bytes.size()).ToLocalChecked());
}

// byteStringToBuffer(bytes): a Buffer holding a byte string's bytes
void ByteStringToBuffer(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope handle_scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> str;
    if (args.Length() < 1 || !args[0]->ToString(context).ToLocal(&str)) return;
    size_t length = str->Length();
    char* data = length > 0 ? new char[length] : nullptr;
    if (length > 0) frogjs::WriteOneByte(isolate, str, reinterpret_cast<uint8_t*>(data));
    args.GetReturnValue().Set(NewBufferFromData(isolate, context, data, length));
}

// Buffer finalizer - cleanup data when buffer is garbage collected
void BufferFinalizer(const WeakCallbackInfo<Object>& data) {
    // This would be called when buffer is garbage collected
    // For simplicity, we're relying on explicit cleanup for now
    // In production, you'd want proper weak callback handling
}

// Set up Buffer class
void SetupBuffer(Isolate* isolate, Local<Context> context) {
    // Create Buffer constructor template
    Local<FunctionTemplate> buffer_tpl = FunctionTemplate::New(isolate, BufferConstructor);
    buffer_tpl->SetClassName(String::NewFromUtf8(isolate, "Buffer").ToLocalChecked());
    buffer_tpl->InstanceTemplate()->SetInternalFieldCount(2);
    g_buffer_template.Reset(isolate, buffer_tpl);

    // Add instance methods
    Local<ObjectTemplate> instance_tpl = buffer_tpl->InstanceTemplate();

    // buf.toString()
    instance_tpl->Set(
        String::NewFromUtf8(isolate, "toString").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferToString)
    );

    // buf.write()
    instance_tpl->Set(
        String::NewFromUtf8(isolate, "write").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferWrite)
    );

    // buf.slice()
    instance_tpl->Set(
        String::NewFromUtf8(isolate, "slice").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferSlice)
    );

    // Create constructor function
    Local<Function> buffer_ctor = buffer_tpl->GetFunction(context).ToLocalChecked();

    // Add static methods to constructor
    // Buffer.alloc()
    buffer_ctor->Set(
        context,
        String::NewFromUtf8(isolate, "alloc").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferAlloc)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Buffer.from()
    buffer_ctor->Set(
        context,
        String::NewFromUtf8(isolate, "from").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferFrom)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Buffer.concat()
    buffer_ctor->Set(
        context,
        String::NewFromUtf8(isolate, "concat").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferConcat)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Buffer.isBuffer()
    buffer_ctor->Set(
        context,
        String::NewFromUtf8(isolate, "isBuffer").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferIsBuffer)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Buffer.byteLength()
    buffer_ctor->Set(
        context,
        String::NewFromUtf8(isolate, "byteLength").ToLocalChecked(),
        FunctionTemplate::New(isolate, BufferByteLength)->GetFunction(context).ToLocalChecked()
    ).Check();

    // Add Buffer to global scope
    context->Global()->Set(
        context,
        String::NewFromUtf8(isolate, "Buffer").ToLocalChecked(),
        buffer_ctor
    ).Check();
}
