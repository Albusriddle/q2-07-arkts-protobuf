// GENERATED -- do not edit by hand.
// Source: out/w0/security/test_vectors/wire_vectors.json (38 vectors, W0 golden).
// Copied into the repo so the host build does not depend on out/** at build time.
// Regenerate: python - << see repo/src/host/tests/README.md
#pragma once

namespace q207 { namespace test {

struct GoldenVector {
  const char* name;
  const char* hex;
  const char* expected;  // E_* code from the reference model
  int strict;            // -1 = default, 0 = compat arm, 1 = strict arm
  unsigned long long max_string_bytes;  // 0 = default
  unsigned long long max_objects;       // 0 = default
  unsigned long long max_fields;        // 0 = default
};

static const GoldenVector kGoldenVectors[] = {
    {"valid_empty", "", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_varint_150", "089601", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_string_abc", "1203616263", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_nested", "22020801", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_packed_fixed32", "1a080000000000000000", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_unpacked_fixed32", "1d00000000", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_map", "2a050a01611001", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_utf8_4byte", "1204f09f9880", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_varint_10th_byte_1", "08ffffffffffffffffff01", "E_OK", -1, 0ull, 0ull, 0ull},
    {"valid_field_19000_unknown", "c0a30901", "E_OK", -1, 0ull, 0ull, 0ull},
    {"truncated_varint", "0880", "E_TRUNCATED", -1, 0ull, 0ull, 0ull},
    {"truncated_length", "12", "E_TRUNCATED", -1, 0ull, 0ull, 0ull},
    {"truncated_fixed32", "1d0000", "E_TRUNCATED", -1, 0ull, 0ull, 0ull},
    {"truncated_group", "4b0801", "E_TRUNCATED", -1, 0ull, 0ull, 0ull},
    {"varint_10th_byte_2", "08ffffffffffffffffff02", "E_VARINT_10TH_BYTE", -1, 0ull, 0ull, 0ull},
    {"tag_too_long", "808080808001", "E_TAG_TOO_LONG", -1, 0ull, 0ull, 0ull},
    {"field_zero", "00", "E_FIELD_ZERO", -1, 0ull, 0ull, 0ull},
    {"field_range", "8080808010", "E_FIELD_RANGE", -1, 0ull, 0ull, 0ull},
    {"wire_type_6", "0e", "E_WIRE_TYPE_INVALID", -1, 0ull, 0ull, 0ull},
    {"wire_type_7", "0f", "E_WIRE_TYPE_INVALID", -1, 0ull, 0ull, 0ull},
    {"known_mismatch_strict", "0a0100", "E_WIRE_TYPE_MISMATCH", 1, 0ull, 0ull, 0ull},
    {"known_mismatch_compat", "0a0100", "E_OK", 0, 0ull, 0ull, 0ull},
    {"group_unexpected", "4c", "E_GROUP_UNEXPECTED", -1, 0ull, 0ull, 0ull},
    {"group_mismatch", "4b14", "E_GROUP_MISMATCH", -1, 0ull, 0ull, 0ull},
    {"group_ok", "4b08014c", "E_OK", -1, 0ull, 0ull, 0ull},
    {"packed_fixed32_len3", "1a03000000", "E_PACKED_LENGTH", -1, 0ull, 0ull, 0ull},
    {"packed_fixed32_len4", "1a0400000000", "E_OK", -1, 0ull, 0ull, 0ull},
    {"length_exceeds_remaining", "12ffffffff0f00", "E_LENGTH_EXCEEDS_REMAINING", -1, 0ull, 0ull, 0ull},
    {"length_huge", "12ffffffffffffffffff01", "E_LENGTH_EXCEEDS_REMAINING", -1, 0ull, 0ull, 0ull},
    {"length_exceeds_cap", "1209313233343536373839", "E_LENGTH_EXCEEDS_CAP", -1, 8ull, 0ull, 0ull},
    {"map_proto_key", "2a0d0a095f5f70726f746f5f5f1001", "E_MAP_KEY_DANGEROUS", -1, 0ull, 0ull, 0ull},
    {"map_constructor_key", "2a0f0a0b636f6e7374727563746f721001", "E_MAP_KEY_DANGEROUS", -1, 0ull, 0ull, 0ull},
    {"utf8_overlong", "1202c080", "E_UTF8_INVALID", -1, 0ull, 0ull, 0ull},
    {"utf8_surrogate", "1203eda080", "E_UTF8_INVALID", -1, 0ull, 0ull, 0ull},
    {"utf8_truncated", "1202e282", "E_UTF8_INVALID", -1, 0ull, 0ull, 0ull},
    {"depth_exceeded_101", "22ec0122e90122e60122e30122e00122dd0122da0122d70122d40122d10122ce0122cb0122c80122c50122c20122bf0122bc0122b90122b60122b30122b00122ad0122aa0122a70122a40122a101229e01229b01229801229501229201228f01228c01228901228601228301228001227e227c227a22782276227422722270226e226c226a22682266226422622260225e225c225a22582256225422522250224e224c224a22482246224422422240223e223c223a22382236223422322230222e222c222a22282226222422222220221e221c221a22182216221422122210220e220c220a22082206220422022200", "E_DEPTH", -1, 0ull, 0ull, 0ull},
    {"object_budget", "22002200", "E_OBJECT_BUDGET", -1, 0ull, 2ull, 0ull},
    {"field_budget", "08010802", "E_FIELD_BUDGET", -1, 0ull, 0ull, 1ull},
};

static const unsigned kGoldenVectorCount = sizeof(kGoldenVectors)/sizeof(kGoldenVectors[0]);

}}  // namespace q207::test
