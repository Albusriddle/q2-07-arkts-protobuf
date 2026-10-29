// HOST-ONLY conformance validator -- decision-order twin of the W0 Python model.
#include "host/decoder/conformance.h"

#include <algorithm>

namespace q207 {
namespace conformance {
namespace {

using wire::kWireEndGroup;
using wire::kWireFixed32;
using wire::kWireFixed64;
using wire::kWireLength;
using wire::kWireStartGroup;
using wire::kWireVarint;
using wire::Reader;

struct Spec {
  uint32_t number;
  const char* name;
  uint32_t wire;
  bool repeated;
  const char* kind;
};

const Spec kSchema[] = {
    {1, "f1", kWireVarint, false, "int32"},
    {2, "f2", kWireLength, false, "string"},
    {3, "f3", kWireFixed32, true, "fixed32"},
    {4, "f4", kWireLength, false, "message"},
    {5, "f5", kWireLength, false, "map"},
};

const Spec* find_spec(uint32_t number) {
  for (const Spec& s : kSchema) {
    if (s.number == number) return &s;
  }
  return nullptr;
}

// Mirrors the reference model's read_known_field / parse_message structure.
class Validator {
 public:
  Validator(Reader* r, bool strict) : r_(r), strict_(strict) {}

  Code parse_message() {
    Code e = r_->count_object();
    if (e != Code::Ok) return e;
    while (!r_->at_end()) {
      uint32_t field = 0, w = 0;
      e = r_->read_tag(&field, &w);
      if (e != Code::Ok) return e;
      if (w == kWireEndGroup) return r_->fail_with(Code::GroupUnexpected);
      e = r_->count_field();
      if (e != Code::Ok) return e;
      const Spec* spec = find_spec(field);
      if (spec == nullptr) {
        e = r_->skip_field(field, w);
        if (e != Code::Ok) return e;
        continue;
      }
      e = read_known(*spec, w);
      if (e != Code::Ok) return e;
    }
    return Code::Ok;
  }

 private:
  Code mismatch(uint32_t field, uint32_t w) {
    if (strict_) return r_->fail_with(Code::WireTypeMismatch);
    // Skip the actual field (not field 0): a group end must match its start.
    return r_->skip_field(field, w);
  }

  Code read_known(const Spec& spec, uint32_t w) {
    const std::string kind = spec.kind;
    if (kind == "int32") {
      if (w != kWireVarint) return mismatch(spec.number, w);
      uint64_t v = 0;
      return r_->read_varint(&v);
    }
    if (kind == "string") {
      if (w != kWireLength) return mismatch(spec.number, w);
      const uint8_t* p = nullptr;
      uint32_t n = 0;
      return r_->read_string_view(&p, &n, r_->limits().max_string_bytes);
    }
    if (kind == "fixed32") {
      if (spec.repeated && w == kWireLength) {
        std::vector<uint32_t> tmp;
        return r_->read_packed_fixed32(&tmp, r_->limits().max_repeated_elements);
      }
      if (w != kWireFixed32) return mismatch(spec.number, w);
      uint32_t v = 0;
      return r_->read_fixed32(&v);
    }
    if (kind == "message") {
      if (w != kWireLength) return mismatch(spec.number, w);
      uint64_t cap = std::min<uint64_t>(static_cast<uint64_t>(r_->remaining()),
                                        r_->limits().max_message_bytes);
      Code e = r_->enter_length(cap);
      if (e != Code::Ok) return e;
      e = parse_message();
      if (e != Code::Ok) return e;
      return r_->leave();
    }
    if (kind == "map") {
      if (w != kWireLength) return mismatch(spec.number, w);
      uint64_t cap = std::min<uint64_t>(static_cast<uint64_t>(r_->remaining()),
                                        r_->limits().max_message_bytes);
      Code e = r_->enter_length(cap);
      if (e != Code::Ok) return e;
      e = read_map_entry();
      if (e != Code::Ok) return e;
      return r_->leave();
    }
    return r_->fail_with(Code::Internal);
  }

  Code read_map_entry() {
    Code e = r_->count_object();
    if (e != Code::Ok) return e;
    std::string key;
    while (!r_->at_end()) {
      uint32_t field = 0, w = 0;
      e = r_->read_tag(&field, &w);
      if (e != Code::Ok) return e;
      e = r_->count_field();
      if (e != Code::Ok) return e;
      if (field == 1 && w == kWireLength) {
        const uint8_t* p = nullptr;
        uint32_t n = 0;
        e = r_->read_string_view(&p, &n, r_->limits().max_string_bytes);
        if (e != Code::Ok) return e;
        key.assign(reinterpret_cast<const char*>(p), n);
      } else if (field == 2 && w == kWireVarint) {
        uint64_t v = 0;
        e = r_->read_varint(&v);
        if (e != Code::Ok) return e;
      } else {
        e = r_->skip_field(field, w);
        if (e != Code::Ok) return e;
      }
    }
    if (r_->limits().map_reject_dangerous && is_dangerous_map_key(key)) {
      return r_->fail_with(Code::MapKeyDangerous);
    }
    return Code::Ok;
  }

  Reader* r_;
  bool strict_;
};

}  // namespace

bool is_dangerous_map_key(const std::string& k) {
  static const char* kDangerous[] = {
      "__proto__",       "constructor",         "prototype",
      "toString",        "valueOf",             "hasOwnProperty",
      "isPrototypeOf",   "propertyIsEnumerable", "__defineGetter__",
      "__defineSetter__", "__lookupGetter__",   "__lookupSetter__",
  };
  for (const char* d : kDangerous) {
    if (k == d) return true;
  }
  return false;
}

const std::vector<FieldSpec>& reference_schema() {
  static const std::vector<FieldSpec> v = {
      {1, "f1", kWireVarint, false, "int32"},
      {2, "f2", kWireLength, false, "string"},
      {3, "f3", kWireFixed32, true, "fixed32"},
      {4, "f4", kWireLength, false, "message"},
      {5, "f5", kWireLength, false, "map"},
  };
  return v;
}

Result validate(const uint8_t* data, size_t size, const Limits& limits, bool strict) {
  Limits lim = limits;
  lim.strict_wire_type = strict;
  Result out;
  if (static_cast<uint64_t>(size) > lim.max_message_bytes) {
    out.code = Code::MessageTooLarge;
    return out;
  }
  Reader r(data, size, lim);
  Validator v(&r, strict);
  Code e = v.parse_message();
  out.code = e;
  out.error_offset = r.error_offset();
  out.objects = static_cast<uint32_t>(r.objects());
  out.fields = static_cast<uint32_t>(r.fields());
  out.reader_alloc_bytes = r.alloc_bytes();
  out.reader_alloc_calls = r.alloc_calls();
  return out;
}

}  // namespace conformance
}  // namespace q207
