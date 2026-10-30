// HOST-ONLY: tape round-trip, determinism and the verifier's negative cases.
#include <string>
#include <vector>

#include "host/decoder/pod.h"
#include "host/tape/tape.h"
#include "host/tests/test_framework.h"

namespace q207 {
namespace test {
namespace {

using tape::kHeaderBytes;
using tape::kProfileRecordSize;
using tape::kUserRecordSize;

void put_u32(std::vector<uint8_t>* v, size_t off, uint32_t x) {
  (*v)[off + 0] = static_cast<uint8_t>(x);
  (*v)[off + 1] = static_cast<uint8_t>(x >> 8);
  (*v)[off + 2] = static_cast<uint8_t>(x >> 16);
  (*v)[off + 3] = static_cast<uint8_t>(x >> 24);
}
uint32_t get_u32(const std::vector<uint8_t>& v, size_t off) {
  return static_cast<uint32_t>(v[off]) | (static_cast<uint32_t>(v[off + 1]) << 8) |
         (static_cast<uint32_t>(v[off + 2]) << 16) | (static_cast<uint32_t>(v[off + 3]) << 24);
}

// Recomputes the section crc so a mutation can reach the checks *after* crc32.
void refresh_crc(std::vector<uint8_t>* v) {
  tape::Header h = tape::parse_header(v->data(), v->size());
  put_u32(v, 64, tape::section_crc(v->data(), v->size(), h));
}

bool verify_fails_with(std::vector<uint8_t> buf, const char* expect_prefix) {
  tape::VerifyReport rep = tape::verify(buf.data(), buf.size());
  if (rep.ok) {
    std::fprintf(stderr, "  expected failure %s but tape verified\n", expect_prefix);
    return false;
  }
  const std::string e = rep.error;
  if (e.rfind(expect_prefix, 0) != 0) {
    std::fprintf(stderr, "  expected %s, got %s\n", expect_prefix, e.c_str());
    return false;
  }
  return true;
}

pod::User make_full_user() {
  pod::User u;
  u.id = 42;
  u.name = "alice";
  u.tags = {"a", "bb", ""};
  u.profile.present = true;
  u.profile.city = "Hangzhou";
  u.profile.age = 30;
  u.mark(pod::f::kUserId);
  u.mark(pod::f::kUserName);
  u.mark(pod::f::kUserTags);
  u.mark(pod::f::kUserProfile);
  u.contact_bits = (1u << pod::f::kUserEmail);
  u.email = "alice@example.com";
  u.mark(pod::f::kUserEmail);
  u.oneof_case = pod::f::kUserEmail;
  u.has_level = true;
  u.level = 0;  // explicit zero must survive the tape
  u.mark(pod::f::kUserLevel);
  u.scores = {{"math", 95}, {"art", -1}, {"math", 99}};  // dup key kept raw
  u.mark(pod::f::kUserScores);
  return u;
}

pod::User make_min_user() {
  pod::User u;
  u.mark(pod::f::kUserId);
  return u;
}

}  // namespace

void suite_tape() {
  ctx().begin("tape/roundtrip");
  {
    std::vector<pod::User> src = {make_full_user(), make_min_user()};
    tape::Writer w;
    for (const pod::User& u : src) w.add_user(u);
    std::vector<uint8_t> buf = w.finish();

    tape::VerifyReport rep = tape::verify(buf.data(), buf.size());
    TCHECK(rep.ok);
    if (!rep.ok) std::fprintf(stderr, "  verify: %s\n", rep.error.c_str());
    TEQ_U64(rep.header.n_user_records, 2);
    TEQ_U64(rep.header.n_profile_records, 1);
    TEQ_U64(rep.header.user_record_size, kUserRecordSize);
    TEQ_U64(rep.header.profile_record_size, kProfileRecordSize);
    TEQ_U64(rep.header.roots_count, 2);
    TEQ_U64(rep.header.tape_bytes, 2 * kUserRecordSize + kProfileRecordSize);
    TEQ_U64(rep.users_seen, 2);
    TEQ_U64(rep.profiles_seen, 1);
    TCHECK((rep.header.tape_off % 8) == 0);
    TCHECK((rep.header.blob_off % 8) == 0);
    TCHECK((rep.header.values_off % 8) == 0);

    tape::Reader r(buf.data(), buf.size());
    TEQ_CODE(r.load(), Code::Ok);
    pod::UserBatch back;
    TEQ_CODE(r.batch(&back), Code::Ok);
    TEQ_U64(back.users.size(), src.size());
    for (size_t i = 0; i < back.users.size() && i < src.size(); ++i) {
      const pod::User& a = back.users[i];
      const pod::User& b = src[i];
      TEQ_U64(a.presence, b.presence);
      TEQ_I64(static_cast<long long>(a.id), static_cast<long long>(b.id));
      TEQ_STR(a.name, b.name);
      TEQ_U64(a.tags.size(), b.tags.size());
      for (size_t t = 0; t < a.tags.size() && t < b.tags.size(); ++t) TEQ_STR(a.tags[t], b.tags[t]);
      TCHECK(a.profile.present == b.profile.present);
      TEQ_STR(a.profile.city, b.profile.city);
      TEQ_I64(a.profile.age, b.profile.age);
      TEQ_U64(a.contact_bits, b.contact_bits);
      TEQ_U64(a.oneof_case, b.oneof_case);
      TEQ_STR(a.email, b.email);
      TEQ_STR(a.phone, b.phone);
      TCHECK(a.has_level == b.has_level);
      TEQ_I64(a.level, b.level);
      TEQ_U64(a.scores.size(), b.scores.size());
      for (size_t s = 0; s < a.scores.size() && s < b.scores.size(); ++s) {
        TEQ_STR(a.scores[s].key, b.scores[s].key);
        TEQ_I64(a.scores[s].value, b.scores[s].value);
      }
      TEQ_U64(a.unknown_fields, b.unknown_fields);
    }
    // The explicit zero (level=0) and the empty tag must both survive verbatim.
    TEQ_I64(back.users[0].level, 0);
    TCHECK(back.users[0].has_level);
    TEQ_STR(back.users[0].tags[2], std::string());
    // Out-of-range roots must fail closed, not read past the roots section.
    TEQ_U64(r.root_at(2), tape::kAbsent);
  }

  ctx().begin("tape/writer-budget");
  {
    Limits tiny;
    tiny.max_tape_bytes = 80;  // less than one full user+profile record
    tape::Writer w(tape::kFlagOneofKeepAll, tiny);
    TEQ_U64(w.add_user(make_full_user()), tape::kAbsent);
    TCHECK(!w.ok());
    TCHECK(w.finish().empty());
  }

  ctx().begin("tape/determinism");
  {
    auto build = []() {
      tape::Writer w;
      w.add_user(make_full_user());
      w.add_user(make_min_user());
      return w.finish();
    };
    std::vector<uint8_t> a = build();
    std::vector<uint8_t> b = build();
    TEQ_STR(hex(a), hex(b));
    tape::Writer w2;
    w2.add_user(make_full_user());
    w2.add_user(make_min_user());
    std::vector<uint8_t> c1 = w2.finish();
    std::vector<uint8_t> c2 = w2.finish();
    TEQ_STR(hex(c1), hex(c2));
    TEQ_STR(hex(a), hex(c1));
    TEQ_U64(tape::crc32(a.data(), a.size()), tape::crc32(b.data(), b.size()));
  }

  ctx().begin("tape/verifier-negatives");
  {
    std::vector<uint8_t> good;
    {
      tape::Writer w;
      w.add_user(make_full_user());
      w.add_user(make_min_user());
      good = w.finish();
    }
    {
      // roots section may not overlap the tape section
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      put_u32(&m, 40, h.roots_off);  // tape_off = roots_off -> overlap
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_SECTION_ORDER_ROOTS_TAPE"));
    }
    {
      // tape section may not overlap the blob section
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      put_u32(&m, 48, h.tape_off);  // blob_off = tape_off -> overlap
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_SECTION_ORDER_TAPE_BLOB"));
    }
    TCHECK(verify_fails_with(std::vector<uint8_t>(good.begin(), good.begin() + 8), "E_TAPE_SIZE"));
    {
      std::vector<uint8_t> m = good;
      m[0] = 'X';
      TCHECK(verify_fails_with(m, "E_TAPE_MAGIC"));
    }
    {
      std::vector<uint8_t> m = good;
      put_u32(&m, 8, 99);
      TCHECK(verify_fails_with(m, "E_TAPE_VERSION"));
    }
    {
      std::vector<uint8_t> m = good;
      put_u32(&m, 16, 72);  // user record size
      TCHECK(verify_fails_with(m, "E_TAPE_USER_RECORD_SIZE"));
    }
    {
      std::vector<uint8_t> m = good;
      tape::Header hh = tape::parse_header(m.data(), m.size());
      m[hh.tape_off + 1] ^= 0xFF;  // corrupt the tape section (covered by crc32)
      TCHECK(verify_fails_with(m, "E_TAPE_CRC32"));
    }
    {
      // unaligned root offset
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      put_u32(&m, h.roots_off + 4, 4);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_ROOT_ALIGN"));
    }
    {
      // root offset outside the tape region
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      put_u32(&m, h.roots_off, 4096);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_ROOT_RANGE"));
    }
    {
      // presence bits for fields that do not exist
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      const uint32_t rec = get_u32(m, h.roots_off);
      put_u32(&m, h.tape_off + rec, 0x0000FF00u);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_PRESENCE_BITS"));
    }
    {
      // oneof bits outside {5,6}
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      const uint32_t rec = get_u32(m, h.roots_off);
      put_u32(&m, h.tape_off + rec + 4, 0xFFFFFFFFu);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_ONEOF_BITS"));
    }
    {
      // blob reference past the end of the blob section
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      const uint32_t rec = get_u32(m, h.roots_off);
      put_u32(&m, h.tape_off + rec + 28, 0x00FFFFFFu);  // name_len
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_BLOB_REF"));
    }
    {
      // tag count larger than the values section can hold
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      const uint32_t rec = get_u32(m, h.roots_off);
      put_u32(&m, h.tape_off + rec + 32, 100000u);  // tags_count
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_TAGS_RANGE"));
    }
    {
      // unaligned profile pointer
      std::vector<uint8_t> m = good;
      tape::Header h = tape::parse_header(m.data(), m.size());
      const uint32_t rec = get_u32(m, h.roots_off);
      put_u32(&m, h.tape_off + rec + 40, get_u32(m, h.tape_off + rec + 40) + 4);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_PROFILE_ALIGN"));
    }
    {
      // record count lied about
      std::vector<uint8_t> m = good;
      put_u32(&m, 24, 7);
      refresh_crc(&m);
      TCHECK(verify_fails_with(m, "E_TAPE_USER_COUNT"));
    }
    {
      // section offset no longer 8-aligned
      std::vector<uint8_t> m = good;
      put_u32(&m, 40, get_u32(m, 40) + 4);
      TCHECK(verify_fails_with(m, "E_TAPE_TAPE_OFF_ALIGN"));
    }
  }

  ctx().begin("tape/empty-and-large");
  {
    tape::Writer w;
    std::vector<uint8_t> empty = w.finish();
    tape::VerifyReport rep = tape::verify(empty.data(), empty.size());
    TCHECK(rep.ok);
    TEQ_U64(rep.header.n_user_records, 0);
    TEQ_U64(rep.header.roots_count, 0);
    TCHECK(verify_fails_with(std::vector<uint8_t>(), "E_TAPE_SIZE"));

    // 2000 records: layout must stay 8-aligned and verifiable.
    tape::Writer w2;
    for (int i = 0; i < 2000; ++i) {
      pod::User u;
      u.id = i;
      u.name = "u" + std::to_string(i);
      u.tags.push_back(u.name);
      u.mark(pod::f::kUserId);
      u.mark(pod::f::kUserName);
      u.mark(pod::f::kUserTags);
      if (i % 3 == 0) {
        u.profile.present = true;
        u.profile.city = "c";
        u.profile.age = i;
        u.mark(pod::f::kUserProfile);
      }
      w2.add_user(u);
    }
    std::vector<uint8_t> buf = w2.finish();
    tape::VerifyReport rep2 = tape::verify(buf.data(), buf.size());
    TCHECK(rep2.ok);
    TEQ_U64(rep2.users_seen, 2000);
    TEQ_U64(rep2.profiles_seen, 667);
    tape::Reader r(buf.data(), buf.size());
    TEQ_CODE(r.load(), Code::Ok);
    pod::UserBatch batch;
    TEQ_CODE(r.batch(&batch), Code::Ok);
    TEQ_U64(batch.users.size(), 2000);
    TEQ_I64(static_cast<long long>(batch.users[1999].id), 1999);
    TEQ_STR(batch.users[1999].name, std::string("u1999"));
    std::printf("   tape for 2000 users: %zu bytes (%u records)\n", buf.size(),
                rep2.header.n_user_records);
  }
}

}  // namespace test
}  // namespace q207
