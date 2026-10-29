// HOST-ONLY.  Canonical E_* spellings, identical to the W0 Python reference
// model (out/w0/security/reference/wire_validator.py) so the 38 golden wire
// vectors can be judged against the C++ reader without translation.
#include "host/common/status.h"

namespace q207 {

const char* code_name(Code c) {
  switch (c) {
    case Code::Ok: return "E_OK";
    case Code::Truncated: return "E_TRUNCATED";
    case Code::VarintTooLong: return "E_VARINT_TOO_LONG";
    case Code::Varint10thByte: return "E_VARINT_10TH_BYTE";
    case Code::TagTooLong: return "E_TAG_TOO_LONG";
    case Code::FieldZero: return "E_FIELD_ZERO";
    case Code::FieldRange: return "E_FIELD_RANGE";
    case Code::WireTypeInvalid: return "E_WIRE_TYPE_INVALID";
    case Code::WireTypeMismatch: return "E_WIRE_TYPE_MISMATCH";
    case Code::LengthExceedsRemaining: return "E_LENGTH_EXCEEDS_REMAINING";
    case Code::LengthExceedsCap: return "E_LENGTH_EXCEEDS_CAP";
    case Code::Depth: return "E_DEPTH";
    case Code::GroupUnexpected: return "E_GROUP_UNEXPECTED";
    case Code::GroupMismatch: return "E_GROUP_MISMATCH";
    case Code::PackedLength: return "E_PACKED_LENGTH";
    case Code::PackedOverrun: return "E_PACKED_OVERRUN";
    case Code::MapKeyDangerous: return "E_MAP_KEY_DANGEROUS";
    case Code::Utf8Invalid: return "E_UTF8_INVALID";
    case Code::ObjectBudget: return "E_OBJECT_BUDGET";
    case Code::FieldBudget: return "E_FIELD_BUDGET";
    case Code::BatchTooLarge: return "E_BATCH_TOO_LARGE";
    case Code::MessageTooLarge: return "E_MESSAGE_TOO_LARGE";
    case Code::FieldTooLarge: return "E_FIELD_TOO_LARGE";
    case Code::Internal: return "E_INTERNAL";
    case Code::IoError: return "E_IO_ERROR";
  }
  return "E_INTERNAL";
}

}  // namespace q207
