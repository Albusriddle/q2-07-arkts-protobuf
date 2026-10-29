// HOST-ONLY norm-v1 object-graph dump.
#include "host/decoder/norm.h"

#include <cstdio>
#include <string>
#include <vector>

#include "host/decoder/object_model.h"

namespace q207 {
namespace norm {
namespace {

void append_int(std::string* out, long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld", v);
  out->append(buf);
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
  view::UserView v;
  view::build_user_view(u, &v);
  return view::user_view_json(v);
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
