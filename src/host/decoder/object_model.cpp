// Host-testable POD -> object view + canonical JSON.
#include "host/decoder/object_model.h"

#include <cstdio>

#include "host/decoder/map_order.h"
#include "host/decoder/norm.h"

namespace q207 {
namespace view {
namespace {

void append_int(std::string* out, long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld", v);
  out->append(buf);
}

}  // namespace

void build_user_view(const pod::User& u, UserView* out) {
  out->id = std::to_string(u.id);
  out->name = u.name;
  out->tags = u.tags;
  out->has_profile = u.profile.present;
  out->profile.city = u.profile.city;
  out->profile.age = u.profile.age;
  out->has_email = (u.contact_bits & (1u << pod::f::kUserEmail)) != 0;
  out->email = u.email;
  out->has_phone = (u.contact_bits & (1u << pod::f::kUserPhone)) != 0;
  out->phone = u.phone;
  out->has_level = u.has_level;
  out->level = u.level;
  out->scores = map_order::dedupe_map(u);
}

void build_batch_view(const pod::UserBatch& b, BatchView* out) {
  out->users.clear();
  out->users.reserve(b.users.size());
  for (const pod::User& u : b.users) {
    UserView v;
    build_user_view(u, &v);
    out->users.push_back(std::move(v));
  }
}

std::string batch_view_json(const BatchView& v) {
  std::string out = "{\"users\":[";
  for (size_t i = 0; i < v.users.size(); ++i) {
    if (i) out += ",";
    out += user_view_json(v.users[i]);
  }
  out += "]}";
  return out;
}

std::string user_view_json(const UserView& v) {
  std::string out = "{\"id\":\"";
  out += norm::json_escape(v.id);
  out += "\",\"name\":\"";
  out += norm::json_escape(v.name);
  out += "\",\"tags\":[";
  for (size_t i = 0; i < v.tags.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += norm::json_escape(v.tags[i]);
    out += "\"";
  }
  out += "],\"profile\":";
  if (v.has_profile) {
    out += "{\"city\":\"";
    out += norm::json_escape(v.profile.city);
    out += "\",\"age\":";
    append_int(&out, v.profile.age);
    out += "}";
  } else {
    out += "null";
  }
  if (v.has_email) {
    out += ",\"email\":\"";
    out += norm::json_escape(v.email);
    out += "\"";
  }
  if (v.has_phone) {
    out += ",\"phone\":\"";
    out += norm::json_escape(v.phone);
    out += "\"";
  }
  if (v.has_level) {
    out += ",\"level\":";
    append_int(&out, v.level);
  }
  out += ",\"scores\":{";
  for (size_t i = 0; i < v.scores.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += norm::json_escape(v.scores[i].first);
    out += "\":";
    append_int(&out, v.scores[i].second);
  }
  out += "}}";
  return out;
}

}  // namespace view
}  // namespace q207
