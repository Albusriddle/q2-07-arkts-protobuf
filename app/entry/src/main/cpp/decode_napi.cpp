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

Limits NapiLimits() {
  Limits l;
  // NAPI is a separate trust boundary from the host tools: lower the batch cap
  // and per-call field/repeated budgets.  max_objects still counts batch+User+
  // Profile+MapEntry, so effective user count can be lower.
  l.max_batch_users = 100000;
  l.max_fields = 2000000;
  l.max_repeated_elements = 100000;
  return l;
}

bool ReadInput(napi_env env, napi_value value, const uint8_t** data, std::size_t* length) {
  bool is_typed = false;
  if (napi_is_typedarray(env, value, &is_typed) != napi_ok || !is_typed) {
    ThrowType(env, "E_ARGUMENT", "bytes must be a Uint8Array");
    return false;
  }
  napi_typedarray_type type = napi_uint8_array;
  void* raw = nullptr;
  napi_value arraybuffer = nullptr;
  std::size_t byte_offset = 0;
  if (napi_get_typedarray_info(env, value, &type, length, &raw, &arraybuffer,
                               &byte_offset) != napi_ok) {
    ThrowType(env, "E_ARGUMENT", "cannot inspect the Uint8Array");
    return false;
  }
  if (type != napi_uint8_array) {
    ThrowType(env, "E_ARGUMENT", "bytes must be a Uint8Array");
    return false;
  }
  if (arraybuffer == nullptr) {
    ThrowType(env, "E_ARGUMENT", "the Uint8Array has no ArrayBuffer");
    return false;
  }
  bool detached = false;
  if (napi_is_detached_arraybuffer(env, arraybuffer, &detached) != napi_ok) {
    ThrowType(env, "E_ARGUMENT", "cannot inspect the Uint8Array buffer");
    return false;
  }
  if (detached) {
    ThrowType(env, "E_ARGUMENT", "the Uint8Array buffer is detached");
    return false;
  }
  if (raw == nullptr && *length > 0) {
    Throw(env, "E_ARGUMENT", "the Uint8Array has no readable data");
    return false;
  }
  *data = static_cast<const uint8_t*>(raw);
  return true;
}

bool IsRangeCode(Code c) {
  return c == Code::MessageTooLarge || c == Code::ObjectBudget ||
         c == Code::BatchTooLarge || c == Code::FieldBudget ||
         c == Code::FieldTooLarge || c == Code::Depth;
}

class ScopeGuard {
 public:
  ScopeGuard(napi_env env, napi_handle_scope scope) : env_(env), scope_(scope) {}
  ~ScopeGuard() {
    if (scope_ != nullptr) {
      // Destructors must not throw; a mismatch is an internal bug and the outer
      // callback will still fail closed.
      (void)napi_close_handle_scope(env_, scope_);
    }
  }
  napi_status Close() {
    if (scope_ == nullptr) return napi_handle_scope_mismatch;
    napi_status st = napi_close_handle_scope(env_, scope_);
    scope_ = nullptr;
    return st;
  }
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;

 private:
  napi_env env_;
  napi_handle_scope scope_;
};

napi_value ThrowCode(napi_env env, Code code, const decode::Stats& stats, const char* prefix) {
  std::string msg = std::string(prefix) + ": " + code_name(code) +
                    " at batch body offset " + std::to_string(stats.error_offset);
  if (IsRangeCode(code)) return ThrowRange(env, code_name(code), msg);
  return Throw(env, code_name(code), msg);
}

}  // namespace

napi_value DecodeUser(napi_env env, napi_callback_info info) {
  try {
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
      return ThrowType(env, "E_ARGUMENT", "decodeUser(bytes) expects one Uint8Array");
    }

    const uint8_t* data = nullptr;
    std::size_t length = 0;
    if (!ReadInput(env, argv[0], &data, &length)) return nullptr;

    Limits limits = NapiLimits();
    if (length > limits.max_message_bytes) {
      return ThrowRange(env, "E_MESSAGE_TOO_LARGE",
                        "decodeUser input exceeds max_message_bytes (64 MiB)");
    }

    pod::User user;
    decode::Stats stats;
    const Code code = decode::user(data, length, limits, &user, &stats);
    if (code != Code::Ok) return ThrowCode(env, code, stats, "decodeUser failed");

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

napi_value DecodeMany(napi_env env, napi_callback_info info) {
  try {
    std::size_t argc = 1;
    napi_value argv[1] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
      return ThrowType(env, "E_ARGUMENT", "decodeMany(bytes) expects one Uint8Array (one UserBatch body)");
    }

    const uint8_t* data = nullptr;
    std::size_t length = 0;
    if (!ReadInput(env, argv[0], &data, &length)) return nullptr;

    Limits limits = NapiLimits();
    if (length > limits.max_message_bytes) {
      return ThrowRange(env, "E_MESSAGE_TOO_LARGE",
                        "decodeMany input exceeds max_message_bytes (64 MiB)");
    }

    pod::UserBatch batch;
    decode::Stats stats;
    const Code code = decode::user_batch(data, length, limits, &batch, &stats);
    if (code != Code::Ok) return ThrowCode(env, code, stats, "decodeMany failed");
    if (batch.users.size() > limits.max_batch_users ||
        batch.users.size() > 0xFFFFFFFFu) {
      return ThrowRange(env, "E_BATCH_TOO_LARGE", "decodeMany user count exceeds the NAPI limit");
    }

    view::BatchView views;
    view::build_batch_view(batch, &views);

    napi_value users = nullptr;
    if (napi_create_array_with_length(env, views.users.size(), &users) != napi_ok) {
      return Throw(env, "E_NAPI", "failed to create the decoded users array");
    }
    for (std::size_t i = 0; i < views.users.size(); ++i) {
      napi_handle_scope scope = nullptr;
      if (napi_open_handle_scope(env, &scope) != napi_ok) {
        return Throw(env, "E_NAPI", "failed to open a NAPI handle scope");
      }
      ScopeGuard guard(env, scope);
      napi_value obj = BuildUser(env, views.users[i]);
      const bool set_ok = obj != nullptr &&
                          napi_set_element(env, users, static_cast<uint32_t>(i), obj) == napi_ok;
      const napi_status close_status = guard.Close();
      if (!set_ok) {
        bool pending = false;
        if (napi_is_exception_pending(env, &pending) == napi_ok && pending) return nullptr;
        return Throw(env, "E_NAPI", "failed to append a decoded user");
      }
      if (close_status != napi_ok) {
        return Throw(env, "E_NAPI", "failed to close a NAPI handle scope");
      }
    }

    napi_value result = nullptr;
    if (napi_create_object(env, &result) != napi_ok ||
        napi_set_named_property(env, result, "users", users) != napi_ok) {
      return Throw(env, "E_NAPI", "failed to wrap the decoded users");
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Throw(env, "E_OUT_OF_MEMORY", "out of memory while decoding a batch");
  } catch (const std::exception& e) {
    return Throw(env, "E_INTERNAL", e.what());
  } catch (...) {
    return Throw(env, "E_INTERNAL", "unknown internal error");
  }
}

}  // namespace napi
}  // namespace q207
