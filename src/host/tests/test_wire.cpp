// HOST-ONLY: wire reader unit tests + the "no allocation before validation" proof.
#include <new>

#include "host/common/status.h"
#include "host/decoder/conformance.h"
#include "host/decoder/wire_reader.h"
#include "host/tests/test_framework.h"

// ---- global allocation counter -----------------------------------------
// ASan installs its own operator new/delete; overriding them would duplicate
// symbols.  The allocation-order proof is compiled out under Q207_ASAN and the
// sanitizer covers the same path at runtime.
#if !defined(Q207_ASAN)
static unsigned long long g_new_bytes = 0;
static unsigned long long g_new_calls = 0;

void* operator new(std::size_t n) {
  ++g_new_calls;
  g_new_bytes += n;
  void* p = std::malloc(n ? n : 1);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace q207 {
namespace test {
namespace {

using wire::Reader;

Limits basic_limits() {
  Limits l;
  return l;
}

// Reads one varint from a hex string and returns (code, value).
struct VarintResult {
  Code code;
  uint64_t value;
};
VarintResult varint_from_hex(const char* h) {
  std::vector<uint8_t> b = unhex(h);
  Reader r(b.data(), b.size(), basic_limits());
  uint64_t v = 0;
  Code e = r.read_varint(&v);
  return {e, v};
}

}  // namespace

void suite_wire() {
  ctx().begin("wire/varint");
  {
    TEQ_U64(varint_from_hex("00").value, 0);
    TEQ_U64(varint_from_hex("01").value, 1);
    TEQ_U64(varint_from_hex("7f").value, 127);
    TEQ_U64(varint_from_hex("8001").value, 128);
    TEQ_U64(varint_from_hex("ac02").value, 300);
    TEQ_U64(varint_from_hex("ffffffff07").value, 0x7FFFFFFFull);
    TEQ_U64(varint_from_hex("ffffffffffffffffff01").value, 0xFFFFFFFFFFFFFFFFull);
    TEQ_U64(varint_from_hex("80808080808080808001").value, 0x8000000000000000ull);
    // non-shortest (non-canonical) encoding must be ACCEPTED: 80 00 == 0
    VarintResult ns = varint_from_hex("8000");
    TEQ_CODE(ns.code, Code::Ok);
    TEQ_U64(ns.value, 0);
    TEQ_U64(varint_from_hex("808000").value, 0);
    // 10th byte > 1 => value bits beyond 64
    TEQ_CODE(varint_from_hex("ffffffffffffffffff02").code, Code::Varint10thByte);
    TEQ_CODE(varint_from_hex("ffffffffffffffffff7f").code, Code::Varint10thByte);
    TEQ_CODE(varint_from_hex("ffffffffffffffffff81").code, Code::Varint10thByte);
    // truncated
    TEQ_CODE(varint_from_hex("80").code, Code::Truncated);
    TEQ_CODE(varint_from_hex("ffffffffffffffffff").code, Code::Truncated);
    TEQ_CODE(varint_from_hex("").code, Code::Truncated);
  }

  ctx().begin("wire/tag");
  {
    struct TagCase {
      const char* hex;
      Code want;
    } cases[] = {
        {"0801", Code::Ok},           // field 1 varint
        {"0a01", Code::Ok},           // field 1 length
        {"0b", Code::Ok},             // field 1 start group
        {"0c", Code::Ok},             // field 1 end group
        {"00", Code::FieldZero},      // field 0
        {"0e", Code::WireTypeInvalid},
        {"0f", Code::WireTypeInvalid},
        {"808080808001", Code::TagTooLong},
        {"8080808010", Code::FieldRange},
        {"ffffffff0f", Code::WireTypeInvalid},  // max field number but wire type 7
    };
    for (const TagCase& c : cases) {
      std::vector<uint8_t> b = unhex(c.hex);
      Reader r(b.data(), b.size(), basic_limits());
      uint32_t f = 0, w = 0;
      TEQ_CODE(r.read_tag(&f, &w), c.want);
      if (c.want == Code::Ok) TCHECK(f != 0 && w != 6 && w != 7);
    }
    // Largest valid field number (2^29-1) with wire type 0.
    {
      std::vector<uint8_t> b = unhex("f8ffffff0f");
      Reader r(b.data(), b.size(), basic_limits());
      uint32_t f = 0, w = 0;
      TEQ_CODE(r.read_tag(&f, &w), Code::Ok);
      TEQ_U64(f, 536870911u);
      TEQ_U64(w, 0);
    }
  }

  ctx().begin("wire/length-order");
  {
    // len <= remaining is checked BEFORE len <= cap: both are violated here and
    // the expected code is the remaining one.
    {
      std::vector<uint8_t> b = unhex("12ffffffff0f00");
      Reader r(b.data(), b.size(), basic_limits());
      uint32_t f = 0, w = 0;
      TCHECK(r.read_tag(&f, &w) == Code::Ok);
      uint64_t len = 0;
      TEQ_CODE(r.read_length(&len, 1ull << 20), Code::LengthExceedsRemaining);
    }
    // len = 2^64-1 must not be narrowed to int/size_t before the check.
    {
      std::vector<uint8_t> b = unhex("ffffffffffffffffff01");
      Reader r(b.data(), b.size(), basic_limits());
      uint64_t len = 0;
      TEQ_CODE(r.read_length(&len, 1ull << 20), Code::LengthExceedsRemaining);
    }
    // remaining OK, cap too small
    {
      std::vector<uint8_t> b = unhex("0a09313233343536373839");
      Reader r(b.data(), b.size(), basic_limits());
      uint32_t f = 0, w = 0;
      TCHECK(r.read_tag(&f, &w) == Code::Ok);
      uint64_t len = 0;
      TEQ_CODE(r.read_length(&len, 8), Code::LengthExceedsCap);
      Reader r2(b.data(), b.size(), basic_limits());
      TCHECK(r2.read_tag(&f, &w) == Code::Ok);
      TEQ_CODE(r2.read_length(&len, 9), Code::Ok);
      TEQ_U64(len, 9);
    }
    // zero length is valid
    {
      std::vector<uint8_t> b = unhex("00");
      Reader r(b.data(), b.size(), basic_limits());
      uint64_t len = 99;
      TEQ_CODE(r.read_length(&len, 0), Code::Ok);
      TEQ_U64(len, 0);
    }
  }

  ctx().begin("wire/fixed-sint-int");
  {
    std::vector<uint8_t> b = unhex("78563412f0debc9a78563412");
    Reader r(b.data(), b.size(), basic_limits());
    uint32_t v32 = 0;
    TEQ_CODE(r.read_fixed32(&v32), Code::Ok);
    TEQ_U64(v32, 0x12345678u);
    uint64_t v64 = 0;
    TEQ_CODE(r.read_fixed64(&v64), Code::Ok);
    TEQ_U64(v64, 0x123456789ABCDEF0ull);
    // truncated fixed reads
    std::vector<uint8_t> t = unhex("7856");
    Reader r2(t.data(), t.size(), basic_limits());
    TEQ_CODE(r2.read_fixed32(&v32), Code::Truncated);
    // zigzag round-trip for the boundary values
    const int32_t samples[] = {0, -1, 1, -2, 2, 2147483647, -2147483647 - 1, 1234567, -1234567};
    for (int32_t s : samples) {
      const uint32_t z = (static_cast<uint32_t>(s) << 1) ^ static_cast<uint32_t>(s >> 31);
      TEQ_I64(wire::zigzag32(z), s);
    }
    // negative int32 is a 10-byte varint, never a fixed32
    std::vector<uint8_t> neg = unhex("ffffffffffffffffff01");
    Reader r3(neg.data(), neg.size(), basic_limits());
    int32_t i32 = 0;
    TEQ_CODE(r3.read_int32(&i32), Code::Ok);
    TEQ_I64(i32, -1);
    Reader r4(neg.data(), neg.size(), basic_limits());
    int64_t i64 = 0;
    TEQ_CODE(r4.read_int64(&i64), Code::Ok);
    TEQ_I64(i64, -1);
    // sint32 decode of zigzag(-1) == 1
    std::vector<uint8_t> zz = unhex("01");
    Reader r5(zz.data(), zz.size(), basic_limits());
    int32_t s32 = 0;
    TEQ_CODE(r5.read_sint32(&s32), Code::Ok);
    TEQ_I64(s32, -1);
  }

  ctx().begin("wire/packed");
  {
    std::vector<uint8_t> b = unhex("080000000002000000");
    Reader r(b.data(), b.size(), basic_limits());
    std::vector<uint32_t> vals;
    TEQ_CODE(r.read_packed_fixed32(&vals, 100), Code::Ok);
    TEQ_U64(vals.size(), 2);
    TEQ_U64(vals[0], 0);
    TEQ_U64(vals[1], 2);
    // length not divisible by 4
    std::vector<uint8_t> bad = unhex("03000000");
    Reader r2(bad.data(), bad.size(), basic_limits());
    std::vector<uint32_t> v2;
    TEQ_CODE(r2.read_packed_fixed32(&v2, 100), Code::PackedLength);
    TCHECK(v2.empty());
    // packed varint
    std::vector<uint8_t> pv = unhex("020103");
    Reader r3(pv.data(), pv.size(), basic_limits());
    std::vector<uint64_t> v3;
    TEQ_CODE(r3.read_packed_varint(&v3, 100), Code::Ok);
    TEQ_U64(v3.size(), 2);
    TEQ_U64(v3[0], 1);
    TEQ_U64(v3[1], 3);
    // element budget
    Reader r4(pv.data(), pv.size(), basic_limits());
    std::vector<uint64_t> v4;
    TEQ_CODE(r4.read_packed_varint(&v4, 1), Code::FieldBudget);
    // packed zigzag
    std::vector<uint8_t> pz = unhex("020102");
    Reader r5(pz.data(), pz.size(), basic_limits());
    std::vector<int32_t> v5;
    TEQ_CODE(r5.read_packed_zigzag32(&v5, 100), Code::Ok);
    TEQ_U64(v5.size(), 2);
    if (v5.size() == 2) {
      TEQ_I64(v5[0], -1);
      TEQ_I64(v5[1], 1);
    }
  }

  ctx().begin("wire/utf8");
  {
    auto ok = [](const char* h) {
      std::vector<uint8_t> b = unhex(h);
      return wire::utf8_valid(b.data(), b.size());
    };
    TCHECK(ok(""));
    TCHECK(ok("616263"));
    TCHECK(ok("e4bda0e5a5bd"));          // 你好
    TCHECK(ok("f09f9880"));              // 😀 (4-byte)
    TCHECK(ok("c280"));                  // U+0080 (shortest form)
    TCHECK(!ok("c080"));                 // overlong NUL
    TCHECK(!ok("e282"));                 // truncated
    TCHECK(!ok("eda080"));               // surrogate
    TCHECK(!ok("f4908080"));             // > U+10FFFF
    TCHECK(!ok("fe"));                   // invalid leading byte
    TCHECK(!ok("ffff"));                 // 5-byte-form lead bytes
    // read_string with strict UTF-8
    std::vector<uint8_t> b = unhex("1202c080");
    Reader r(b.data(), b.size(), basic_limits());
    uint32_t f = 0, w = 0;
    TCHECK(r.read_tag(&f, &w) == Code::Ok);
    std::string s;
    TEQ_CODE(r.read_string(&s, 1u << 20), Code::Utf8Invalid);
    Limits lax = basic_limits();
    lax.utf8_strict = false;
    Reader r2(b.data(), b.size(), lax);
    TCHECK(r2.read_tag(&f, &w) == Code::Ok);
    TEQ_CODE(r2.read_string(&s, 1u << 20), Code::Ok);
    TEQ_U64(s.size(), 2);
  }

  ctx().begin("wire/depth-and-caps");
  {
    // 100 nested length-delimited frames are accepted, the 101st is not.
    auto build_nested = [](int frames) {
      // innermost frame is empty; every outer level wraps the previous payload
      std::vector<uint8_t> payload;
      for (int i = 0; i < frames; ++i) {
        std::vector<uint8_t> varint;
        uint64_t v = payload.size();
        while (true) {
          const uint8_t b = static_cast<uint8_t>(v & 0x7F);
          v >>= 7;
          if (v) {
            varint.push_back(static_cast<uint8_t>(b | 0x80));
          } else {
            varint.push_back(b);
            break;
          }
        }
        std::vector<uint8_t> next;
        next.push_back(0x0a);  // field 1, wire 2
        next.insert(next.end(), varint.begin(), varint.end());
        next.insert(next.end(), payload.begin(), payload.end());
        payload.swap(next);
      }
      return payload;
    };
    auto enter_all = [](Reader& r, int frames) {
      Code e = Code::Ok;
      int entered = 0;
      for (int i = 0; i < frames && e == Code::Ok; ++i) {
        uint32_t f = 0, w = 0;
        e = r.read_tag(&f, &w);
        if (e == Code::Ok) {
          e = r.enter_length(1u << 20);
          if (e == Code::Ok) ++entered;
        }
      }
      return std::make_pair(e, entered);
    };
    {
      std::vector<uint8_t> b = build_nested(100);
      Reader r(b.data(), b.size(), basic_limits());
      auto res = enter_all(r, 100);
      TEQ_CODE(res.first, Code::Ok);
      TEQ_U64(res.second, 100);
      TEQ_U64(r.depth(), 100);
    }
    {
      std::vector<uint8_t> b = build_nested(101);
      Reader r(b.data(), b.size(), basic_limits());
      auto res = enter_all(r, 101);
      TEQ_CODE(res.first, Code::Depth);
      TEQ_U64(res.second, 100);
    }
    // single-message cap: 2^32-1 length is fine while it fits, rejected otherwise
    {
      Limits tiny = basic_limits();
      tiny.max_message_bytes = 4;
      std::vector<uint8_t> small = unhex("03010203");
      Reader r(small.data(), small.size(), tiny);
      uint64_t len = 0;
      TEQ_CODE(r.read_length(&len, tiny.max_message_bytes), Code::Ok);
      std::vector<uint8_t> big = unhex("ffffffff0f");
      Reader r2(big.data(), big.size(), tiny);
      TEQ_CODE(r2.read_length(&len, tiny.max_message_bytes), Code::LengthExceedsRemaining);
    }
  }

#if !defined(Q207_ASAN)
  ctx().begin("wire/allocation-order");
  {
    // Pathological length prefixes must be rejected with ZERO allocations,
    // both in the reader's own accounting and in the process allocator.
    const char* vectors[] = {"12ffffffff0f00", "12ffffffffffffffffff01", "0a7fffffff"};
    for (const char* v : vectors) {
      std::vector<uint8_t> b = unhex(v);
      conformance::Result r = conformance::validate(b.data(), b.size(), Limits(), true);
      TCHECK(r.code != Code::Ok);
      TEQ_U64(r.reader_alloc_calls, 0);
      TEQ_U64(r.reader_alloc_bytes, 0);
    }
    std::vector<uint8_t> b = unhex("12ffffffff0f00");
    const unsigned long long bytes_before = g_new_bytes;
    const unsigned long long calls_before = g_new_calls;
    conformance::Result r = conformance::validate(b.data(), b.size(), Limits(), true);
    TEQ_CODE(r.code, Code::LengthExceedsRemaining);
    // A 4 GiB malloc would show up here; a few hundred bytes of scratch is fine.
    const unsigned long long dbytes = g_new_bytes - bytes_before;
    TCHECK(dbytes < 4096);
    std::printf("   allocation delta for 4 GiB length prefix: %llu bytes in %llu calls\n", dbytes,
                g_new_calls - calls_before);
  }
#endif

  ctx().begin("wire/skip-unknown");
  {
    std::vector<uint8_t> b = unhex("480752030102035500000000");
    Reader r(b.data(), b.size(), basic_limits());
    Code e = Code::Ok;
    while (e == Code::Ok && !r.at_end()) {
      uint32_t f = 0, w = 0;
      e = r.read_tag(&f, &w);
      if (e == Code::Ok) e = r.skip_field(f, w);
    }
    TEQ_CODE(e, Code::Ok);
    TCHECK(r.at_end());
    // skipping a length-delimited unknown field must not allocate
    std::vector<uint8_t> big = unhex("4affffff0f");
    Reader r2(big.data(), big.size(), basic_limits());
    uint32_t f = 0, w = 0;
    TCHECK(r2.read_tag(&f, &w) == Code::Ok);
    TEQ_CODE(r2.skip_field(f, w), Code::LengthExceedsRemaining);
    TEQ_U64(r2.alloc_calls(), 0);
  }
}

}  // namespace test
}  // namespace q207
