// HOST-ONLY row tape v0: writer / reader / verifier.
//
// Layout follows report v3 §5.5.1 (four regions written once by C++, read once by
// the materializer) with the two hard rules from §5.5.6: `values` slots are
// addressed by BYTE offset and counted in ELEMENTS, and `roots` holds byte
// offsets into `tape`, never record indices.
//
//   +--------+----------------+--------+----------+---------+-------------+
//   | header | tape records   | roots  | blob     | values  |  (padding)  |
//   +--------+----------------+--------+----------+---------+-------------+
//    80 B      80 B / user      4 B/root  raw bytes  4 B-aligned slots
//              16 B / profile
//
// Header (80 bytes, little endian):
//   0   magic       8 B   "Q207TAP0"
//   8   version     u32   1
//   12  header_bytes u32  80
//   16  user_record_size u32   80
//   20  profile_record_size u32 16
//   24  n_user_records u32
//   28  n_profile_records u32
//   32  roots_off u32   (byte offset from file start)
//   36  roots_count u32
//   40  tape_off u32
//   44  tape_bytes u32
//   48  blob_off u32
//   52  blob_bytes u32
//   56  values_off u32
//   60  values_bytes u32
//   64  crc32 u32       (over tape|roots|blob|values, in that order)
//   68  flags u32       bit0: oneof keep-all (compat arm)
//   72  presence_words u32 (== 1; segmented bitmap hook for wide messages)
//   76  reserved u32    (0)
//
// User record (80 bytes, 8-byte aligned):
//   0  presence u32         bit(field-1) for fields 1..8
//   4  contact_bits u32     bit 5 / bit 6 (real oneof members)
//   8  oneof_case u32       last member seen (0 none)
//   12 unknown_fields u32
//   16 id i64
//   24 name_off u32, 28 name_len u32          -> blob
//   32 tags_count u32, 36 tags_vals_off u32   -> values (8 B/slot: off,len)
//   40 profile_off u32     0xFFFFFFFF = absent, else tape byte offset (16 B rec)
//   44 email_off u32, 48 email_len u32        -> blob
//   52 phone_off u32, 56 phone_len u32        -> blob
//   60 level i32
//   64 scores_count u32, 68 scores_vals_off u32 -> values (12 B/slot: off,len,i32)
//   72 reserved0 u32, 76 reserved1 u32 (0)
//
// Profile record (16 bytes):
//   0 city_off u32, 4 city_len u32, 8 age i32, 12 flags u32 (bit0 present)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "host/common/status.h"
#include "host/decoder/pod.h"

namespace q207 {
namespace tape {

constexpr uint32_t kHeaderBytes = 80;
constexpr uint32_t kUserRecordSize = 80;
constexpr uint32_t kProfileRecordSize = 16;
constexpr uint32_t kTagSlotBytes = 8;
constexpr uint32_t kScoreSlotBytes = 12;
constexpr uint32_t kAbsent = 0xFFFFFFFFu;
constexpr uint32_t kVersion = 1;
constexpr uint32_t kFlagOneofKeepAll = 1u << 0;
extern const char kMagic[8];

uint32_t crc32(const uint8_t* data, size_t n, uint32_t seed = 0);
uint32_t align8(uint32_t v);

struct Header {
  uint32_t version = 0;
  uint32_t header_bytes = 0;
  uint32_t user_record_size = 0;
  uint32_t profile_record_size = 0;
  uint32_t n_user_records = 0;
  uint32_t n_profile_records = 0;
  uint32_t roots_off = 0, roots_count = 0;
  uint32_t tape_off = 0, tape_bytes = 0;
  uint32_t blob_off = 0, blob_bytes = 0;
  uint32_t values_off = 0, values_bytes = 0;
  uint32_t crc32 = 0;
  uint32_t flags = 0;
  uint32_t presence_words = 0;
  uint32_t reserved = 0;
};

// ---- writer ------------------------------------------------------------
class Writer {
 public:
  explicit Writer(uint32_t flags = kFlagOneofKeepAll) : flags_(flags) {}

  void clear();
  // Appends one user record (+ profile record when present) and one root.
  // Returns the byte offset of the user record inside the tape region.
  uint32_t add_user(const pod::User& u);
  // Adds an explicit root (already added by add_user unless you passed false).
  void add_root(uint32_t rec_off);

  std::vector<uint8_t> finish() const;

  uint32_t n_user_records() const { return n_users_; }
  uint32_t n_profile_records() const { return n_profiles_; }
  size_t tape_bytes() const { return tape_.size(); }
  size_t blob_bytes() const { return blob_.size(); }
  size_t values_bytes() const { return values_.size(); }
  const std::vector<uint8_t>& tape() const { return tape_; }
  const std::vector<uint8_t>& blob() const { return blob_; }
  const std::vector<uint8_t>& values() const { return values_; }
  const std::vector<uint32_t>& roots() const { return roots_; }

 private:
  uint32_t add_blob(const std::string& s);
  uint32_t add_values_bytes(const uint8_t* p, size_t n);

  std::vector<uint8_t> tape_;
  std::vector<uint8_t> blob_;
  std::vector<uint8_t> values_;
  std::vector<uint32_t> roots_;
  uint32_t n_users_ = 0;
  uint32_t n_profiles_ = 0;
  uint32_t flags_;
};

// ---- verifier ----------------------------------------------------------
struct VerifyReport {
  bool ok = false;
  std::string error;      // first failing check, human readable
  std::string first_code; // "E_..." where applicable
  Header header;
  uint32_t users_seen = 0;
  uint32_t profiles_seen = 0;
  size_t total_bytes = 0;
};

// Header decode without any validation (tests use it to mutate fields).
Header parse_header(const uint8_t* data, size_t size);
// crc32 over roots|tape|blob|values using the offsets stored in `h`.
uint32_t section_crc(const uint8_t* data, size_t size, const Header& h);

// Full structural verification: magic/version/sizes, section bounds and
// 8-byte alignment, crc32, then every root record: presence bits, blob ranges,
// values ranges, profile pointer.  Also re-derives record counts.
VerifyReport verify(const uint8_t* data, size_t size);

// ---- reader ------------------------------------------------------------
class Reader {
 public:
  // NOTE: `data` must outlive the reader.  Call load() and check it first.
  Reader(const uint8_t* data, size_t size);
  Code load();
  const Header& header() const { return h_; }

  // Rebuilds the POD for one record.  Returns Internal on a structural problem
  // (verify() is the authoritative structural check).
  Code user_at(uint32_t rec_off, pod::User* out) const;
  // Reads all roots into a batch (materializer-equivalent path in C++).
  Code batch(pod::UserBatch* out) const;

  std::string_view blob(uint32_t off, uint32_t len) const;
  uint32_t root_at(uint32_t i) const;

 private:
  const uint8_t* data_;
  size_t size_;
  Header h_;
  bool loaded_ = false;
};

}  // namespace tape
}  // namespace q207
