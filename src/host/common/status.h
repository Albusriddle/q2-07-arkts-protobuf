// Q2-07 host vertical slice -- error codes and hard limits.
//
// HOST-ONLY: everything in src/host/** is a host-side logic/correctness harness.
// It is NOT a device (OpenHarmony/ArkTS) result and must never be quoted as one.
//
// Code names are the wire-validation spec names used by the W0 Python reference
// model (out/w0/security/reference/wire_validator.py) and by the 38 golden wire
// vectors (out/w0/security/test_vectors/wire_vectors.json).  Keeping the strings
// identical is what lets the C++ reader be judged by the same golden file.
#pragma once

#include <cstddef>
#include <cstdint>

namespace q207 {

enum class Code : uint8_t {
  Ok = 0,
  Truncated,              // E_TRUNCATED
  VarintTooLong,          // E_VARINT_TOO_LONG
  Varint10thByte,         // E_VARINT_10TH_BYTE  (10th byte > 1 => bit 65)
  TagTooLong,             // E_TAG_TOO_LONG      (tag needs > 5 bytes)
  FieldZero,              // E_FIELD_ZERO        (tag.FieldNumber() == 0)
  FieldRange,             // E_FIELD_RANGE       (tag > 2^32-1)
  WireTypeInvalid,        // E_WIRE_TYPE_INVALID (wire type 6 / 7)
  WireTypeMismatch,       // E_WIRE_TYPE_MISMATCH(strict mode only)
  LengthExceedsRemaining, // E_LENGTH_EXCEEDS_REMAINING
  LengthExceedsCap,       // E_LENGTH_EXCEEDS_CAP
  Depth,                  // E_DEPTH             (nested > max_depth)
  GroupUnexpected,        // E_GROUP_UNEXPECTED  (wire type 4 in message body)
  GroupMismatch,          // E_GROUP_MISMATCH
  PackedLength,           // E_PACKED_LENGTH     (packed byte length not a multiple)
  PackedOverrun,          // E_PACKED_OVERRUN
  MapKeyDangerous,        // E_MAP_KEY_DANGEROUS (__proto__ / constructor / ...)
  Utf8Invalid,            // E_UTF8_INVALID
  ObjectBudget,           // E_OBJECT_BUDGET
  FieldBudget,            // E_FIELD_BUDGET
  BatchTooLarge,          // E_BATCH_TOO_LARGE (UserBatch max_batch_users)
  MessageTooLarge,        // E_MESSAGE_TOO_LARGE (single message > 64 MiB)
  FieldTooLarge,          // E_FIELD_TOO_LARGE   (single string/bytes > 16 MiB)
  Internal,               // E_INTERNAL
  IoError,                // E_IO_ERROR          (host tool only, not a wire code)
};

// Canonical "E_*" spelling (matches wire_validator.py Code values).
const char* code_name(Code c);

// Hard limits.  Defaults come from the W0 threat model as restated in report
// v3 §5.5.8 / §6.3.4: depth 100, single message 64 MiB, single string/bytes
// 16 MiB, single repeated 1,000,000 elements, tape 256 MiB.
struct Limits {
  uint64_t max_message_bytes = 64ull << 20;   // 67,108,864
  uint32_t max_depth = 100;
  uint64_t max_string_bytes = 16ull << 20;    // 16,777,216
  uint64_t max_repeated_elements = 1000000;
  uint64_t max_objects = 1000000;
  uint64_t max_fields = 10000000;
  uint64_t max_values_bytes = 64ull << 20;
  uint64_t max_blob_bytes = 128ull << 20;
  uint64_t max_batch_users = 1000000;  // UserBatch only; NAPI profile lowers this
  uint64_t max_tape_bytes = 256ull << 20;
  bool strict_wire_type = true;   // known-field wire mismatch => error (compat mode skips)
  bool utf8_strict = true;        // invalid UTF-8 => error (compat mode would replace)
  bool map_reject_dangerous = true;

  // The 38 golden vectors specify per-vector limit overrides; this helper applies
  // the two overrides the vector file actually uses so the JSON can stay a table.
  Limits with(uint64_t max_string_bytes_ = 0, uint64_t max_objects_ = 0,
              uint64_t max_fields_ = 0) const {
    Limits l = *this;
    if (max_string_bytes_) l.max_string_bytes = max_string_bytes_;
    if (max_objects_) l.max_objects = max_objects_;
    if (max_fields_) l.max_fields = max_fields_;
    return l;
  }
};

}  // namespace q207
