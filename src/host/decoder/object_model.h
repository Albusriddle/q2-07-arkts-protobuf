// Host-testable object model: POD -> a plain value tree -> canonical JSON.
//
// This is deliberately outside decode_napi.cpp so the exact user-visible shape
// (int64 string, null profile, oneof members by contact_bits, optional level,
// last-wins map) is covered by host tests without a NAPI runtime.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "host/decoder/pod.h"

namespace q207 {
namespace view {

struct ProfileView {
  std::string city;
  int32_t age = 0;
};

struct UserView {
  std::string id;
  std::string name;
  std::vector<std::string> tags;
  bool has_profile = false;
  ProfileView profile;
  bool has_email = false;
  std::string email;
  bool has_phone = false;
  std::string phone;
  bool has_level = false;
  int32_t level = 0;
  std::vector<std::pair<std::string, int32_t>> scores;
};

void build_user_view(const pod::User& u, UserView* out);
std::string user_view_json(const UserView& v);

}  // namespace view
}  // namespace q207
