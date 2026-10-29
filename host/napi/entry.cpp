#include <napi/native_api.h>

#include <cstddef>
#include <string>

namespace {

napi_value MakeString(napi_env env, const char* value) {
    napi_value result = nullptr;
    napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &result);
    return result;
}

napi_value Ping(napi_env env, napi_callback_info /*info*/) {
    return MakeString(env, "pong");
}

napi_value Add(napi_env env, napi_callback_info info) {
    std::size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    double a = 0.0;
    double b = 0.0;
    if (argc >= 1) {
        napi_get_value_double(env, argv[0], &a);
    }
    if (argc >= 2) {
        napi_get_value_double(env, argv[1], &b);
    }

    napi_value result = nullptr;
    napi_create_double(env, a + b, &result);
    return result;
}

napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"ping", nullptr, Ping, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"add", nullptr, Add, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
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
