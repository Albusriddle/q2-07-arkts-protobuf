// NAPI binding: ArkTS decodeUser(bytes) -> plain object matching norm-v1.
//
// This file is intentionally a thin mechanical mapper.  The user-visible shape
// (int64 string, null profile, oneof members by contact_bits, optional level,
// last-wins map) is built/tested in src/host/decoder/object_model.{h,cpp}.
#include "decode_napi.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <string>

#include "host/common/status.h"
#include "host/decoder/decode_user.h"
#include "host/decoder/object_model.h"
#include "host/decoder/pod.h"

namespace q207 {
namespace napi {
namespace {

napi_value Throw(napi_env env, const char* code, const std::string& msg) {
  napi_throw_error(env, code, msg.c_str());
  return nullptr;
}

napi_value ThrowType(napi_env env, const char* code, const std::string& msg) {
  napi_throw_type_error(env, code, msg.c_str());
  return nullptr;
}

napi_value ThrowRange(napi_env env, const char* code, const std::string& msg) {
  napi_throw_range_error(env, code, msg.c_str());
  return nullptr;
}

napi_value MakeString(napi_env env, const std::string& s) {
  napi_value v = nullptr;
  if (napi_create_string_utf8(env, s.data(), s.size(), &v) != napi_ok) return nullptr;
  return v;
}

napi_value MakeInt32(napi_env env, int32_t n) {
  napi_value v = nullptr;
  if (napi_create_int32(env, n, &v) != napi_ok) return nullptr;
  return v;
}

bool SetNamed(napi_env env, napi_value obj, const char* key, napi_value value) {
  return napi_set_named_property(env, obj, key, value) == napi_ok;
}

bool SetKey(napi_env env, napi_value obj, const std::string& key, napi_value value) {
  napi_value k = MakeString(env, key);
  return k != nullptr && napi_set_property(env, obj, k, value) == napi_ok;
}

napi_value BuildProfile(napi_env env, const view::ProfileView& p) {
  napi_value obj = nullptr;
  if (napi_create_object(env, &obj) != napi_ok) return nullptr;
  napi_value city = MakeString(env, p.city);
  napi_value age = MakeInt32(env, p.age);
  if (city == nullptr || age == nullptr || !SetNamed(env, obj, "city", city) ||
      !SetNamed(env, obj, "age", age)) {
    return nullptr;
  }
  return obj;
}

napi_value BuildUser(napi_env env, const view::UserView& u) {
  napi_value obj = nullptr;
  if (napi_create_object(env, &obj) != napi_ok) return nullptr;

  napi_value id = MakeString(env, u.id);
  napi_value name = MakeString(env, u.name);
  if (id == nullptr || name == nullptr || !SetNamed(env, obj, "id", id) ||
      !SetNamed(env, obj, "name", name)) {
    return nullptr;
  }

  napi_value tags = nullptr;
  if (napi_create_array_with_length(env, u.tags.size(), &tags) != napi_ok) return nullptr;
  for (size_t i = 0; i < u.tags.size(); ++i) {
    napi_value tag = MakeString(env, u.tags[i]);
    if (tag == nullptr || napi_set_element(env, tags, static_cast<uint32_t>(i), tag) != napi_ok) {
      return nullptr;
    }
  }
  if (!SetNamed(env, obj, "tags", tags)) return nullptr;

  if (u.has_profile) {
    napi_value profile = BuildProfile(env, u.profile);
    if (profile == nullptr || !SetNamed(env, obj, "profile", profile)) return nullptr;
  } else {
    napi_value null_value = nullptr;
    if (napi_get_null(env, &null_value) != napi_ok || !SetNamed(env, obj, "profile", null_value)) {
      return nullptr;
    }
  }

  if (u.has_email) {
    napi_value email = MakeString(env, u.email);
    if (email == nullptr || !SetNamed(env, obj, "email", email)) return nullptr;
  }
  if (u.has_phone) {
    napi_value phone = MakeString(env, u.phone);
    if (phone == nullptr || !SetNamed(env, obj, "phone", phone)) return nullptr;
  }
  if (u.has_level) {
    napi_value level = MakeInt32(env, u.level);
    if (level == nullptr || !SetNamed(env, obj, "level", level)) return nullptr;
  }

  napi_value scores = nullptr;
  if (napi_create_object(env, &scores) != napi_ok) return nullptr;
  for (const auto& kv : u.scores) {
    napi_value value = MakeInt32(env, kv.second);
    if (value == nullptr || !SetKey(env, scores, kv.first, value)) return nullptr;
  }
  if (!SetNamed(env, obj, "scores", scores)) return nullptr;
  return obj;
}

}  // namespace

napi_value DecodeUser(napi_env env, napi_callback_info info) {
  try {
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
      return ThrowType(env, "E_ARGUMENT", "decodeUser(bytes) expects one Uint8Array");
    }

    bool is_typed = false;
    if (napi_is_typedarray(env, argv[0], &is_typed) != napi_ok || !is_typed) {
      return ThrowType(env, "E_ARGUMENT", "decodeUser(bytes) expects a Uint8Array");
    }
    napi_typedarray_type type = napi_uint8_array;
    std::size_t length = 0;
    void* data = nullptr;
    napi_value arraybuffer = nullptr;
    std::size_t byte_offset = 0;
    if (napi_get_typedarray_info(env, argv[0], &type, &length, &data, &arraybuffer,
                                 &byte_offset) != napi_ok) {
      return ThrowType(env, "E_ARGUMENT", "cannot inspect the Uint8Array");
    }
    if (type != napi_uint8_array) {
      return ThrowType(env, "E_ARGUMENT", "decodeUser(bytes) expects a Uint8Array");
    }
    bool detached = false;
    if (arraybuffer != nullptr &&
        napi_is_detached_arraybuffer(env, arraybuffer, &detached) == napi_ok && detached) {
      return ThrowType(env, "E_ARGUMENT", "the Uint8Array buffer is detached");
    }
    if (data == nullptr && length > 0) {
      return Throw(env, "E_ARGUMENT", "the Uint8Array has no readable data");
    }

    Limits limits;
    if (length > limits.max_message_bytes) {
      return ThrowRange(env, "E_MESSAGE_TOO_LARGE",
                        "decodeUser input exceeds max_message_bytes (64 MiB)");
    }

    pod::User user;
    decode::Stats stats;
    const Code code = decode::user(static_cast<const uint8_t*>(data), length, limits, &user, &stats);
    if (code != Code::Ok) {
      std::string msg = std::string(code_name(code)) + " at offset " + std::to_string(stats.error_offset);
      return Throw(env, code_name(code), msg);
    }

    view::UserView view;
    view::build_user_view(user, &view);
    napi_value result = BuildUser(env, view);
    if (result == nullptr) {
      return Throw(env, "E_NAPI", "failed to construct the decoded ArkTS object");
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Throw(env, "E_OUT_OF_MEMORY", "out of memory while decoding");
  } catch (const std::exception& e) {
    return Throw(env, "E_INTERNAL", e.what());
  } catch (...) {
    return Throw(env, "E_INTERNAL", "unknown internal error");
  }
}

}  // namespace napi
}  // namespace q207
