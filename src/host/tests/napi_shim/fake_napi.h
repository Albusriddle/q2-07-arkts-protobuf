#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <napi/native_api.h>

namespace fake {

napi_env env_new();
napi_value make_uint8(const std::vector<uint8_t>& bytes);
napi_value make_object();
napi_callback_info cb(napi_value arg);
bool has_exception(napi_env env);
std::string exception(napi_env env);
std::string type(napi_value value);
napi_value get(napi_value object, const std::string& key);
bool has(napi_value object, const std::string& key);
size_t array_size(napi_value value);
napi_value element(napi_value value, size_t index);
std::string as_string(napi_value value);
int32_t as_int32(napi_value value);

}  // namespace fake
