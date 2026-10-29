// HOST-ONLY.  Plain-old-data target for proto/example.proto (package q207.demo).
//
// This is the "POD struct" the slice decodes into, before any tape/materializer
// work.  Presence is tracked explicitly because proto3 `optional` and `oneof`
// need tri-state (absent / present-with-default / present-with-value) and the
// differential test has to reproduce protobufjs 7.6.5's own-property behaviour.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace q207 {
namespace pod {

// proto/example.proto -- field numbers (kept next to the POD so a schema change
// is a single-file edit; codegen replaces this file later).
namespace f {
// message Profile
constexpr uint32_t kProfileCity = 1;
constexpr uint32_t kProfileAge = 2;
// message User
constexpr uint32_t kUserId = 1;
constexpr uint32_t kUserName = 2;
constexpr uint32_t kUserTags = 3;
constexpr uint32_t kUserProfile = 4;
constexpr uint32_t kUserEmail = 5;
constexpr uint32_t kUserPhone = 6;
constexpr uint32_t kUserLevel = 7;
constexpr uint32_t kUserScores = 8;
// message UserBatch
constexpr uint32_t kUserBatchUsers = 1;
// Highest field number in User; presence bits live in one uint32.
constexpr uint32_t kUserMaxField = 8;
}  // namespace f

struct Profile {
  bool present = false;
  // bit (field-1) per nested field, so presence can be reported to the diff harness
  uint32_t presence = 0;
  std::string city;
  int32_t age = 0;

  bool present_field(uint32_t field) const {
    return field >= 1 && field <= 32 && ((presence >> (field - 1)) & 1u) != 0;
  }
  void mark(uint32_t field) {
    if (field >= 1 && field <= 32) presence |= (1u << (field - 1));
  }
  void clear() {
    present = false;
    presence = 0;
    city.clear();
    age = 0;
  }
};

struct MapEntry {
  std::string key;
  int32_t value = 0;
};

struct User {
  // bit (field-1) is set when the field occurred on the wire (presence bitmap).
  uint32_t presence = 0;
  int64_t id = 0;
  std::string name;
  std::vector<std::string> tags;
  Profile profile;
  // oneof contact: compat arm keeps ALL members (bits), spec arm keeps last.
  uint32_t contact_bits = 0;  // bit (field) for fields 5 / 6
  uint32_t oneof_case = 0;    // last member seen: 0 none, 5 email, 6 phone
  std::string email;
  std::string phone;
  bool has_level = false;
  int32_t level = 0;
  std::vector<MapEntry> scores;  // wire order; duplicate keys resolved last-wins on dump
  uint32_t unknown_fields = 0;

  bool present(uint32_t field) const { return field >= 1 && field <= 32 && ((presence >> (field - 1)) & 1u) != 0; }
  void mark(uint32_t field) {
    if (field >= 1 && field <= 32) presence |= (1u << (field - 1));
  }
  void clear() {
    presence = 0;
    id = 0;
    name.clear();
    tags.clear();
    profile.clear();
    contact_bits = 0;
    oneof_case = 0;
    email.clear();
    phone.clear();
    has_level = false;
    level = 0;
    scores.clear();
    unknown_fields = 0;
  }
};

struct UserBatch {
  std::vector<User> users;
  void clear() { users.clear(); }
};

}  // namespace pod
}  // namespace q207
