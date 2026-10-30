// HOST-ONLY decoder wrapper for proto/example.proto.
// The parser body is generated into generated/q207_demo_decoder.inc.
#include "host/decoder/decode_user.h"

#include <algorithm>
#include <utility>

namespace q207 {
namespace decode {
namespace {

// The generated table carries wire types as well as field numbers.  A proto type
// change (e.g. int64 -> string) must fail here, not silently change decode logic.
static_assert(pod::f::kUserIdWire == wire::kWireVarint, "User.id wire type changed");
static_assert(pod::f::kUserNameWire == wire::kWireLength, "User.name wire type changed");
static_assert(pod::f::kUserTagsWire == wire::kWireLength, "User.tags wire type changed");
static_assert(pod::f::kUserProfileWire == wire::kWireLength, "User.profile wire type changed");
static_assert(pod::f::kUserEmailWire == wire::kWireLength, "User.email wire type changed");
static_assert(pod::f::kUserPhoneWire == wire::kWireLength, "User.phone wire type changed");
static_assert(pod::f::kUserLevelWire == wire::kWireVarint, "User.level wire type changed");
static_assert(pod::f::kUserScoresWire == wire::kWireLength, "User.scores wire type changed");
static_assert(pod::f::kProfileCityWire == wire::kWireLength, "Profile.city wire type changed");
static_assert(pod::f::kProfileAgeWire == wire::kWireVarint, "Profile.age wire type changed");
static_assert(pod::f::kUserBatchUsersWire == wire::kWireLength, "UserBatch.users wire type changed");

using wire::kWireEndGroup;
using wire::kWireLength;
using wire::kWireVarint;
using wire::Reader;
namespace f = pod::f;

// A known field whose wire type does not match the schema: strict fails, compat
// skips the field as if it were unknown (never reinterpret the bytes).
inline Code mismatch(Reader& r, const Limits& lim, uint32_t field, uint32_t got,
                     uint32_t* unknown_fields) {
  if (lim.strict_wire_type) return r.fail_with(Code::WireTypeMismatch);
  // Compat arm: treat it as an unknown field (never reinterpret the bytes).
  if (unknown_fields) ++*unknown_fields;
  return r.skip_field(field, got);
}

#include "generated/q207_demo_decoder.inc"

void fill_stats(const Reader& r, uint32_t unknown_fields, Stats* out) {
  if (!out) return;
  out->objects = static_cast<uint32_t>(r.objects());
  out->fields = static_cast<uint32_t>(r.fields());
  out->unknown_fields = unknown_fields;
  out->error_offset = r.error_offset();
}

}  // namespace

Code user(const uint8_t* data, size_t size, const Limits& limits, pod::User* out,
          Stats* stats) {
  if (static_cast<uint64_t>(size) > limits.max_message_bytes) {
    if (stats) stats->error_offset = 0;
    return Code::MessageTooLarge;
  }
  out->clear();
  Reader r(data, size, limits);
  Code e = parse_user_body(r, limits, out);
  if (e == Code::Ok && r.pos() != size) e = r.fail_with(Code::Truncated);
  fill_stats(r, out->unknown_fields, stats);
  return e;
}

Code user_batch(const uint8_t* data, size_t size, const Limits& limits,
                pod::UserBatch* out, Stats* stats) {
  if (static_cast<uint64_t>(size) > limits.max_message_bytes) {
    if (stats) stats->error_offset = 0;
    return Code::MessageTooLarge;
  }
  out->clear();
  Reader r(data, size, limits);
  Code e = parse_user_batch(r, limits, out);
  if (e == Code::Ok && r.pos() != size) e = r.fail_with(Code::Truncated);
  uint32_t unknown = 0;
  for (const pod::User& u : out->users) unknown += u.unknown_fields;
  fill_stats(r, unknown, stats);
  return e;
}

}  // namespace decode
}  // namespace q207
