#include <napi/native_api.h>

#include <cstddef>
#include <string>

#include "decode_napi.h"

namespace {

napi_value MakeString(napi_env env, const char* value) {
    napi_value result = nullptr;
    if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &result) != napi_ok) {
        return nullptr;
    }
    return result;
}

bool IsNumber(napi_env env, napi_value value) {
    napi_valuetype type = napi_undefined;
    return napi_typeof(env, value, &type) == napi_ok && type == napi_number;
}

napi_value Ping(napi_env env, napi_callback_info /*info*/) {
    return MakeString(env, "pong");
}

napi_value Add(napi_env env, napi_callback_info info) {
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok) {
        return nullptr;
    }
    if (argc < 2 || !IsNumber(env, argv[0]) || !IsNumber(env, argv[1])) {
        napi_throw_type_error(env, nullptr, "add(a, b) expects two numbers");
        return nullptr;
    }

    double a = 0.0;
    double b = 0.0;
    if (napi_get_value_double(env, argv[0], &a) != napi_ok ||
        napi_get_value_double(env, argv[1], &b) != napi_ok) {
        napi_throw_type_error(env, nullptr, "add(a, b) expects two numbers");
        return nullptr;
    }

    napi_value result = nullptr;
    if (napi_create_double(env, a + b, &result) != napi_ok) {
        return nullptr;
    }
    return result;
}

napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"ping", nullptr, Ping, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"add", nullptr, Add, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"decodeUser", nullptr, q207::napi::DecodeUser, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"decodeMany", nullptr, q207::napi::DecodeMany, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    if (napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc) != napi_ok) {
        return nullptr;
    }
    return exports;
}

}  // namespace

static napi_module entryModule = {
    1,          // nm_version
    0,          // nm_flags
    nullptr,    // nm_filename
    Init,       // nm_register_func
    "entry",    // nm_modname
    nullptr,    // nm_priv
    {0},        // reserved
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) {
    napi_module_register(&entryModule);
}
