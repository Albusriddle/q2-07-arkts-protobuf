// HOST-ONLY: decode proto/example.proto into POD + canonical norm-v1 dump.
#include <string>
#include <vector>

#include "host/decoder/decode_user.h"
#include "host/decoder/norm.h"
#include "host/decoder/pod.h"
#include "host/tests/test_framework.h"

namespace q207 {
namespace test {
namespace {

pod::User decode_user_hex(const char* h, Code* code = nullptr, Limits lim = Limits()) {
  std::vector<uint8_t> b = unhex(h);
  pod::User u;
  decode::Stats st;
  Code e = decode::user(b.data(), b.size(), lim, &u, &st);
  if (code) *code = e;
  return u;
}

// The full mixed-schema User used by the differential corpus.
// id=1 | name="alice" | tags=["a","b"] | profile{city:"X",age:3} |
// email="e@x" | phone="123" | level=0 | scores{k:1, k:2}
const char* kFullUserHex =
    "0801"
    "1205616c696365"
    "1a0161"
    "1a0162"
    "22050a01581003"
    "2a03654078"
    "3203313233"
    "3800"
    "42050a016b1001"
    "42050a016b1002";

}  // namespace

void suite_decode() {
  ctx().begin("decode/empty-and-defaults");
  {
    Code code = Code::Internal;
    pod::User u = decode_user_hex("", &code);
    TEQ_CODE(code, Code::Ok);
    TEQ_U64(u.presence, 0);
    TEQ_I64(static_cast<long long>(u.id), 0);
    TEQ_STR(u.name, std::string());
    TCHECK(u.tags.empty());
    TCHECK(!u.profile.present);
    TCHECK(!u.has_level);
    TCHECK(u.scores.empty());
    TEQ_STR(norm::user_json(u),
            std::string("{\"id\":\"0\",\"name\":\"\",\"tags\":[],\"profile\":null,\"scores\":{}}"));
  }

  ctx().begin("decode/full-mixed-schema");
  {
    Code code = Code::Internal;
    pod::User u = decode_user_hex(kFullUserHex, &code);
    TEQ_CODE(code, Code::Ok);
    TEQ_I64(static_cast<long long>(u.id), 1);
    TEQ_STR(u.name, std::string("alice"));
    TEQ_U64(u.tags.size(), 2);
    TEQ_STR(u.tags[0], std::string("a"));
    TEQ_STR(u.tags[1], std::string("b"));
    TCHECK(u.profile.present);
    TEQ_STR(u.profile.city, std::string("X"));
    TEQ_I64(u.profile.age, 3);
    // oneof keep-all: both members retained, last-wins case recorded
    TEQ_U64(u.contact_bits, (1u << 5) | (1u << 6));
    TEQ_U64(u.oneof_case, 6);
    TEQ_STR(u.email, std::string("e@x"));
    TEQ_STR(u.phone, std::string("123"));
    // proto3 optional explicit zero is NOT the same as absent
    TCHECK(u.has_level);
    TEQ_I64(u.level, 0);
    TCHECK(u.present(pod::f::kUserLevel));
    // raw map entries kept in wire order (duplicate key)
    TEQ_U64(u.scores.size(), 2);
    TEQ_STR(u.scores[0].key, std::string("k"));
    TEQ_I64(u.scores[0].value, 1);
    TEQ_I64(u.scores[1].value, 2);
    // presence bitmap covers exactly the fields that appeared
    TEQ_U64(u.presence, 0xFFu);
    // norm dump applies map last-wins (protobufjs object semantics)
    const std::string want =
        "{\"id\":\"1\",\"name\":\"alice\",\"tags\":[\"a\",\"b\"],"
        "\"profile\":{\"city\":\"X\",\"age\":3},"
        "\"email\":\"e@x\",\"phone\":\"123\",\"level\":0,\"scores\":{\"k\":2}}";
    TEQ_STR(norm::user_json(u), want);
  }

  ctx().begin("decode/optional-tri-state");
  {
    // absent
    pod::User a = decode_user_hex("0801");
    TCHECK(!a.has_level);
    TCHECK(norm::user_json(a).find("level") == std::string::npos);
    // present with 0
    pod::User b = decode_user_hex("08013800");
    TCHECK(b.has_level);
    TEQ_I64(b.level, 0);
    TCHECK(norm::user_json(b).find("\"level\":0") != std::string::npos);
    // present with a negative value (10-byte varint)
    pod::User c = decode_user_hex("38ffffffffffffffffff01");
    TCHECK(c.has_level);
    TEQ_I64(c.level, -1);
    // absent vs present-empty profile
    pod::User d = decode_user_hex("2200");
    TCHECK(d.profile.present);
    TEQ_STR(d.profile.city, std::string());
    TEQ_STR(norm::user_json(d).substr(norm::user_json(d).find("\"profile\"")),
            std::string("\"profile\":{\"city\":\"\",\"age\":0},\"scores\":{}}"));
    // oneof member absent -> key omitted
    TCHECK(norm::user_json(a).find("email") == std::string::npos);
  }

  ctx().begin("decode/unknown-and-mismatch");
  {
    // unknown field 9 varint + unknown field 10 length-delimited
    pod::User u = decode_user_hex("080148075203010203");
    TEQ_U64(u.unknown_fields, 2);
    TEQ_I64(static_cast<long long>(u.id), 1);
    // known field with the wrong wire type: strict fails ...
    Code code = Code::Internal;
    decode_user_hex("1005", &code);  // field 2 (string) sent as varint
    TEQ_CODE(code, Code::WireTypeMismatch);
    // ... compat skips it
    Limits lax;
    lax.strict_wire_type = false;
    pod::User c = decode_user_hex("1005", &code, lax);
    TEQ_CODE(code, Code::Ok);
    TEQ_U64(c.unknown_fields, 1);
  }

  ctx().begin("decode/map-and-oneof-edges");
  {
    // dangerous map key is rejected by policy
    Code code = Code::Internal;
    decode_user_hex("420d0a095f5f70726f746f5f5f1001", &code);
    TEQ_CODE(code, Code::MapKeyDangerous);
    Limits no_reject;
    no_reject.map_reject_dangerous = false;
    pod::User u = decode_user_hex("420d0a095f5f70726f746f5f5f1001", &code, no_reject);
    TEQ_CODE(code, Code::Ok);
    TEQ_U64(u.scores.size(), 1);
    TEQ_STR(u.scores[0].key, std::string("__proto__"));
    // map entry with value before key (order must not matter)
    pod::User v = decode_user_hex("420510010a0161");
    TEQ_U64(v.scores.size(), 1);
    TEQ_STR(v.scores[0].key, std::string("a"));
    TEQ_I64(v.scores[0].value, 1);
    // oneof: phone then email -> case is the last one, both members kept
    pod::User w = decode_user_hex("2a0165320331323333");
    TEQ_U64(w.oneof_case, 6);
    TEQ_U64(w.contact_bits, (1u << 5) | (1u << 6));
  }

  ctx().begin("decode/utf8-and-limits");
  {
    Code code = Code::Internal;
    decode_user_hex("1202c080", &code);  // overlong
    TEQ_CODE(code, Code::Utf8Invalid);
    Limits lax;
    lax.utf8_strict = false;
    pod::User u = decode_user_hex("1202c080", &code, lax);
    TEQ_CODE(code, Code::Ok);
    TEQ_U64(u.name.size(), 2);

    // message larger than the configured single-message cap
    Limits tiny;
    tiny.max_message_bytes = 4;
    pod::User v = decode_user_hex("1205616c696365", &code, tiny);
    TEQ_CODE(code, Code::MessageTooLarge);
    (void)v;
    // string longer than max_string_bytes
    Limits s8;
    s8.max_string_bytes = 2;
    decode_user_hex("1205616c696365", &code, s8);
    TEQ_CODE(code, Code::LengthExceedsCap);
    // truncated length-delimited payload
    decode_user_hex("12056162", &code);
    TEQ_CODE(code, Code::LengthExceedsRemaining);
    // truncated nested message
    decode_user_hex("22020a01", &code);
    TEQ_CODE(code, Code::LengthExceedsRemaining);
    // trailing garbage after a complete field is not silently accepted
    decode_user_hex("0801ff", &code);
    TCHECK(code != Code::Ok);
  }

  ctx().begin("decode/user-batch");
  {
    // two users inside UserBatch
    std::string hexBatch = "0a09" "08011205616c696365";  // user1: id=1, name="alice" (9 B)
    hexBatch += "0a0a" "0802" "1203626f62" "1a0178";    // user2: id=2, name="bob", tags=["x"]
    std::vector<uint8_t> b = unhex(hexBatch.c_str());
    // user1 body: 08 01 12 05 61 6c 69 63 65 -> 9 bytes
    // user2 body: 08 02 12 03 62 6f 62 1a 01 78 -> 10 bytes
    pod::UserBatch batch;
    decode::Stats st;
    Code e = decode::user_batch(b.data(), b.size(), Limits(), &batch, &st);
    TEQ_CODE(e, Code::Ok);
    TEQ_U64(batch.users.size(), 2);
    if (batch.users.size() != 2) {
      std::fprintf(stderr, "  batch users=%zu\n", batch.users.size());
    } else {
      TEQ_I64(static_cast<long long>(batch.users[0].id), 1);
      TEQ_STR(batch.users[0].name, std::string("alice"));
      TEQ_I64(static_cast<long long>(batch.users[1].id), 2);
      TEQ_STR(batch.users[1].name, std::string("bob"));
      TEQ_U64(batch.users[1].tags.size(), 1);
      TEQ_STR(batch.users[1].tags[0], std::string("x"));
    }
    TEQ_U64(st.objects, 3);  // batch + 2 users
    const std::string want =
        "{\"users\":[{\"id\":\"1\",\"name\":\"alice\",\"tags\":[],\"profile\":null,\"scores\":{}},"
        "{\"id\":\"2\",\"name\":\"bob\",\"tags\":[\"x\"],\"profile\":null,\"scores\":{}}]}";
    TEQ_STR(norm::user_batch_json(batch), want);
  }

  ctx().begin("decode/json-escape");
  {
    pod::User u;
    u.name = "a\"b\\c\n\t\x01";
    u.mark(pod::f::kUserName);
    TEQ_STR(norm::json_escape(u.name), std::string("a\\\"b\\\\c\\n\\t\\u0001"));
    u.name = "\xe4\xbd\xa0\xe5\xa5\xbd";  // 你好, valid UTF-8 passthrough
    TEQ_STR(norm::json_escape(u.name), std::string("\xe4\xbd\xa0\xe5\xa5\xbd"));
  }

  ctx().begin("decode/int64-boundaries");
  {
    // int64 min / max via wire bytes produced by protobufjs 7.6.5
    pod::User lo = decode_user_hex("0880808080808080808001");
    TEQ_I64(static_cast<long long>(lo.id), -9223372036854775807LL - 1);
    pod::User hi = decode_user_hex("08ffffffffffffffff7f");
    TEQ_I64(static_cast<long long>(hi.id), 9223372036854775807LL);
    TCHECK(norm::user_json(lo).find("\"id\":\"-9223372036854775808\"") != std::string::npos);
  }
}

}  // namespace test
}  // namespace q207
