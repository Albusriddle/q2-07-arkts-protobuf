// HOST-ONLY row tape v0.  See tape.h for the byte-level layout contract.
#include "host/tape/tape.h"

#include <cstring>
#include <string_view>

namespace q207 {
namespace tape {

const char kMagic[8] = {'Q', '2', '0', '7', 'T', 'A', 'P', '0'};

namespace {

inline void put_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
inline void put_i32(uint8_t* p, int32_t v) { put_u32(p, static_cast<uint32_t>(v)); }
inline void put_u64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t get_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t get_i32(const uint8_t* p) { return static_cast<int32_t>(get_u32(p)); }
inline uint64_t get_u64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

bool range_ok(uint64_t off, uint64_t len, uint64_t section_off, uint64_t section_len) {
  return off >= section_off && off <= section_off + section_len &&
         len <= section_off + section_len - off;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t n, uint32_t seed) {
  uint32_t crc = seed ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

uint32_t align8(uint32_t v) { return (v + 7u) & ~7u; }

// ---- writer -------------------------------------------------------------
void Writer::clear() {
  tape_.clear();
  blob_.clear();
  values_.clear();
  roots_.clear();
  n_users_ = 0;
  n_profiles_ = 0;
}

uint32_t Writer::add_blob(const std::string& s) {
  const uint32_t off = static_cast<uint32_t>(blob_.size());
  blob_.insert(blob_.end(), s.begin(), s.end());
  return off;
}

uint32_t Writer::add_values_bytes(const uint8_t* p, size_t n) {
  const uint32_t off = static_cast<uint32_t>(values_.size());
  values_.insert(values_.end(), p, p + n);
  return off;
}

uint32_t Writer::add_user(const pod::User& u) {
  const uint32_t rec_off = static_cast<uint32_t>(tape_.size());
  uint8_t rec[kUserRecordSize];
  std::memset(rec, 0, sizeof(rec));

  put_u32(rec + 0, u.presence);
  put_u32(rec + 4, u.contact_bits);
  put_u32(rec + 8, u.oneof_case);
  put_u32(rec + 12, u.unknown_fields);
  put_u64(rec + 16, static_cast<uint64_t>(u.id));

  put_u32(rec + 24, add_blob(u.name));
  put_u32(rec + 28, static_cast<uint32_t>(u.name.size()));

  // tags: element slots in the values region
  {
    uint8_t slots[kTagSlotBytes];
    // Reserve the region first so slot offsets are computed from a stable base.
    const uint32_t base = static_cast<uint32_t>(values_.size());
    put_u32(rec + 32, static_cast<uint32_t>(u.tags.size()));
    put_u32(rec + 36, base);
    for (const std::string& t : u.tags) {
      put_u32(slots, add_blob(t));
      put_u32(slots + 4, static_cast<uint32_t>(t.size()));
      add_values_bytes(slots, kTagSlotBytes);
    }
  }

  // profile: separate 16-byte record appended after the user record
  if (u.profile.present) {
    uint8_t p[kProfileRecordSize];
    std::memset(p, 0, sizeof(p));
    put_u32(p + 0, add_blob(u.profile.city));
    put_u32(p + 4, static_cast<uint32_t>(u.profile.city.size()));
    put_i32(p + 8, u.profile.age);
    put_u32(p + 12, 1u);
    const uint32_t prof_off = rec_off + kUserRecordSize;
    put_u32(rec + 40, prof_off);
    tape_.insert(tape_.end(), rec, rec + kUserRecordSize);
    tape_.insert(tape_.end(), p, p + kProfileRecordSize);
    ++n_profiles_;
  } else {
    put_u32(rec + 40, kAbsent);
    tape_.insert(tape_.end(), rec, rec + kUserRecordSize);
  }

  // oneof member strings
  put_u32(rec + 44, add_blob(u.email));
  put_u32(rec + 48, static_cast<uint32_t>(u.email.size()));
  put_u32(rec + 52, add_blob(u.phone));
  put_u32(rec + 56, static_cast<uint32_t>(u.phone.size()));
  put_i32(rec + 60, u.level);

  {
    uint8_t slots[kScoreSlotBytes];
    const uint32_t base = static_cast<uint32_t>(values_.size());
    put_u32(rec + 64, static_cast<uint32_t>(u.scores.size()));
    put_u32(rec + 68, base);
    for (const pod::MapEntry& e : u.scores) {
      put_u32(slots + 0, add_blob(e.key));
      put_u32(slots + 4, static_cast<uint32_t>(e.key.size()));
      put_i32(slots + 8, e.value);
      add_values_bytes(slots, kScoreSlotBytes);
    }
  }
  put_u32(rec + 72, 0);
  put_u32(rec + 76, 0);

  // The record was appended above (before the oneof/score slots were filled);
  // rewrite it in place so every slot is consistent.
  std::memcpy(tape_.data() + rec_off, rec, kUserRecordSize);

  ++n_users_;
  roots_.push_back(rec_off);
  return rec_off;
}

void Writer::add_root(uint32_t rec_off) { roots_.push_back(rec_off); }

std::vector<uint8_t> Writer::finish() const {
  Header h;
  h.version = kVersion;
  h.header_bytes = kHeaderBytes;
  h.user_record_size = kUserRecordSize;
  h.profile_record_size = kProfileRecordSize;
  h.n_user_records = n_users_;
  h.n_profile_records = n_profiles_;
  h.flags = flags_;
  h.presence_words = 1;

  const uint32_t roots_bytes = static_cast<uint32_t>(roots_.size() * 4);
  h.roots_count = static_cast<uint32_t>(roots_.size());
  h.roots_off = kHeaderBytes;
  h.tape_off = align8(h.roots_off + roots_bytes);
  h.tape_bytes = static_cast<uint32_t>(tape_.size());
  h.blob_off = align8(h.tape_off + h.tape_bytes);
  h.blob_bytes = static_cast<uint32_t>(blob_.size());
  h.values_off = align8(h.blob_off + h.blob_bytes);
  h.values_bytes = static_cast<uint32_t>(values_.size());
  const uint32_t total = align8(h.values_off + h.values_bytes);

  std::vector<uint8_t> out(total, 0);
  std::memcpy(out.data(), kMagic, 8);
  put_u32(out.data() + 8, h.version);
  put_u32(out.data() + 12, h.header_bytes);
  put_u32(out.data() + 16, h.user_record_size);
  put_u32(out.data() + 20, h.profile_record_size);
  put_u32(out.data() + 24, h.n_user_records);
  put_u32(out.data() + 28, h.n_profile_records);
  put_u32(out.data() + 32, h.roots_off);
  put_u32(out.data() + 36, h.roots_count);
  put_u32(out.data() + 40, h.tape_off);
  put_u32(out.data() + 44, h.tape_bytes);
  put_u32(out.data() + 48, h.blob_off);
  put_u32(out.data() + 52, h.blob_bytes);
  put_u32(out.data() + 56, h.values_off);
  put_u32(out.data() + 60, h.values_bytes);
  put_u32(out.data() + 68, h.flags);
  put_u32(out.data() + 72, h.presence_words);
  put_u32(out.data() + 76, h.reserved);

  // roots | tape | blob | values
  if (roots_bytes) {
    for (size_t i = 0; i < roots_.size(); ++i) put_u32(out.data() + h.roots_off + i * 4, roots_[i]);
  }
  if (!tape_.empty()) std::memcpy(out.data() + h.tape_off, tape_.data(), tape_.size());
  if (!blob_.empty()) std::memcpy(out.data() + h.blob_off, blob_.data(), blob_.size());
  if (!values_.empty()) std::memcpy(out.data() + h.values_off, values_.data(), values_.size());

  // crc32 over the four sections in a fixed order
  uint32_t crc = 0;
  crc = crc32(out.data() + h.roots_off, roots_bytes, crc);
  crc = crc32(out.data() + h.tape_off, h.tape_bytes, crc);
  crc = crc32(out.data() + h.blob_off, h.blob_bytes, crc);
  crc = crc32(out.data() + h.values_off, h.values_bytes, crc);
  put_u32(out.data() + 64, crc);
  return out;
}

// ---- verifier -----------------------------------------------------------
Header parse_header(const uint8_t* data, size_t size) {
  Header h;
  if (data == nullptr || size < kHeaderBytes) return h;
  h.version = get_u32(data + 8);
  h.header_bytes = get_u32(data + 12);
  h.user_record_size = get_u32(data + 16);
  h.profile_record_size = get_u32(data + 20);
  h.n_user_records = get_u32(data + 24);
  h.n_profile_records = get_u32(data + 28);
  h.roots_off = get_u32(data + 32);
  h.roots_count = get_u32(data + 36);
  h.tape_off = get_u32(data + 40);
  h.tape_bytes = get_u32(data + 44);
  h.blob_off = get_u32(data + 48);
  h.blob_bytes = get_u32(data + 52);
  h.values_off = get_u32(data + 56);
  h.values_bytes = get_u32(data + 60);
  h.crc32 = get_u32(data + 64);
  h.flags = get_u32(data + 68);
  h.presence_words = get_u32(data + 72);
  h.reserved = get_u32(data + 76);
  return h;
}

uint32_t section_crc(const uint8_t* data, size_t size, const Header& h) {
  if (data == nullptr || size < kHeaderBytes) return 0;
  if (!range_ok(h.roots_off, static_cast<uint64_t>(h.roots_count) * 4, 0, size)) return 0;
  if (!range_ok(h.tape_off, h.tape_bytes, 0, size)) return 0;
  if (!range_ok(h.blob_off, h.blob_bytes, 0, size)) return 0;
  if (!range_ok(h.values_off, h.values_bytes, 0, size)) return 0;
  uint32_t crc = 0;
  if (h.roots_count) crc = crc32(data + h.roots_off, static_cast<size_t>(h.roots_count) * 4, crc);
  crc = crc32(data + h.tape_off, h.tape_bytes, crc);
  crc = crc32(data + h.blob_off, h.blob_bytes, crc);
  crc = crc32(data + h.values_off, h.values_bytes, crc);
  return crc;
}

VerifyReport verify(const uint8_t* data, size_t size, const Limits& lim) {
  VerifyReport rep;
  rep.total_bytes = size;
  auto bad = [&](const char* what) {
    rep.ok = false;
    rep.error = what;
    return rep;
  };

  if (data == nullptr || size < kHeaderBytes) return bad("E_TAPE_SIZE: shorter than header");
  if (std::memcmp(data, kMagic, 8) != 0) return bad("E_TAPE_MAGIC");

  Header h;
  h.version = get_u32(data + 8);
  h.header_bytes = get_u32(data + 12);
  h.user_record_size = get_u32(data + 16);
  h.profile_record_size = get_u32(data + 20);
  h.n_user_records = get_u32(data + 24);
  h.n_profile_records = get_u32(data + 28);
  h.roots_off = get_u32(data + 32);
  h.roots_count = get_u32(data + 36);
  h.tape_off = get_u32(data + 40);
  h.tape_bytes = get_u32(data + 44);
  h.blob_off = get_u32(data + 48);
  h.blob_bytes = get_u32(data + 52);
  h.values_off = get_u32(data + 56);
  h.values_bytes = get_u32(data + 60);
  h.crc32 = get_u32(data + 64);
  h.flags = get_u32(data + 68);
  h.presence_words = get_u32(data + 72);
  h.reserved = get_u32(data + 76);
  rep.header = h;

  if (h.version != kVersion) return bad("E_TAPE_VERSION");
  if (h.header_bytes != kHeaderBytes) return bad("E_TAPE_HEADER_BYTES");
  if (h.user_record_size != kUserRecordSize) return bad("E_TAPE_USER_RECORD_SIZE");
  if (h.profile_record_size != kProfileRecordSize) return bad("E_TAPE_PROFILE_RECORD_SIZE");
  if (h.presence_words != 1) return bad("E_TAPE_PRESENCE_WORDS");
  if (h.roots_count > lim.max_objects) return bad("E_TAPE_ROOTS_BUDGET");
  if (h.tape_bytes > lim.max_tape_bytes) return bad("E_TAPE_TAPE_BUDGET");
  if (h.blob_bytes > lim.max_blob_bytes) return bad("E_TAPE_BLOB_BUDGET");
  if (h.values_bytes > lim.max_values_bytes) return bad("E_TAPE_VALUES_BUDGET");

  // section bounds + alignment
  if (h.roots_off < kHeaderBytes || h.roots_off % 8 != 0) return bad("E_TAPE_ROOTS_OFF");
  if (!range_ok(h.roots_off, static_cast<uint64_t>(h.roots_count) * 4, 0, size))
    return bad("E_TAPE_ROOTS_RANGE");
  if (h.tape_off % 8 != 0) return bad("E_TAPE_TAPE_OFF_ALIGN");
  if (!range_ok(h.tape_off, h.tape_bytes, 0, size)) return bad("E_TAPE_TAPE_RANGE");
  if (h.blob_off % 8 != 0) return bad("E_TAPE_BLOB_OFF_ALIGN");
  if (!range_ok(h.blob_off, h.blob_bytes, 0, size)) return bad("E_TAPE_BLOB_RANGE");
  if (h.values_off % 8 != 0) return bad("E_TAPE_VALUES_OFF_ALIGN");
  if (!range_ok(h.values_off, h.values_bytes, 0, size)) return bad("E_TAPE_VALUES_RANGE");

  // Section order: header | roots | tape | blob | values, no overlap.
  {
    const uint64_t roots_end = static_cast<uint64_t>(h.roots_off) +
                               static_cast<uint64_t>(h.roots_count) * 4;
    const uint64_t tape_end = static_cast<uint64_t>(h.tape_off) + h.tape_bytes;
    const uint64_t blob_end = static_cast<uint64_t>(h.blob_off) + h.blob_bytes;
    const uint64_t values_end = static_cast<uint64_t>(h.values_off) + h.values_bytes;
    if (h.roots_off != kHeaderBytes) return bad("E_TAPE_ROOTS_OFF");
    if (roots_end > h.tape_off) return bad("E_TAPE_SECTION_ORDER_ROOTS_TAPE");
    if (tape_end > h.blob_off) return bad("E_TAPE_SECTION_ORDER_TAPE_BLOB");
    if (blob_end > h.values_off) return bad("E_TAPE_SECTION_ORDER_BLOB_VALUES");
    if (values_end > size) return bad("E_TAPE_SECTION_END");
  }

  // crc32
  uint32_t crc = 0;
  if (h.roots_count) crc = crc32(data + h.roots_off, static_cast<size_t>(h.roots_count) * 4, crc);
  crc = crc32(data + h.tape_off, h.tape_bytes, crc);
  crc = crc32(data + h.blob_off, h.blob_bytes, crc);
  crc = crc32(data + h.values_off, h.values_bytes, crc);
  if (crc != h.crc32) return bad("E_TAPE_CRC32");

  // records
  uint32_t profiles = 0;
  for (uint32_t i = 0; i < h.roots_count; ++i) {
    const uint32_t rec_off = get_u32(data + h.roots_off + i * 4);
    if (rec_off % 8 != 0) return bad("E_TAPE_ROOT_ALIGN");
    if (!range_ok(rec_off, kUserRecordSize, 0, h.tape_bytes)) return bad("E_TAPE_ROOT_RANGE");
    const uint8_t* rec = data + h.tape_off + rec_off;
    const uint32_t presence = get_u32(rec + 0);
    const uint32_t contact_bits = get_u32(rec + 4);
    if ((presence & ~0xFFu) != 0) return bad("E_TAPE_PRESENCE_BITS");
    if ((contact_bits & ~((1u << 5) | (1u << 6))) != 0) return bad("E_TAPE_ONEOF_BITS");
    // blob refs
    const uint32_t refs[6][2] = {{24, 28}, {44, 48}, {52, 56}, {0, 0}, {0, 0}, {0, 0}};
    for (int k = 0; k < 3; ++k) {
      const uint32_t off = get_u32(rec + refs[k][0]);
      const uint32_t len = get_u32(rec + refs[k][1]);
      if (!range_ok(off, len, 0, h.blob_bytes)) return bad("E_TAPE_BLOB_REF");
    }
    // values: tags
    {
      const uint32_t count = get_u32(rec + 32);
      const uint32_t voff = get_u32(rec + 36);
      if (count == 0) {
        if (voff > h.values_bytes) return bad("E_TAPE_TAGS_OFF");
      } else {
        if (voff % 4 != 0) return bad("E_TAPE_TAGS_ALIGN");
        if (!range_ok(voff, static_cast<uint64_t>(count) * kTagSlotBytes, 0, h.values_bytes))
          return bad("E_TAPE_TAGS_RANGE");
        for (uint32_t t = 0; t < count; ++t) {
          const uint8_t* slot = data + h.values_off + voff + t * kTagSlotBytes;
          if (!range_ok(get_u32(slot), get_u32(slot + 4), 0, h.blob_bytes))
            return bad("E_TAPE_TAG_SLOT_REF");
        }
      }
    }
    // values: scores
    {
      const uint32_t count = get_u32(rec + 64);
      const uint32_t voff = get_u32(rec + 68);
      if (count == 0) {
        if (voff > h.values_bytes) return bad("E_TAPE_SCORES_OFF");
      } else {
        if (voff % 4 != 0) return bad("E_TAPE_SCORES_ALIGN");
        if (!range_ok(voff, static_cast<uint64_t>(count) * kScoreSlotBytes, 0, h.values_bytes))
          return bad("E_TAPE_SCORES_RANGE");
        for (uint32_t s = 0; s < count; ++s) {
          const uint8_t* slot = data + h.values_off + voff + s * kScoreSlotBytes;
          if (!range_ok(get_u32(slot), get_u32(slot + 4), 0, h.blob_bytes))
            return bad("E_TAPE_SCORE_SLOT_REF");
        }
      }
    }
    // profile pointer
    const uint32_t prof_off = get_u32(rec + 40);
    if (prof_off != kAbsent) {
      if (prof_off % 8 != 0) return bad("E_TAPE_PROFILE_ALIGN");
      if (!range_ok(prof_off, kProfileRecordSize, 0, h.tape_bytes))
        return bad("E_TAPE_PROFILE_RANGE");
      const uint8_t* pr = data + h.tape_off + prof_off;
      if (!range_ok(get_u32(pr + 0), get_u32(pr + 4), 0, h.blob_bytes)) return bad("E_TAPE_PROFILE_BLOB_REF");
      if ((get_u32(pr + 12) & 1u) == 0) return bad("E_TAPE_PROFILE_FLAG");
      ++profiles;
    }
    ++rep.users_seen;
  }
  rep.profiles_seen = profiles;
  if (rep.users_seen != h.n_user_records) return bad("E_TAPE_USER_COUNT");
  if (profiles != h.n_profile_records) return bad("E_TAPE_PROFILE_COUNT");
  if ((h.tape_bytes % 8) != 0) return bad("E_TAPE_TAPE_BYTES_ALIGN");
  rep.ok = true;
  return rep;
}

// ---- reader -------------------------------------------------------------
Reader::Reader(const uint8_t* data, size_t size, const Limits& limits)
    : data_(data), size_(size), lim_(limits) {}

Code Reader::load() {
  VerifyReport rep = verify(data_, size_, lim_);
  if (!rep.ok) return Code::Internal;
  h_ = rep.header;
  loaded_ = true;
  return Code::Ok;
}

std::string_view Reader::blob(uint32_t off, uint32_t len) const {
  if (!loaded_) return {};
  if (!range_ok(off, len, 0, h_.blob_bytes)) return {};
  return std::string_view(reinterpret_cast<const char*>(data_ + h_.blob_off + off), len);
}

uint32_t Reader::root_at(uint32_t i) const {
  return get_u32(data_ + h_.roots_off + i * 4);
}

Code Reader::user_at(uint32_t rec_off, pod::User* out) const {
  if (!loaded_) return Code::Internal;
  if (!range_ok(rec_off, kUserRecordSize, 0, h_.tape_bytes)) return Code::Internal;
  const uint8_t* rec = data_ + h_.tape_off + rec_off;
  out->clear();
  out->presence = get_u32(rec + 0);
  out->contact_bits = get_u32(rec + 4);
  out->oneof_case = get_u32(rec + 8);
  out->unknown_fields = get_u32(rec + 12);
  out->id = static_cast<int64_t>(get_u64(rec + 16));
  out->name.assign(blob(get_u32(rec + 24), get_u32(rec + 28)));

  const uint32_t tag_count = get_u32(rec + 32);
  const uint32_t tag_off = get_u32(rec + 36);
  if (tag_count > lim_.max_repeated_elements) return Code::FieldBudget;
  out->tags.reserve(tag_count);
  for (uint32_t i = 0; i < tag_count; ++i) {
    const uint8_t* slot = data_ + h_.values_off + tag_off + i * kTagSlotBytes;
    out->tags.emplace_back(blob(get_u32(slot), get_u32(slot + 4)));
  }

  const uint32_t prof_off = get_u32(rec + 40);
  if (prof_off != kAbsent) {
    const uint8_t* pr = data_ + h_.tape_off + prof_off;
    out->profile.present = true;
    out->profile.city.assign(blob(get_u32(pr + 0), get_u32(pr + 4)));
    out->profile.age = get_i32(pr + 8);
  }
  out->email.assign(blob(get_u32(rec + 44), get_u32(rec + 48)));
  out->phone.assign(blob(get_u32(rec + 52), get_u32(rec + 56)));
  out->level = get_i32(rec + 60);
  out->has_level = (out->presence & (1u << (pod::f::kUserLevel - 1))) != 0;

  const uint32_t score_count = get_u32(rec + 64);
  const uint32_t score_off = get_u32(rec + 68);
  if (score_count > lim_.max_repeated_elements) return Code::FieldBudget;
  out->scores.reserve(score_count);
  for (uint32_t i = 0; i < score_count; ++i) {
    const uint8_t* slot = data_ + h_.values_off + score_off + i * kScoreSlotBytes;
    pod::MapEntry e;
    e.key.assign(blob(get_u32(slot), get_u32(slot + 4)));
    e.value = get_i32(slot + 8);
    out->scores.push_back(std::move(e));
  }
  return Code::Ok;
}

Code Reader::batch(pod::UserBatch* out) const {
  if (!loaded_) return Code::Internal;
  out->clear();
  out->users.resize(h_.n_user_records);
  for (uint32_t i = 0; i < h_.n_user_records; ++i) {
    Code e = user_at(root_at(i), &out->users[i]);
    if (e != Code::Ok) return e;
  }
  return Code::Ok;
}

}  // namespace tape
}  // namespace q207
