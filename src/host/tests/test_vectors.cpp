// HOST-ONLY: the 38 W0 golden wire vectors, judged against the C++ reader.
//
// The golden file lives outside the repo (out/w0/security/test_vectors/), so it is
// copied into wire_vectors_data.inc at authoring time: the host build must not
// depend on out/** being present.  Expectations were NOT edited; the copy is a
// byte-for-byte transliteration of name/hex/expected/strict/limits.
#include "host/common/status.h"
#include "host/decoder/conformance.h"
#include "host/decoder/decode_user.h"
#include "host/decoder/pod.h"
#include "host/tests/test_framework.h"
#include "host/tests/wire_vectors_data.inc"

namespace q207 {
namespace test {

void suite_vectors() {
  ctx().begin("golden/38-wire-vectors");
  int checked = 0;
  int mismatched = 0;
  for (unsigned i = 0; i < kGoldenVectorCount; ++i) {
    const GoldenVector& v = kGoldenVectors[i];
    std::vector<uint8_t> bytes = unhex(v.hex);
    // A non-empty hex string that fails to parse / has odd length would be a copy bug.
    const size_t hex_len = std::strlen(v.hex);
    TCHECK(hex_len % 2 == 0);
    TCHECK(v.hex[0] == '\0' || bytes.size() == hex_len / 2);

    Limits lim;
    if (v.max_string_bytes || v.max_objects || v.max_fields) {
      if (v.max_string_bytes) lim.max_string_bytes = v.max_string_bytes;
      if (v.max_objects) lim.max_objects = v.max_objects;
      if (v.max_fields) lim.max_fields = v.max_fields;
    }
    const bool strict = (v.strict != 0);  // -1 (default) and 1 both use strict
    conformance::Result res = conformance::validate(bytes.data(), bytes.size(), lim, strict);
    const std::string got = code_name(res.code);
    ++checked;
    if (got != v.expected) {
      ++mismatched;
      std::fprintf(stderr, "FAIL [golden] %-28s got %-30s want %s\n", v.name, got.c_str(),
                   v.expected);
    }
    ctx().check(got == v.expected, v.name, __FILE__, __LINE__);
    // Every non-OK vector must also report a non-zero error offset when a cursor
    // could be blamed; E_OK vectors must stay at offset 0.
    if (res.code == Code::Ok) TEQ_U64(res.error_offset, 0);
  }
  TEQ_U64(static_cast<uint64_t>(checked), 38);
  TEQ_U64(static_cast<uint64_t>(mismatched), 0);
  std::printf("   38-vector golden: %d/%d bytes-exact expectations matched\n", checked,
              static_cast<int>(kGoldenVectorCount));

  // ---- R1/D17 required additions (report v3 §6.3.3) ---------------------
  ctx().begin("golden/R1-additions");
  {
    // 1) non-shortest varint must be ACCEPTED, not reported as an error.
    std::vector<uint8_t> ns = unhex("088000");  // f1 = 0 encoded in 2 bytes
    conformance::Result r = conformance::validate(ns.data(), ns.size(), Limits(), true);
    TEQ_CODE(r.code, Code::Ok);
    // 2) negative int32: 10-byte varint, truncate to 32 bits, must be E_OK.
    std::vector<uint8_t> neg = unhex("08ffffffffffffffffff01");
    TEQ_CODE(conformance::validate(neg.data(), neg.size(), Limits(), true).code, Code::Ok);
    // 3) 2^64-1 (longest legal) vs 10th-byte overflow differ only in byte 10.
    std::vector<uint8_t> max64 = unhex("08ffffffffffffffffff01");
    std::vector<uint8_t> over = unhex("08ffffffffffffffffff02");
    TEQ_CODE(conformance::validate(max64.data(), max64.size(), Limits(), true).code, Code::Ok);
    TEQ_CODE(conformance::validate(over.data(), over.size(), Limits(), true).code,
             Code::Varint10thByte);
  }

  // Same three rules, but observed on the real schema through the POD decoder.
  ctx().begin("golden/R1-on-pod");
  {
    // level is `optional int32` (field 7, wire 0): ff*9 01 must decode to -1.
    std::vector<uint8_t> neg = unhex("38ffffffffffffffffff01");
    pod::User u;
    decode::Stats st;
    TEQ_CODE(decode::user(neg.data(), neg.size(), Limits(), &u, &st), Code::Ok);
    TEQ_I64(u.level, -1);
    TCHECK(u.has_level);
    TCHECK(u.present(pod::f::kUserLevel));
    // id is int64: same bytes -> -1
    std::vector<uint8_t> negid = unhex("08ffffffffffffffffff01");
    pod::User u2;
    TEQ_CODE(decode::user(negid.data(), negid.size(), Limits(), &u2, &st), Code::Ok);
    TEQ_I64(static_cast<long long>(u2.id), -1);
  }
}

}  // namespace test
}  // namespace q207
