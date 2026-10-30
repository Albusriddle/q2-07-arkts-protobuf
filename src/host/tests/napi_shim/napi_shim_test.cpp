// Host-only runtime smoke tests for the NAPI object mapper.
// This executes app/entry/src/main/cpp/decode_napi.cpp against fake_napi.cpp,
// not against ArkCompiler.  It is intentionally separate from the 468-assertion
// host core suite so the normal host build does not depend on a device/SDK.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "decode_napi.h"
#include "fake_napi.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
}

void CheckUserShape(napi_env env, napi_value user, const char* label) {
  Check(user != nullptr, label);
  if (user == nullptr) return;
  Check(fake::type(user) == "object", "DecodeUser returns an object");
  Check(fake::type(fake::get(user, "id")) == "string", "user.id is string");
  Check(fake::type(fake::get(user, "name")) == "string", "user.name is string");
  Check(fake::type(fake::get(user, "tags")) == "array", "user.tags is array");
  Check(fake::has(user, "profile"), "user.profile is present");
  Check(fake::type(fake::get(user, "scores")) == "object", "user.scores is object");
  Check(!fake::has_exception(env), "no exception after successful DecodeUser");
}

}  // namespace

int main() {
  const char* root = ".";
  const std::vector<uint8_t> c20 = ReadFile((std::string(root) +
      "/evidence/host/corpus/c20_full.wire").c_str());
  const std::vector<uint8_t> batch = ReadFile((std::string(root) +
      "/evidence/host/batch/user_batch_c20_plus_empty.wire").c_str());
  Check(!c20.empty(), "c20 corpus loaded");
  Check(!batch.empty(), "batch corpus loaded");

  {
    napi_env env = fake::env_new();
    napi_callback_info info = fake::cb(fake::make_uint8(c20));
    napi_value out = q207::napi::DecodeUser(env, info);
    CheckUserShape(env, out, "DecodeUser(c20) returns a user");
    Check(fake::array_size(fake::get(out, "tags")) > 0, "c20 has tags");
    Check(fake::type(fake::get(out, "profile")) == "object", "c20 has profile object");
    Check(!fake::has_exception(env), "DecodeUser(c20) no exception");
    Check(env->scopes_open == env->scopes_closed, "DecodeUser balances handle scopes");
  }

  {
    napi_env env = fake::env_new();
    napi_callback_info info = fake::cb(fake::make_uint8(batch));
    napi_value out = q207::napi::DecodeMany(env, info);
    Check(fake::type(out) == "object", "DecodeMany returns object");
    napi_value users = fake::get(out, "users");
    Check(fake::type(users) == "array", "DecodeMany.users is array");
    Check(fake::array_size(users) == 2, "batch has two users");
    CheckUserShape(env, fake::element(users, 0), "batch user[0]");
    napi_value empty = fake::element(users, 1);
    Check(empty != nullptr, "batch user[1] exists");
    Check(fake::as_string(fake::get(empty, "id")) == "0", "empty user id is string 0");
    Check(fake::as_string(fake::get(empty, "name")).empty(), "empty user name is empty");
    Check(fake::array_size(fake::get(empty, "tags")) == 0, "empty user tags is []");
    Check(fake::type(fake::get(empty, "profile")) == "null", "empty user profile is null");
    Check(!fake::has_exception(env), "DecodeMany(batch) no exception");
    Check(env->scopes_open == env->scopes_closed, "DecodeMany balances handle scopes");
  }

  {
    napi_env env = fake::env_new();
    napi_callback_info info = fake::cb(fake::make_uint8({}));
    napi_value out = q207::napi::DecodeMany(env, info);
    Check(fake::type(out) == "object", "empty DecodeMany returns object");
    Check(fake::array_size(fake::get(out, "users")) == 0, "empty batch has zero users");
    Check(!fake::has_exception(env), "empty batch is valid");
  }

  {
    napi_env env = fake::env_new();
    napi_callback_info info = fake::cb(fake::make_object());
    napi_value out = q207::napi::DecodeMany(env, info);
    Check(out == nullptr, "non-typed-array returns null");
    Check(fake::has_exception(env), "non-typed-array throws");
  }

  {
    napi_env env = fake::env_new();
    napi_callback_info info = fake::cb(fake::make_uint8({0x0a}));
    napi_value out = q207::napi::DecodeMany(env, info);
    Check(out == nullptr, "truncated batch returns null");
    Check(fake::has_exception(env), "truncated batch throws");
  }

  if (failures == 0) {
    std::printf("napi shim: PASS\n");
    return 0;
  }
  std::printf("napi shim: FAIL (%d)\n", failures);
  return 1;
}
