// HOST-ONLY norm-v1 object-graph dump.
#include "host/decoder/norm.h"

#include <cstdio>
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
std::vector<std::pair<std::string, int32_t>> dedupe_map(const pod::User& u) {
  std::vector<std::pair<std::string, int32_t>> out;
  out.reserve(u.scores.size());
  for (const pod::MapEntry& e : u.scores) {
    bool found = false;
    for (auto& kv : out) {
      if (kv.first == e.key) {
        kv.second = e.value;
        found = true;
        break;
      }
    }
    if (!found) out.emplace_back(e.key, e.value);
  }
  return out;
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
