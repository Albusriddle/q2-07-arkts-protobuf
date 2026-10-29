// HOST-ONLY CLI: wire bytes -> POD -> (norm-v1 JSON | tape v0) + fingerprint metadata.
//
// Used by the differential harness:
//   host_decode_dump --corpus <dir> --out <dir> --meta <meta.json>
//        decodes every *.wire file (one User message each) found in <dir>
//   host_decode_dump --framed <card.bin> --out <dir> --stem <name>
//        decodes a data card (length-prefixed UserBatch frames) and writes the
//        norm JSON + tape of the first frame (used for the materializer check)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "host/common/sha256.h"
#include "host/decoder/decode_user.h"
#include "host/decoder/norm.h"
#include "host/decoder/pod.h"
#include "host/tape/tape.h"

namespace fs = std::filesystem;
using namespace q207;

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) {
    std::fclose(f);
    return false;
  }
  out->resize(static_cast<size_t>(n));
  const size_t got = out->empty() ? 0 : std::fread(out->data(), 1, out->size(), f);
  std::fclose(f);
  return got == out->size();
}

bool write_file(const std::string& path, const void* data, size_t n) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const size_t put = n ? std::fwrite(data, 1, n, f) : 0;
  std::fclose(f);
  return put == n;
}

bool write_text(const std::string& path, const std::string& s) {
  return write_file(path, s.data(), s.size());
}

struct Item {
  std::string stem;
  std::string present_fields = "[]";   // JSON array; valid even on a failed item
  std::string profile_fields = "[]";   // JSON array; valid even on a failed item
  uint32_t oneof_case = 0;
  std::string code;
  std::string wire_sha256;
  std::string norm_sha256;
  std::string tape_sha256;
  uint64_t wire_bytes = 0;
  uint32_t objects = 0;
  uint32_t fields = 0;
  uint32_t unknown_fields = 0;
  uint64_t tape_bytes = 0;
  bool tape_verified = false;
  bool ok = false;
};

std::string json_escape(const std::string& s) { return norm::json_escape(s); }

std::string item_json(const Item& it) {
  std::string o = "{";
  o += "\"stem\":\"" + json_escape(it.stem) + "\"";
  o += ",\"code\":\"" + it.code + "\"";
  o += ",\"wire_bytes\":" + std::to_string(it.wire_bytes);
  o += ",\"wire_sha256\":\"" + it.wire_sha256 + "\"";
  o += ",\"objects\":" + std::to_string(it.objects);
  o += ",\"fields\":" + std::to_string(it.fields);
  o += ",\"unknown_fields\":" + std::to_string(it.unknown_fields);
  o += ",\"norm_sha256\":\"" + it.norm_sha256 + "\"";
  o += ",\"tape_bytes\":" + std::to_string(it.tape_bytes);
  o += ",\"tape_sha256\":\"" + it.tape_sha256 + "\"";
  o += ",\"tape_verified\":";
  o += it.tape_verified ? "true" : "false";
  o += ",\"present_fields\":" + it.present_fields;
  o += ",\"profile_fields\":" + it.profile_fields;
  o += ",\"oneof_case\":" + std::to_string(it.oneof_case);
  o += "}";
  return o;
}

// Writes norm + tape for one decoded item.
std::string fields_json(const pod::User& user) {
  std::string o = "[";
  bool first = true;
  for (uint32_t f = 1; f <= pod::f::kUserMaxField; ++f) {
    if (!user.present(f)) continue;
    if (!first) o += ",";
    first = false;
    o += std::to_string(f);
  }
  o += "]";
  return o;
}

std::string profile_fields_json(const pod::User& user) {
  std::string o = "[";
  bool first = true;
  for (uint32_t f = 1; f <= 2; ++f) {
    if (!user.profile.present_field(f)) continue;
    if (!first) o += ",";
    first = false;
    o += std::to_string(f);
  }
  o += "]";
  return o;
}

void emit(const pod::User& user, const std::string& out_dir, const std::string& stem,
          const std::vector<uint8_t>& wire, Item* it) {
  it->stem = stem;
  it->present_fields = fields_json(user);
  it->profile_fields = profile_fields_json(user);
  it->oneof_case = user.oneof_case;
  it->wire_bytes = wire.size();
  it->wire_sha256 = sha256_hex(wire.data(), wire.size());
  const std::string norm = norm::user_json(user);
  it->norm_sha256 = sha256_hex(norm.data(), norm.size());
  write_text(out_dir + "/" + stem + ".norm.json", norm);

  tape::Writer w;
  w.add_user(user);
  std::vector<uint8_t> buf = w.finish();
  tape::VerifyReport rep = tape::verify(buf.data(), buf.size());
  it->tape_verified = rep.ok;
  it->tape_bytes = buf.size();
  it->tape_sha256 = sha256_hex(buf.data(), buf.size());
  write_file(out_dir + "/" + stem + ".tape.bin", buf.data(), buf.size());
}

