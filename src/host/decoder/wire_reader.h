// Q2-07 host vertical slice -- protobuf wire reader (C++17).
//
// HOST-ONLY.  This is host-side logic proof, not a device measurement.
//
// The reader is the security boundary described in report v3 §5.5.8 / §6.5.1:
// the ONLY allowed order for every length-delimited field is
//
//     tag  ->  wire type  ->  len <= remaining  ->  len <= cap  ->  allocate
//
// Nothing here allocates (or reserves) until the two length checks have passed.
// `note_alloc()` exists so tests can assert that the allocation counter stays at
// zero for pathological inputs (e.g. a 4 GiB length prefix).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "host/common/status.h"

namespace q207 {
namespace wire {

// Wire types (protobuf encoding).
enum WireType : uint32_t {
  kWireVarint = 0,
  kWireFixed64 = 1,
  kWireLength = 2,
  kWireStartGroup = 3,
  kWireEndGroup = 4,
  kWireFixed32 = 5,
};

constexpr uint32_t kMaxTagBytes = 5;    // 5 bytes => up to 2^35-1, covers uint32 tags
constexpr uint32_t kMaxVarintBytes = 10;

// Strict protobuf UTF-8 check: no overlong, no surrogate, no > U+10FFFF,
// no truncated sequence.  Same rules as the W0 reference model.
bool utf8_valid(const uint8_t* p, size_t n);

// zigzag helpers (sint32 / sint64).  Present because the primitive set required
// by the slice includes zigzag even though proto/example.proto has no sint field.
inline int32_t zigzag32(uint32_t v) {
  return static_cast<int32_t>((v >> 1) ^ (~(v & 1) + 1));
}
inline int64_t zigzag64(uint64_t v) {
  return static_cast<int64_t>((v >> 1) ^ (~(v & 1) + 1));
}

class Reader {
 public:
  Reader(const uint8_t* data, size_t size, const Limits& limits);

  // ---- primitives ------------------------------------------------------
  // Bounded varint.  `max_bytes` is 10 for values, 5 for tags.
  Code read_varint(uint64_t* out, uint32_t max_bytes = kMaxVarintBytes);
  // tag -> (field number, wire type).  Validates field!=0, tag<=2^32-1, wire!=6/7.
  Code read_tag(uint32_t* field, uint32_t* wire);
  // Length prefix with the mandated decision order: remaining first, cap second.
  Code read_length(uint64_t* out, uint64_t cap);
  Code read_fixed32(uint32_t* out);
  Code read_fixed64(uint64_t* out);
  Code read_sint32(int32_t* out);
  Code read_sint64(int64_t* out);
  // int32/int64 semantics: read as uint64, then truncate (negative int32 is a
  // 10-byte varint on the wire -- never read it as a fixed32).
  Code read_int32(int32_t* out);
  Code read_int64(int64_t* out);

  // ---- length-delimited containers -------------------------------------
  // Reads a length prefix, checks remaining/cap, checks depth, then bounds the
  // reader to the payload.  Pairs with leave().
  Code enter_length(uint64_t cap);
  // Restores the previous bound.  Returns Truncated if the payload was not fully
  // consumed (mirrors the reference model's `pos != end` check).
  Code leave();
  uint32_t depth() const { return depth_; }

  // Packed repeated primitives (proto3 default for numeric repeated fields).
  // `out` is only touched after the length checks pass.
  Code read_packed_varint(std::vector<uint64_t>* out, uint64_t max_elems);
  Code read_packed_fixed32(std::vector<uint32_t>* out, uint64_t max_elems);
  Code read_packed_zigzag32(std::vector<int32_t>* out, uint64_t max_elems);

  // ---- skipping (unknown fields: never allocate) -----------------------
  Code skip_field(uint32_t field, uint32_t wire);
  Code skip_group(uint32_t field);

  // ---- strings ---------------------------------------------------------
  // Validates length + UTF-8, then copies.  Allocation happens last.
  Code read_string(std::string* out, uint64_t cap);
  // Same validation, zero copy: returns a view into the input buffer.
  Code read_string_view(const uint8_t** p, uint32_t* len, uint64_t cap);

  // ---- accounting ------------------------------------------------------
  // Budgets are the reference model's object/field budgets.
  Code count_object();  // 1 per message started, 1 per map entry
  Code count_field();   // 1 per field occurrence (packed elements count individually)

  // Fail the reader with an explicit code (used by schema layers that detect a
  // semantic error after the reader has already moved the cursor).
  Code fail_with(Code c) { return fail(c); }

  // ---- state -----------------------------------------------------------
  size_t pos() const { return pos_; }
  size_t end() const { return end_; }
  size_t remaining() const { return end_ - pos_; }
  bool at_end() const { return pos_ >= end_; }
  size_t error_offset() const { return error_offset_; }
  void rewind(size_t p) { pos_ = p; }
  const uint8_t* data() const { return data_; }
  size_t data_size() const { return size_; }
  const Limits& limits() const { return lim_; }

  // Allocation accounting hook.  Called immediately before every buffer growth
  // that is attributable to a wire-supplied length, i.e. only after validation.
  void note_alloc(uint64_t bytes) {
    ++alloc_calls_;
    alloc_bytes_ += bytes;
  }
  uint64_t alloc_calls() const { return alloc_calls_; }
  uint64_t alloc_bytes() const { return alloc_bytes_; }
  uint64_t objects() const { return objects_; }
  uint64_t fields() const { return fields_; }

  // Last error, for diagnostics.
  Code code() const { return code_; }

 private:
  Code fail(Code c);
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  size_t end_;
  size_t error_offset_ = 0;
  Code code_ = Code::Ok;
  uint32_t depth_ = 0;
  uint32_t sp_ = 0;
  size_t stack_[128];  // max_depth <= 100 by default; guard extra room
  const Limits& lim_;
  uint64_t objects_ = 0;
  uint64_t fields_ = 0;
  uint64_t alloc_calls_ = 0;
  uint64_t alloc_bytes_ = 0;
};

}  // namespace wire
}  // namespace q207
