// HOST-ONLY wire reader.  See wire_reader.h for the mandated decision order.
#include "host/decoder/wire_reader.h"

namespace q207 {
namespace wire {

bool utf8_valid(const uint8_t* b, size_t n) {
  size_t i = 0;
  while (i < n) {
    const uint8_t c = b[i];
    if (c < 0x80) {
      i += 1;
    } else if (c >= 0xC2 && c <= 0xDF) {
      if (i + 1 >= n || (b[i + 1] & 0xC0) != 0x80) return false;
      i += 2;
    } else if (c == 0xE0) {
      if (i + 2 >= n || !(b[i + 1] >= 0xA0 && b[i + 1] <= 0xBF) ||
          (b[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
    } else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) {
      if (i + 2 >= n || (b[i + 1] & 0xC0) != 0x80 || (b[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
    } else if (c == 0xED) {
      if (i + 2 >= n || !(b[i + 1] >= 0x80 && b[i + 1] <= 0x9F) ||
          (b[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
    } else if (c == 0xF0) {
      if (i + 3 >= n || !(b[i + 1] >= 0x90 && b[i + 1] <= 0xBF) ||
          (b[i + 2] & 0xC0) != 0x80 || (b[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
    } else if (c >= 0xF1 && c <= 0xF3) {
      if (i + 3 >= n || (b[i + 1] & 0xC0) != 0x80 || (b[i + 2] & 0xC0) != 0x80 ||
          (b[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
    } else if (c == 0xF4) {
      if (i + 3 >= n || !(b[i + 1] >= 0x80 && b[i + 1] <= 0x8F) ||
          (b[i + 2] & 0xC0) != 0x80 || (b[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

Reader::Reader(const uint8_t* data, size_t size, const Limits& limits)
    : data_(data), size_(size), end_(size), lim_(limits) {}

Code Reader::fail(Code c) {
  code_ = c;
  return c;
}

Code Reader::read_varint(uint64_t* out, uint32_t max_bytes) {
  const size_t start = pos_;
  uint64_t result = 0;
  uint32_t shift = 0;
  for (uint32_t i = 0; i < max_bytes; ++i) {
    if (pos_ >= end_) {  // bound by the current message end, not just the buffer
      error_offset_ = start;
      return fail(Code::Truncated);
    }
    const uint8_t b = data_[pos_++];
    if (i == 9) {
      // Only the low bit of the 10th byte is meaningful (bit 63).  Anything
      // else, including a continuation bit, means bit 65+ is being encoded.
      if (b > 1) {
        error_offset_ = pos_ - 1;
        return fail(Code::Varint10thByte);
      }
      result |= static_cast<uint64_t>(b & 0x7F) << shift;
      *out = result;
      return Code::Ok;
    }
    result |= static_cast<uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      *out = result;
      return Code::Ok;
    }
    shift += 7;
  }
  error_offset_ = pos_;
  return fail(Code::VarintTooLong);
}

Code Reader::read_tag(uint32_t* field, uint32_t* wire) {
  const size_t start = pos_;
  uint64_t v = 0;
  const Code e = read_varint(&v, kMaxTagBytes);
  if (e != Code::Ok) {
    // A tag needing a 6th byte is TAG_TOO_LONG, not a generic varint error.
    return fail(e == Code::VarintTooLong ? Code::TagTooLong : e);
  }
  if (v > 0xFFFFFFFFull) {
    error_offset_ = start;
    return fail(Code::FieldRange);
  }
  const uint32_t f = static_cast<uint32_t>(v >> 3);
  const uint32_t w = static_cast<uint32_t>(v & 0x07);
  if (f == 0) {
    error_offset_ = start;
    return fail(Code::FieldZero);
  }
  // Protobuf field numbers are limited to 2^29-1 (the tag itself may use 32 bits).
  if (f > 0x1FFFFFFFu) {
    error_offset_ = start;
    return fail(Code::FieldRange);
  }
  if (w == 6 || w == 7) {
    error_offset_ = start;
    return fail(Code::WireTypeInvalid);
  }
  *field = f;
  *wire = w;
  return Code::Ok;
}

Code Reader::read_length(uint64_t* out, uint64_t cap) {
  const size_t start = pos_;
  uint64_t v = 0;
  Code e = read_varint(&v, kMaxVarintBytes);
  if (e != Code::Ok) return e;
  // Order matters: remaining first, then cap.  Both before any allocation.
  if (v > static_cast<uint64_t>(end_ - pos_)) {
    error_offset_ = start;
    return fail(Code::LengthExceedsRemaining);
  }
  if (v > cap) {
    error_offset_ = start;
    return fail(Code::LengthExceedsCap);
  }
  *out = v;
  return Code::Ok;
}

Code Reader::read_fixed32(uint32_t* out) {
  if (end_ - pos_ < 4) {
    error_offset_ = pos_;
    return fail(Code::Truncated);
  }
  const uint8_t* p = data_ + pos_;
  *out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  pos_ += 4;
  return Code::Ok;
}

Code Reader::read_fixed64(uint64_t* out) {
  if (end_ - pos_ < 8) {
    error_offset_ = pos_;
    return fail(Code::Truncated);
  }
  const uint8_t* p = data_ + pos_;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  *out = v;
  pos_ += 8;
  return Code::Ok;
}

Code Reader::read_sint32(int32_t* out) {
  uint64_t v = 0;
  Code e = read_varint(&v, kMaxVarintBytes);
  if (e != Code::Ok) return e;
  *out = zigzag32(static_cast<uint32_t>(v));
  return Code::Ok;
}

Code Reader::read_sint64(int64_t* out) {
  uint64_t v = 0;
  Code e = read_varint(&v, kMaxVarintBytes);
  if (e != Code::Ok) return e;
  *out = zigzag64(v);
  return Code::Ok;
}

Code Reader::read_int32(int32_t* out) {
  uint64_t v = 0;
  Code e = read_varint(&v, kMaxVarintBytes);
  if (e != Code::Ok) return e;
  // Negative int32 is sign-extended to 64 bits on the wire (10 bytes) then
  // truncated here.  Deliberately NOT a fixed32 read.
  *out = static_cast<int32_t>(static_cast<uint32_t>(v));
  return Code::Ok;
}

Code Reader::read_int64(int64_t* out) {
  uint64_t v = 0;
  Code e = read_varint(&v, kMaxVarintBytes);
  if (e != Code::Ok) return e;
  *out = static_cast<int64_t>(v);
  return Code::Ok;
}

Code Reader::enter_length(uint64_t cap) {
  uint64_t len = 0;
  Code e = read_length(&len, cap);
  if (e != Code::Ok) return e;
  // Depth is checked before the bound is pushed / any buffer is grown.
  ++depth_;
  if (depth_ > lim_.max_depth) {
    error_offset_ = pos_;
    return fail(Code::Depth);
  }
  if (sp_ >= sizeof(stack_) / sizeof(stack_[0])) {
    return fail(Code::Internal);
  }
  stack_[sp_++] = end_;
  end_ = pos_ + static_cast<size_t>(len);
  return Code::Ok;
}

Code Reader::leave() {
  const size_t sub_end = end_;
  if (sp_ == 0) return fail(Code::Internal);
  end_ = stack_[--sp_];
  --depth_;
  if (pos_ != sub_end) {
    error_offset_ = pos_;
    return fail(Code::Truncated);
  }
  return Code::Ok;
}

Code Reader::read_packed_varint(std::vector<uint64_t>* out, uint64_t max_elems) {
  Code e = enter_length(lim_.max_values_bytes);
  if (e != Code::Ok) return e;
  // Only now may we touch the vector.
  const uint64_t upper = static_cast<uint64_t>(end_ - pos_);
  note_alloc(upper / 8 > 0 ? upper / 8 : 0);
  out->clear();
  while (!at_end()) {
    uint64_t v = 0;
    e = read_varint(&v, kMaxVarintBytes);
    if (e != Code::Ok) return e;
    if (out->size() >= max_elems) {
      error_offset_ = pos_;
      return fail(Code::FieldBudget);
    }
    e = count_field();
    if (e != Code::Ok) return e;
    out->push_back(v);
  }
  return leave();
}

Code Reader::read_packed_fixed32(std::vector<uint32_t>* out, uint64_t max_elems) {
  Code e = enter_length(lim_.max_values_bytes);
  if (e != Code::Ok) return e;
  const uint64_t len = static_cast<uint64_t>(end_ - pos_);
  if (len % 4 != 0) {  // divisibility is checked before count is computed
    error_offset_ = pos_;
    return fail(Code::PackedLength);
  }
  const uint64_t count = len / 4;
  if (count > max_elems) {
    error_offset_ = pos_;
    return fail(Code::FieldBudget);
  }
  note_alloc(len);
  out->clear();
  out->reserve(static_cast<size_t>(count));
  while (!at_end()) {
    uint32_t v = 0;
    e = read_fixed32(&v);
    if (e != Code::Ok) return e;
    e = count_field();
    if (e != Code::Ok) return e;
    out->push_back(v);
  }
  return leave();
}

Code Reader::read_packed_zigzag32(std::vector<int32_t>* out, uint64_t max_elems) {
  Code e = enter_length(lim_.max_values_bytes);
  if (e != Code::Ok) return e;
  const uint64_t upper = static_cast<uint64_t>(end_ - pos_);
  note_alloc(upper / 8 * 4);
  out->clear();
  while (!at_end()) {
    uint64_t v = 0;
    e = read_varint(&v, kMaxVarintBytes);
    if (e != Code::Ok) return e;
    if (out->size() >= max_elems) {
      error_offset_ = pos_;
      return fail(Code::FieldBudget);
    }
    e = count_field();
    if (e != Code::Ok) return e;
    out->push_back(zigzag32(static_cast<uint32_t>(v)));
  }
  return leave();
}

Code Reader::skip_field(uint32_t field, uint32_t wire) {
  switch (wire) {
    case kWireVarint: {
      uint64_t v = 0;
      return read_varint(&v, kMaxVarintBytes);
    }
    case kWireFixed64: {
      if (end_ - pos_ < 8) {
        error_offset_ = pos_;
        return fail(Code::Truncated);
      }
      pos_ += 8;
      return Code::Ok;
    }
    case kWireLength: {
      uint64_t len = 0;
      Code e = read_length(&len, lim_.max_message_bytes);
      if (e != Code::Ok) return e;
      pos_ += static_cast<size_t>(len);  // never allocate for unknown fields
      return Code::Ok;
    }
    case kWireStartGroup:
      return skip_group(field);
    case kWireEndGroup:
      error_offset_ = pos_;
      return fail(Code::GroupUnexpected);
    case kWireFixed32:
      if (end_ - pos_ < 4) {
        error_offset_ = pos_;
        return fail(Code::Truncated);
      }
      pos_ += 4;
      return Code::Ok;
    default:
      error_offset_ = pos_;
      return fail(Code::WireTypeInvalid);
  }
}

Code Reader::skip_group(uint32_t field) {
  ++depth_;
  if (depth_ > lim_.max_depth) {
    error_offset_ = pos_;
    return fail(Code::Depth);
  }
  const size_t group_end = end_;
  while (pos_ < group_end) {
    uint32_t f = 0, w = 0;
    Code e = read_tag(&f, &w);
    if (e != Code::Ok) return e;
    if (w == kWireEndGroup) {
      if (f != field) {
        error_offset_ = pos_;
        return fail(Code::GroupMismatch);
      }
      --depth_;
      return Code::Ok;
    }
    e = skip_field(f, w);
    if (e != Code::Ok) return e;
  }
  --depth_;
  error_offset_ = pos_;
  return fail(Code::Truncated);
}

Code Reader::read_string(std::string* out, uint64_t cap) {
  const uint8_t* p = nullptr;
  uint32_t n = 0;
  Code e = read_string_view(&p, &n, cap);
  if (e != Code::Ok) return e;
  // Allocation happens after length/UTF-8 validation.
  note_alloc(n);
  out->assign(reinterpret_cast<const char*>(p), n);
  return Code::Ok;
}

Code Reader::read_string_view(const uint8_t** p, uint32_t* len, uint64_t cap) {
  uint64_t n = 0;
  Code e = read_length(&n, cap);
  if (e != Code::Ok) return e;
  const uint8_t* start = data_ + pos_;
  if (lim_.utf8_strict && !utf8_valid(start, static_cast<size_t>(n))) {
    error_offset_ = pos_;
    return fail(Code::Utf8Invalid);
  }
  *p = start;
  *len = static_cast<uint32_t>(n);
  pos_ += static_cast<size_t>(n);
  return Code::Ok;
}

Code Reader::count_object() {
  ++objects_;
  if (objects_ > lim_.max_objects) {
    error_offset_ = pos_;
    return fail(Code::ObjectBudget);
  }
  return Code::Ok;
}

Code Reader::count_field() {
  ++fields_;
  if (fields_ > lim_.max_fields) {
    error_offset_ = pos_;
    return fail(Code::FieldBudget);
  }
  return Code::Ok;
}

}  // namespace wire
}  // namespace q207
