// HOST-ONLY benchmark: wire decode throughput + tape write cost.
//
// Produces CSV rows with scope=HOST-ONLY.  These numbers are measured on the
// development PC with a native host compiler; they are NOT device numbers, NOT a
// 1.5x claim and NOT a 700 ms claim.  The data card path, size and sha256 are
// recorded next to every row so a row can never be quoted without its dataset.
//
// Card format (data-card convention, not protobuf):
//   repeated [u32 little-endian payload length][UserBatch message bytes]
// Frames exist because a single protobuf message is capped at 64 MiB by design
// (report v3 §5.5.8): the 100 MB card is split on message boundaries, not bytes.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "host/common/sha256.h"
#include "host/decoder/decode_user.h"
#include "host/decoder/pod.h"
#include "host/tape/tape.h"

using namespace q207;

namespace {

struct Frame {
  size_t off = 0;
  uint32_t len = 0;
};

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

std::string csv_escape(const std::string& s) {
  if (s.find(',') == std::string::npos && s.find('"') == std::string::npos) return s;
  std::string o = "\"";
  for (char c : s) {
    if (c == '"') o += "\"\"";
    else o += c;
  }
  o += "\"";
  return o;
}

std::string compiler_string() {
#if defined(_MSC_VER)
  char buf[64];
  std::snprintf(buf, sizeof(buf), "MSVC 19.%02d (cl %d)", _MSC_VER % 100, _MSC_VER);
  return buf;
#elif defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("gcc ") + __VERSION__;
#else
  return "unknown";
#endif
}

std::string os_string() {
#if defined(_WIN32)
  return "Windows (host native)";
#elif defined(__linux__)
  return "Linux (host native)";
#else
  return "unknown host";
#endif
}

double median_ms(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Row {
  std::string metric;
  std::string value;
  std::string unit;
};

int usage() {
  std::fprintf(stderr,
               "usage: host_bench --card <path> --dataset <name> --reps <n> --csv <file> "
               "[--csv-header]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string card_path, dataset, csv_path;
  int reps = 5;
  bool header = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](std::string* dst) {
      if (i + 1 >= argc) return false;
      *dst = argv[++i];
      return true;
    };
    if (a == "--card") {
      if (!next(&card_path)) return usage();
    } else if (a == "--dataset") {
      if (!next(&dataset)) return usage();
    } else if (a == "--reps") {
      std::string s;
      if (!next(&s)) return usage();
      reps = std::atoi(s.c_str());
      if (reps < 1) reps = 1;
    } else if (a == "--csv") {
      if (!next(&csv_path)) return usage();
    } else if (a == "--csv-header") {
      header = true;
    } else {
      return usage();
    }
  }
  if (card_path.empty() || dataset.empty() || csv_path.empty()) return usage();

  std::vector<uint8_t> card;
  if (!read_file(card_path, &card)) {
    std::fprintf(stderr, "cannot read card %s\n", card_path.c_str());
    return 3;
  }
  const std::string card_sha = sha256_hex(card.data(), card.size());

  // ---- split into frames ------------------------------------------------
  std::vector<Frame> frames;
  size_t pos = 0;
  uint64_t payload = 0;
  while (pos + 4 <= card.size()) {
    const uint32_t len = static_cast<uint32_t>(card[pos]) |
                         (static_cast<uint32_t>(card[pos + 1]) << 8) |
                         (static_cast<uint32_t>(card[pos + 2]) << 16) |
                         (static_cast<uint32_t>(card[pos + 3]) << 24);
    if (static_cast<uint64_t>(pos) + 4 + len > card.size()) {
      std::fprintf(stderr, "frame overruns card at %zu (len=%u)\n", pos, len);
      return 4;
    }
    Frame fr;
    fr.off = pos + 4;
    fr.len = len;
    frames.push_back(fr);
    payload += len;
    pos += 4 + len;
  }
  if (frames.empty() || pos != card.size()) {
    std::fprintf(stderr, "malformed card: frames=%zu consumed=%zu size=%zu\n", frames.size(), pos,
                 card.size());
    return 4;
  }

  // ---- wire decode pass --------------------------------------------------
  std::printf("dataset=%s card=%s bytes=%zu payload=%llu frames=%zu reps=%d\n", dataset.c_str(),
              card_path.c_str(), card.size(), static_cast<unsigned long long>(payload),
              frames.size(), reps);
  pod::UserBatch batch;
  uint64_t messages = 0;
  uint64_t fields = 0;
  {
    decode::Stats st;
    Code e = decode::user_batch(card.data() + frames[0].off, frames[0].len, Limits(), &batch, &st);
    if (e != Code::Ok) {
      std::fprintf(stderr, "warmup decode failed: %s\n", code_name(e));
      return 5;
    }
    messages = batch.users.size();
    fields = st.fields;
    for (size_t f = 1; f < frames.size(); ++f) {
      e = decode::user_batch(card.data() + frames[f].off, frames[f].len, Limits(), &batch, &st);
      if (e != Code::Ok) {
        std::fprintf(stderr, "warmup decode failed at frame %zu: %s\n", f, code_name(e));
        return 5;
      }
      messages += batch.users.size();
      fields += st.fields;
    }
    std::printf("  warmup: %llu messages, %llu fields\n",
                static_cast<unsigned long long>(messages),
                static_cast<unsigned long long>(fields));
  }

  std::vector<double> decode_ms;
  for (int rep = 0; rep < reps; ++rep) {
    const auto t0 = std::chrono::steady_clock::now();
    for (const Frame& fr : frames) {
      decode::Stats st;
      Code e = decode::user_batch(card.data() + fr.off, fr.len, Limits(), &batch, &st);
      if (e != Code::Ok) {
        std::fprintf(stderr, "decode failed: %s\n", code_name(e));
        return 5;
      }
    }
    const auto t1 = std::chrono::steady_clock::now();
    decode_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  const double dec_ms = median_ms(decode_ms);
  const double mbps = dec_ms > 0 ? (static_cast<double>(payload) / 1e6) / (dec_ms / 1000.0) : 0.0;
  const double msg_s = dec_ms > 0 ? static_cast<double>(messages) / (dec_ms / 1000.0) : 0.0;
  const double ns_msg = messages ? (dec_ms * 1e6) / static_cast<double>(messages) : 0.0;

  // ---- tape write pass ---------------------------------------------------
  std::vector<double> tape_ms;
  uint64_t tape_bytes_total = 0;
  std::string tape_first_sha;
  for (int rep = 0; rep < reps; ++rep) {
    uint64_t bytes = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t f = 0; f < frames.size(); ++f) {
      decode::Stats st;
      Code e = decode::user_batch(card.data() + frames[f].off, frames[f].len, Limits(), &batch, &st);
      if (e != Code::Ok) {
        std::fprintf(stderr, "tape-pass decode failed: %s\n", code_name(e));
        return 5;
      }
      tape::Writer w;
      for (const pod::User& u : batch.users) w.add_user(u);
      std::vector<uint8_t> buf = w.finish();
      bytes += buf.size();
      if (rep == 0 && f == 0) {
        tape::VerifyReport rep0 = tape::verify(buf.data(), buf.size());
        if (!rep0.ok) {
          std::fprintf(stderr, "tape verify failed: %s\n", rep0.error.c_str());
          return 5;
        }
        tape_first_sha = sha256_hex(buf.data(), buf.size());
      }
    }
    const auto t1 = std::chrono::steady_clock::now();
    tape_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    tape_bytes_total = bytes;
  }
  const double tw_ms = median_ms(tape_ms);
  const double tw_ns_msg = messages ? (tw_ms * 1e6) / static_cast<double>(messages) : 0.0;
  // The tape pass includes the wire decode it feeds on; report the difference as
  // a derived metric so nobody reads it as pure tape cost.
  const double tape_only_ms = tw_ms > dec_ms ? (tw_ms - dec_ms) : 0.0;
  const double tape_only_ns_msg =
      messages ? (tape_only_ms * 1e6) / static_cast<double>(messages) : 0.0;

  // ---- CSV ---------------------------------------------------------------
  std::vector<Row> rows = {
      {"wire_decode_mb_per_s", std::to_string(mbps), "MB/s (payload bytes/1e6 per s)"},
      {"wire_decode_msg_per_s", std::to_string(msg_s), "messages/s"},
      {"wire_decode_ns_per_msg", std::to_string(ns_msg), "ns/message"},
      {"wire_decode_median_ms", std::to_string(dec_ms), "ms/full card"},
      {"tape_write_median_ms", std::to_string(tw_ms), "ms/full card (decode + tape write)"},
      {"tape_write_only_median_ms", std::to_string(tape_only_ms),
       "ms/full card (derived: tape pass minus decode pass)"},
      {"tape_write_only_ns_per_msg", std::to_string(tape_only_ns_msg), "ns/message (derived)"},
      {"tape_write_ns_per_msg", std::to_string(tw_ns_msg), "ns/message"},
      {"tape_bytes_total", std::to_string(tape_bytes_total), "bytes/full card"},
      {"tape_bytes_per_msg",
       std::to_string(messages ? static_cast<double>(tape_bytes_total) / static_cast<double>(messages)
                               : 0.0),
       "bytes/message"},
      {"tape_first_frame_sha256", tape_first_sha, "hex"},
      {"card_payload_bytes", std::to_string(payload), "bytes"},
      {"frames", std::to_string(frames.size()), "count"},
      {"messages", std::to_string(messages), "count"},
      {"fields", std::to_string(fields), "count"},
  };

  const std::string compiler = compiler_string();
  const std::string os = os_string();
  std::FILE* f = std::fopen(csv_path.c_str(), header ? "wb" : "ab");
  if (f == nullptr) {
    std::fprintf(stderr, "cannot open csv %s\n", csv_path.c_str());
    return 3;
  }
  if (header) {
    std::fprintf(f,
                 "scope,dataset,card_file,card_bytes,card_sha256,payload_bytes,frames,messages,"
                 "fields,reps,metric,value,unit,compiler,host_os\n");
  }
  for (const Row& r : rows) {
    std::fprintf(f, "HOST-ONLY,%s,%s,%zu,%s,%llu,%zu,%llu,%llu,%d,%s,%s,%s,%s,%s\n",
                 csv_escape(dataset).c_str(), csv_escape(card_path).c_str(), card.size(),
                 card_sha.c_str(), static_cast<unsigned long long>(payload), frames.size(),
                 static_cast<unsigned long long>(messages),
                 static_cast<unsigned long long>(fields), reps, r.metric.c_str(),
                 csv_escape(r.value).c_str(), csv_escape(r.unit).c_str(),
                 csv_escape(compiler).c_str(), csv_escape(os).c_str());
  }
  std::fclose(f);

  std::printf("  wire decode: %.3f ms  -> %.2f MB/s, %.0f msg/s, %.1f ns/msg\n", dec_ms, mbps,
              msg_s, ns_msg);
  std::printf("  tape write : %.3f ms  -> %.1f ns/msg, %.2f bytes/msg, first-frame sha %s\n",
              tw_ms, tw_ns_msg,
              messages ? static_cast<double>(tape_bytes_total) / static_cast<double>(messages) : 0.0,
              tape_first_sha.substr(0, 16).c_str());
  std::printf("  CSV rows appended: %zu (scope=HOST-ONLY)\n", rows.size());
  return 0;
}
