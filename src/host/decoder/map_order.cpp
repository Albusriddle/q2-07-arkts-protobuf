// Shared JS-object map ordering for norm-v1 and NAPI object construction.
#include "host/decoder/map_order.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace q207 {
namespace map_order {
namespace {

bool is_array_index(const std::string& k, uint32_t* value) {
  if (k.empty() || (k.size() > 1 && k[0] == '0')) return false;
  uint64_t v = 0;
  for (char c : k) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
    if (v > 0xFFFFFFFEull) return false;
  }
  *value = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

std::vector<std::pair<std::string, int32_t>> dedupe_map(const pod::User& u) {
  std::vector<std::pair<std::string, int32_t>> out;
  out.reserve(u.scores.size());
  std::unordered_map<std::string, size_t> index;
  index.reserve(u.scores.size());
  for (const pod::MapEntry& e : u.scores) {
    auto it = index.find(e.key);
    if (it != index.end()) {
      out[it->second].second = e.value;
    } else {
      index.emplace(e.key, out.size());
      out.emplace_back(e.key, e.value);
    }
  }

  std::vector<size_t> ints;
  std::vector<size_t> strings;
  ints.reserve(out.size());
  strings.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    uint32_t ignored = 0;
    (is_array_index(out[i].first, &ignored) ? ints : strings).push_back(i);
  }
  std::stable_sort(ints.begin(), ints.end(), [&](size_t a, size_t b) {
    uint32_t va = 0, vb = 0;
    is_array_index(out[a].first, &va);
    is_array_index(out[b].first, &vb);
    return va < vb;
  });
  std::vector<std::pair<std::string, int32_t>> ordered;
  ordered.reserve(out.size());
  for (size_t i : ints) ordered.push_back(out[i]);
  for (size_t i : strings) ordered.push_back(out[i]);
  return ordered;
}

}  // namespace map_order
}  // namespace q207
