#include "fake_napi.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

napi_value New(napi_value__::Kind kind) {
  auto* v = new napi_value__();
  v->kind = kind;
  return v;
}

napi_value NewString(const std::string& s) {
  napi_value v = New(napi_value__::kString);
  v->str = s;
  return v;
}

}  // namespace

extern "C" {

napi_status napi_get_cb_info(napi_env env, napi_callback_info info, size_t* argc,
                             napi_value* argv, napi_value* this_arg, void** data) {
  (void)env;
  (void)this_arg;
  (void)data;
  if (info == nullptr || argc == nullptr) return napi_invalid_arg;
  const size_t requested = *argc;
  const size_t actual = info->argv.size();
  *argc = actual;
  if (argv != nullptr) {
    const size_t n = std::min(requested, actual);
    for (size_t i = 0; i < n; ++i) argv[i] = info->argv[i];
  }
  return napi_ok;
}

napi_status napi_is_typedarray(napi_env env, napi_value value, bool* result) {
  (void)env;
  if (value == nullptr || result == nullptr) return napi_invalid_arg;
  *result = value->kind == napi_value__::kTypedArray;
  return napi_ok;
}

napi_status napi_get_typedarray_info(napi_env env, napi_value typedarray,
                                     napi_typedarray_type* type, size_t* length,
                                     void** data, napi_value* arraybuffer,
                                     size_t* byte_offset) {
  (void)env;
  if (typedarray == nullptr || typedarray->kind != napi_value__::kTypedArray) {
    return napi_invalid_arg;
  }
  if (type != nullptr) *type = napi_uint8_array;
  if (length != nullptr) *length = typedarray->bytes.size();
  if (data != nullptr) {
    *data = typedarray->bytes.empty()
                ? nullptr
                : static_cast<void*>(typedarray->bytes.data());
  }
  if (arraybuffer != nullptr) *arraybuffer = typedarray->buffer;
  if (byte_offset != nullptr) *byte_offset = 0;
  return napi_ok;
}

napi_status napi_is_detached_arraybuffer(napi_env env, napi_value value, bool* result) {
  (void)env;
  if (value == nullptr || result == nullptr) return napi_invalid_arg;
  *result = value->detached;
  return napi_ok;
}

napi_status napi_create_object(napi_env env, napi_value* result) {
  (void)env;
  if (result == nullptr) return napi_invalid_arg;
  *result = New(napi_value__::kObject);
  return napi_ok;
}

napi_status napi_create_array_with_length(napi_env env, size_t length, napi_value* result) {
  (void)env;
  if (result == nullptr) return napi_invalid_arg;
  napi_value v = New(napi_value__::kArray);
  v->array.assign(length, nullptr);
  *result = v;
  return napi_ok;
}

napi_status napi_set_element(napi_env env, napi_value object, uint32_t index, napi_value value) {
  (void)env;
  if (object == nullptr || object->kind != napi_value__::kArray || value == nullptr) {
    return napi_invalid_arg;
  }
  if (object->array.size() <= index) object->array.resize(static_cast<size_t>(index) + 1, nullptr);
  object->array[index] = value;
  return napi_ok;
}

napi_status napi_set_property(napi_env env, napi_value object, napi_value key, napi_value value) {
  (void)env;
  if (object == nullptr || object->kind != napi_value__::kObject || key == nullptr ||
      key->kind != napi_value__::kString || value == nullptr) {
    return napi_invalid_arg;
  }
  object->object[key->str] = value;
  return napi_ok;
}

napi_status napi_set_named_property(napi_env env, napi_value object, const char* utf8name,
                                    napi_value value) {
  (void)env;
  if (object == nullptr || object->kind != napi_value__::kObject || utf8name == nullptr ||
      value == nullptr) {
    return napi_invalid_arg;
  }
  object->object[utf8name] = value;
  return napi_ok;
}

napi_status napi_create_string_utf8(napi_env env, const char* str, size_t length,
                                    napi_value* result) {
  (void)env;
  if (result == nullptr) return napi_invalid_arg;
  *result = NewString(str == nullptr ? std::string() : std::string(str, length));
  return napi_ok;
}

napi_status napi_create_int32(napi_env env, int32_t value, napi_value* result) {
  (void)env;
  if (result == nullptr) return napi_invalid_arg;
  napi_value v = New(napi_value__::kInt32);
  v->i32 = value;
  *result = v;
  return napi_ok;
}

napi_status napi_get_null(napi_env env, napi_value* result) {
  (void)env;
  if (result == nullptr) return napi_invalid_arg;
  *result = New(napi_value__::kNull);
  return napi_ok;
}

napi_status napi_throw_error(napi_env env, const char* code, const char* msg) {
  if (env == nullptr) return napi_invalid_arg;
  env->pending = true;
  env->error = std::string(code == nullptr ? "" : code) + ": " +
               std::string(msg == nullptr ? "" : msg);
  return napi_ok;
}

napi_status napi_throw_type_error(napi_env env, const char* code, const char* msg) {
  return napi_throw_error(env, code, msg);
}

napi_status napi_throw_range_error(napi_env env, const char* code, const char* msg) {
  return napi_throw_error(env, code, msg);
}

napi_status napi_is_exception_pending(napi_env env, bool* result) {
  if (env == nullptr || result == nullptr) return napi_invalid_arg;
  *result = env->pending;
  return napi_ok;
}

napi_status napi_open_handle_scope(napi_env env, napi_handle_scope* result) {
  if (env == nullptr || result == nullptr) return napi_invalid_arg;
  *result = new napi_handle_scope__();
  ++env->scopes_open;
  return napi_ok;
}

napi_status napi_close_handle_scope(napi_env env, napi_handle_scope scope) {
  if (env == nullptr || scope == nullptr || env->scopes_open <= env->scopes_closed) {
    return napi_handle_scope_mismatch;
  }
  delete scope;
  ++env->scopes_closed;
  return napi_ok;
}

}  // extern "C"

