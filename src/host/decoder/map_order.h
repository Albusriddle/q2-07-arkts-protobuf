// Map<string,int32> ordering shared by norm-v1 and the NAPI object model.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "host/decoder/pod.h"

namespace q207 {
namespace map_order {

// JS object semantics: duplicate keys last-wins, first insertion position kept;
// integer-like keys enumerate first in ascending numeric order, then strings in
// insertion order (protobufjs toObject on 7.6.5).
std::vector<std::pair<std::string, int32_t>> dedupe_map(const pod::User& u);

}  // namespace map_order
}  // namespace q207
