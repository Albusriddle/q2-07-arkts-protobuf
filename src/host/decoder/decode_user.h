// HOST-ONLY decoder for proto/example.proto.
//
// Covers the mixed-schema slice: int64 (User.id), string (name/tags/profile.city),
// repeated string (tags), nested message (profile), oneof (email|phone),
// proto3 optional (level) and map<string,int32> (scores).
//
// `user_batch` returns the batch index space; `user` decodes a single message.
// Wire-type mismatch is strict by default; compat mode skips as unknown field
// (matches the reference model's strict/compat arms).
#pragma once

#include <cstddef>
#include <cstdint>

#include "host/common/status.h"
#include "host/decoder/pod.h"
#include "host/decoder/wire_reader.h"

namespace q207 {
namespace decode {

struct Stats {
  uint32_t objects = 0;
  uint32_t fields = 0;
  uint32_t unknown_fields = 0;
  size_t error_offset = 0;
};

// One User message (wire bytes = the message body, no length prefix).
Code user(const uint8_t* data, size_t size, const Limits& limits, pod::User* out,
          Stats* stats = nullptr);

// One UserBatch message.  `out->users` is cleared first; its buffer is reused.
Code user_batch(const uint8_t* data, size_t size, const Limits& limits,
                pod::UserBatch* out, Stats* stats = nullptr);

}  // namespace decode
}  // namespace q207
