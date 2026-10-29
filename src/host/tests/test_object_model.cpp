// HOST-ONLY: object-model tests (POD -> UserView -> canonical JSON).
#include <cstdint>
#include <string>
#include <vector>

#include "host/decoder/decode_user.h"
#include "host/decoder/object_model.h"
#include "host/decoder/pod.h"
#include "host/tests/test_framework.h"

namespace q207 {
namespace test {
namespace {

std::string decode_view_json(const char* hex) {
  std::vector<uint8_t> bytes = unhex(hex);
  pod::User u;
  decode::Stats st;
  Code code = decode::user(bytes.data(), bytes.size(), Limits(), &u, &st);
  TCHECK(code == Code::Ok);
  view::UserView v;
  view::build_user_view(u, &v);
  return view::user_view_json(v);
}

}  // namespace

void suite_object_model() {
  ctx().begin("object_model/defaults");
  {
    pod::User u;
    view::UserView v;
    view::build_user_view(u, &v);
    TEQ_STR(view::user_view_json(v),
            std::string("{\"id\":\"0\",\"name\":\"\",\"tags\":[],\"profile\":null,\"scores\":{}}"));
  }

  ctx().begin("object_model/c20-full");
  {
    // c20_full.wire: id=2^53+1, name, tags, profile, email, level=7, 2 scores.
    const std::string got = decode_view_json(
        "0881808080808080101205616c6963651a01611a016222050a015810032a036540783807"
        "42060a026b31100142060a026b321002");
    TEQ_STR(got,
            std::string("{\"id\":\"9007199254740993\",\"name\":\"alice\",\"tags\":[\"a\",\"b\"],"
                        "\"profile\":{\"city\":\"X\",\"age\":3},\"email\":\"e@x\",\"level\":7,"
                        "\"scores\":{\"k1\":1,\"k2\":2}}"));
  }

  ctx().begin("object_model/oneof-both");
  {
    pod::User u;
    u.contact_bits = (1u << pod::f::kUserEmail) | (1u << pod::f::kUserPhone);
    u.email = "e@x";
    u.phone = "123";
    u.oneof_case = pod::f::kUserPhone;
    view::UserView v;
    view::build_user_view(u, &v);
    TEQ_STR(view::user_view_json(v),
            std::string("{\"id\":\"0\",\"name\":\"\",\"tags\":[],\"profile\":null,"
                        "\"email\":\"e@x\",\"phone\":\"123\",\"scores\":{}}"));
  }

  ctx().begin("object_model/map-last-wins-order");
  {
    pod::User u;
    u.scores = {{"10", 10}, {"2", 2}, {"x", 1}, {"1", 1}, {"x", 9}};
    view::UserView v;
    view::build_user_view(u, &v);
    TEQ_STR(view::user_view_json(v),
            std::string("{\"id\":\"0\",\"name\":\"\",\"tags\":[],\"profile\":null,"
                        "\"scores\":{\"1\":1,\"2\":2,\"10\":10,\"x\":9}}"));
  }

  ctx().begin("object_model/int64-min-nul-string");
  {
    pod::User u;
    u.id = INT64_MIN;
    u.name = std::string("a\0b", 3);
    view::UserView v;
    view::build_user_view(u, &v);
    TEQ_STR(v.id, std::string("-9223372036854775808"));
    TEQ_U64(v.name.size(), 3);
    TEQ_STR(view::user_view_json(v),
            std::string("{\"id\":\"-9223372036854775808\",\"name\":\"a\\u0000b\",\"tags\":[],"
                        "\"profile\":null,\"scores\":{}}"));
  }
}

}  // namespace test
}  // namespace q207
