// HOST-ONLY canonical object-graph dump ("norm-v1").
//
// The differential test compares three producers of the same object graph:
//   1. C++ decoder  -> norm JSON          (decoder/norm.cpp, this file)
//   2. C++ tape     -> TS materializer    (materializer/materializer.ts)
//   3. protobufjs 7.6.5 decode -> toObject({defaults:true, longs:String})
// Rules are exactly (3)'s, probed on 2026-10-29 against protobufjs 7.6.5:
//   * int64 -> decimal string; int32 -> number
//   * string -> string, empty string when absent; repeated -> [], map -> {}
//   * sub-message: nested object when the field is present (even if empty),
//     null when absent
//   * real oneof members (email/phone) and proto3 optional (level): the key is
//     OMITTED when absent (protobufjs does not default-initialise them)
//   * duplicate map keys: last value wins, position of the first occurrence kept
#pragma once

#include <string>

#include "host/decoder/pod.h"

namespace q207 {
namespace norm {

std::string json_escape(const std::string& s);

// Canonical dump of one User / a UserBatch.
std::string user_json(const pod::User& u);
std::string user_batch_json(const pod::UserBatch& b);

// Convenience: one message of arbitrary type name is not needed for this slice.
std::string profile_json(const pod::Profile& p);

}  // namespace norm
}  // namespace q207
