// Minimal fake NAPI header for host-only runtime tests of decode_napi.cpp.
// This shadows <napi/native_api.h> through the test include path.  It is NOT
// an ArkCompiler/Node implementation and must never be linked into the app.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

typedef struct napi_env__* napi_env;
typedef struct napi_value__* napi_value;
typedef struct napi_callback_info__* napi_callback_info;
typedef struct napi_handle_scope__* napi_handle_scope;

typedef enum {
  napi_ok = 0,
  napi_invalid_arg = 1,
  napi_object_expected = 2,
  napi_generic_failure = 3,
  napi_pending_exception = 4,
  napi_handle_scope_mismatch = 5,
} napi_status;

typedef enum {
  napi_undefined = 0,
  napi_null = 1,
  napi_boolean = 2,
  napi_number = 3,
  napi_string = 4,
  napi_object = 5,
} napi_valuetype;

typedef enum {
  napi_int8_array = 0,
  napi_uint8_array = 1,
  napi_uint8_clamped_array = 2,
  napi_int16_array = 3,
  napi_uint16_array = 4,
  napi_int32_array = 5,
  napi_uint32_array = 6,
  napi_float32_array = 7,
  napi_float64_array = 8,
  napi_bigint64_array = 9,
  napi_biguint64_array = 10,
} napi_typedarray_type;

struct napi_env__ {
  bool pending = false;
  std::string error;
  int scopes_open = 0;
  int scopes_closed = 0;
};

struct napi_value__ {
  enum Kind { kUndefined, kNull, kInt32, kString, kObject, kArray, kTypedArray } kind = kUndefined;
  int32_t i32 = 0;
  std::string str;
  std::map<std::string, napi_value> object;
  std::vector<napi_value> array;
  std::vector<uint8_t> bytes;
  napi_value buffer = nullptr;
  bool detached = false;
};

struct napi_callback_info__ {
  std::vector<napi_value> argv;
};

struct napi_handle_scope__ {
  int marker = 0;
};

extern "C" {

napi_status napi_get_cb_info(napi_env env, napi_callback_info info, size_t* argc,
                             napi_value* argv, napi_value* this_arg, void** data);
napi_status napi_is_typedarray(napi_env env, napi_value value, bool* result);
napi_status napi_get_typedarray_info(napi_env env, napi_value typedarray,
                                     napi_typedarray_type* type, size_t* length,
                                     void** data, napi_value* arraybuffer,
                                     size_t* byte_offset);
napi_status napi_is_detached_arraybuffer(napi_env env, napi_value value, bool* result);
napi_status napi_create_object(napi_env env, napi_value* result);
napi_status napi_create_array_with_length(napi_env env, size_t length, napi_value* result);
napi_status napi_set_element(napi_env env, napi_value object, uint32_t index, napi_value value);
napi_status napi_set_property(napi_env env, napi_value object, napi_value key, napi_value value);
napi_status napi_set_named_property(napi_env env, napi_value object, const char* utf8name,
                                    napi_value value);
napi_status napi_create_string_utf8(napi_env env, const char* str, size_t length,
                                    napi_value* result);
napi_status napi_create_int32(napi_env env, int32_t value, napi_value* result);
napi_status napi_get_null(napi_env env, napi_value* result);
napi_status napi_throw_error(napi_env env, const char* code, const char* msg);
napi_status napi_throw_type_error(napi_env env, const char* code, const char* msg);
napi_status napi_throw_range_error(napi_env env, const char* code, const char* msg);
napi_status napi_is_exception_pending(napi_env env, bool* result);
napi_status napi_open_handle_scope(napi_env env, napi_handle_scope* result);
napi_status napi_close_handle_scope(napi_env env, napi_handle_scope scope);

}  // extern "C"