void emit_batch(const pod::UserBatch& batch, const std::string& out_dir, const std::string& stem,
                const std::vector<uint8_t>& wire, Item* it) {
  it->stem = stem;
  it->wire_bytes = wire.size();
  it->wire_sha256 = sha256_hex(wire.data(), wire.size());
  const std::string norm = norm::user_batch_json(batch);
  it->norm_sha256 = sha256_hex(norm.data(), norm.size());
  write_text(out_dir + "/" + stem + ".norm.json", norm);

  tape::Writer w;
  for (const pod::User& u : batch.users) w.add_user(u);
  std::vector<uint8_t> buf = w.finish();
  tape::VerifyReport rep = tape::verify(buf.data(), buf.size());
  it->tape_verified = rep.ok;
  it->tape_bytes = buf.size();
  it->tape_sha256 = sha256_hex(buf.data(), buf.size());
  write_file(out_dir + "/" + stem + ".tape.bin", buf.data(), buf.size());
}

int usage() {
  std::fprintf(stderr,
               "usage:\n"
               "  host_decode_dump --corpus <dir> --out <dir> --meta <meta.json>\n"
               "  host_decode_dump --framed <card.bin> --out <dir> --stem <name>\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string corpus, out_dir, meta_path, framed, stem = "card";
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](std::string* dst) {
      if (i + 1 >= argc) return false;
      *dst = argv[++i];
      return true;
    };
    if (a == "--corpus") {
      if (!next(&corpus)) return usage();
    } else if (a == "--out") {
      if (!next(&out_dir)) return usage();
    } else if (a == "--meta") {
      if (!next(&meta_path)) return usage();
    } else if (a == "--framed") {
      if (!next(&framed)) return usage();
    } else if (a == "--stem") {
      if (!next(&stem)) return usage();
    } else {
      return usage();
    }
  }
  if (out_dir.empty()) return usage();

  if (!corpus.empty()) {
    if (meta_path.empty()) return usage();
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(corpus, ec)) {
      if (!e.is_regular_file()) continue;
      const std::string name = e.path().filename().string();
      if (name.size() > 5 && name.compare(name.size() - 5, 5, ".wire") == 0) files.push_back(name);
    }
    if (ec) {
      std::fprintf(stderr, "cannot read corpus dir %s\n", corpus.c_str());
      return 3;
    }
    std::sort(files.begin(), files.end());
    std::string items;
    int failures = 0;
    for (const std::string& name : files) {
      const std::string path = corpus + "/" + name;
      std::vector<uint8_t> wire;
      if (!read_file(path, &wire)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 3;
      }
      const std::string s = name.substr(0, name.size() - 5);
      pod::User u;
      decode::Stats st;
      const Code code = decode::user(wire.data(), wire.size(), Limits(), &u, &st);
      Item it;
      it.code = code_name(code);
      it.objects = st.objects;
      it.fields = st.fields;
      it.unknown_fields = st.unknown_fields;
      if (code == Code::Ok) {
        emit(u, out_dir, s, wire, &it);
        it.ok = it.tape_verified;
        if (!it.tape_verified) ++failures;
      } else {
        it.stem = s;
        it.wire_bytes = wire.size();
        it.wire_sha256 = sha256_hex(wire.data(), wire.size());
        ++failures;
      }
      if (!items.empty()) items += ",";
      items += item_json(it);
      std::printf("%-28s %-28s wire=%zu\n", s.c_str(), it.code.c_str(), wire.size());
    }
    std::string meta = "{\"tool\":\"host_decode_dump\",\"scope\":\"HOST-ONLY\",\"items\":[";
    meta += items;
    meta += "]}";
    write_text(meta_path, meta);
    return failures == 0 ? 0 : 4;
  }

  if (!framed.empty()) {
    std::vector<uint8_t> card;
    if (!read_file(framed, &card)) {
      std::fprintf(stderr, "cannot read %s\n", framed.c_str());
      return 3;
    }
    // card = repeated [u32 LE payload_len][UserBatch bytes]
    if (card.size() < 4) {
      std::fprintf(stderr, "card too short\n");
      return 3;
    }
    const uint32_t len = static_cast<uint32_t>(card[0]) | (static_cast<uint32_t>(card[1]) << 8) |
                         (static_cast<uint32_t>(card[2]) << 16) |
                         (static_cast<uint32_t>(card[3]) << 24);
    if (static_cast<uint64_t>(4) + len > card.size()) {
      std::fprintf(stderr, "frame 0 overruns card\n");
      return 3;
    }
    std::vector<uint8_t> frame(card.begin() + 4, card.begin() + 4 + len);
    pod::UserBatch batch;
    decode::Stats st;
    const Code code = decode::user_batch(frame.data(), frame.size(), Limits(), &batch, &st);
    Item it;
    it.code = code_name(code);
    it.objects = st.objects;
    it.fields = st.fields;
    it.unknown_fields = st.unknown_fields;
    std::printf("%s frame0: %s users=%zu wire=%zu\n", stem.c_str(), it.code.c_str(),
                batch.users.size(), frame.size());
    if (code != Code::Ok) {
      std::fprintf(stderr, "decode failed: %s\n", code_name(code));
      return 4;
    }
    emit_batch(batch, out_dir, stem, frame, &it);
    std::string meta = "{\"tool\":\"host_decode_dump\",\"scope\":\"HOST-ONLY\",\"items\":[";
    meta += item_json(it);
    meta += "]}";
    write_text(out_dir + "/" + stem + ".meta.json", meta);
    std::printf("  tape=%llu bytes verified=%d\n",
                static_cast<unsigned long long>(it.tape_bytes), it.tape_verified ? 1 : 0);
    return it.tape_verified ? 0 : 5;
  }

  return usage();
}
