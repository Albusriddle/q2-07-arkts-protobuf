#pragma once

#include <napi/native_api.h>

namespace q207 {
namespace napi {

// ArkTS: decodeUser(bytes: Uint8Array): DecodedUser
napi_value DecodeUser(napi_env env, napi_callback_info info);

// ArkTS: decodeMany(bytes: Uint8Array): { users: DecodedUser[] }
napi_value DecodeMany(napi_env env, napi_callback_info info);

}  // namespace napi
}  // namespace q207
