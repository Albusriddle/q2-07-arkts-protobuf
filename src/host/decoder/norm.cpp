// HOST-ONLY norm-v1 object-graph dump.
#include "host/decoder/norm.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace q207 {
namespace norm {
namespace {

void append_int(std::string* out, long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld", v);
  out->append(buf);
}

// Map<string,int32> with JS object semantics: last value wins, first position kept.
// JS enumerates integer-like keys first (ascending), then string keys in insertion
// order; replicate that so norm-v1 matches protobufjs toObject(), not just C++ order.
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

}  // namespace

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);  // UTF-8 passes through (validated upstream)
        }
    }
  }
  return out;
}

std::string profile_json(const pod::Profile& p) {
  std::string out = "{\"city\":\"";
  out += json_escape(p.city);
  out += "\",\"age\":";
  append_int(&out, p.age);
  out += "}";
  return out;
}

std::string user_json(const pod::User& u) {
  std::string out = "{";
  // id (int64 -> decimal string, protobufjs longs:String)
  out += "\"id\":\"";
  append_int(&out, static_cast<long long>(u.id));
  out += "\",\"name\":\"";
  out += json_escape(u.name);
  out += "\",\"tags\":[";
  for (size_t i = 0; i < u.tags.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += json_escape(u.tags[i]);
    out += "\"";
  }
  out += "],\"profile\":";
  out += u.profile.present ? profile_json(u.profile) : std::string("null");
  // oneof members: omitted when absent
  if ((u.contact_bits & (1u << pod::f::kUserEmail)) != 0) {
    out += ",\"email\":\"";
    out += json_escape(u.email);
    out += "\"";
  }
  if ((u.contact_bits & (1u << pod::f::kUserPhone)) != 0) {
    out += ",\"phone\":\"";
    out += json_escape(u.phone);
    out += "\"";
  }
  // proto3 optional: omitted when absent (null on the message, key omitted by toObject)
  if (u.has_level) {
    out += ",\"level\":";
    append_int(&out, u.level);
  }
  out += ",\"scores\":{";
  const auto map = dedupe_map(u);
  for (size_t i = 0; i < map.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += json_escape(map[i].first);
    out += "\":";
    append_int(&out, map[i].second);
  }
  out += "}}";
  return out;
}

std::string user_batch_json(const pod::UserBatch& b) {
  std::string out = "{\"users\":[";
  for (size_t i = 0; i < b.users.size(); ++i) {
    if (i) out += ",";
    out += user_json(b.users[i]);
  }
  out += "]}";
  return out;
}

}  // namespace norm
}  // namespace q207