namespace fake {

napi_env env_new() { return new napi_env__(); }

napi_value make_uint8(const std::vector<uint8_t>& bytes) {
  napi_value v = New(napi_value__::kTypedArray);
  v->bytes = bytes;
  v->buffer = New(napi_value__::kObject);
  return v;
}

napi_value make_object() { return New(napi_value__::kObject); }

napi_callback_info cb(napi_value arg) {
  auto* info = new napi_callback_info__();
  info->argv.push_back(arg);
  return info;
}

bool has_exception(napi_env env) { return env != nullptr && env->pending; }
std::string exception(napi_env env) { return env == nullptr ? std::string() : env->error; }

std::string type(napi_value value) {
  if (value == nullptr) return "nullptr";
  switch (value->kind) {
    case napi_value__::kUndefined: return "undefined";
    case napi_value__::kNull: return "null";
    case napi_value__::kInt32: return "int32";
    case napi_value__::kString: return "string";
    case napi_value__::kObject: return "object";
    case napi_value__::kArray: return "array";
    case napi_value__::kTypedArray: return "uint8array";
  }
  return "unknown";
}

napi_value get(napi_value object, const std::string& key) {
  if (object == nullptr || object->kind != napi_value__::kObject) return nullptr;
  auto it = object->object.find(key);
  return it == object->object.end() ? nullptr : it->second;
}

bool has(napi_value object, const std::string& key) {
  return object != nullptr && object->kind == napi_value__::kObject &&
         object->object.find(key) != object->object.end();
}

size_t array_size(napi_value value) {
  return value == nullptr || value->kind != napi_value__::kArray ? 0 : value->array.size();
}

napi_value element(napi_value value, size_t index) {
  if (value == nullptr || value->kind != napi_value__::kArray || index >= value->array.size()) {
    return nullptr;
  }
  return value->array[index];
}

std::string as_string(napi_value value) {
  return value == nullptr || value->kind != napi_value__::kString ? std::string() : value->str;
}

int32_t as_int32(napi_value value) {
  return value == nullptr || value->kind != napi_value__::kInt32 ? 0 : value->i32;
}

}  // namespace fake
