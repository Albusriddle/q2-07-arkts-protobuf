// HOST-ONLY: conformance validator for the W0 golden wire vectors.
//
// The 38 vectors in out/w0/security/test_vectors/wire_vectors.json were written
// against the tiny reference schema of out/w0/security/reference/wire_validator.py
// (f1 int32, f2 string, f3 repeated fixed32, f4 recursive message, f5 map<string,int32>).
// That schema is deliberately different from proto/example.proto, so the golden
// file can only judge the *reader* if the same schema is instantiated here.
// The reader itself (decoder/wire_reader) is shared by both schemas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "host/common/status.h"
#include "host/decoder/wire_reader.h"

namespace q207 {
namespace conformance {

// Result of a conformance validation.
struct Result {
  Code code = Code::Ok;
  uint32_t objects = 0;
  uint32_t fields = 0;
  size_t error_offset = 0;
  // Allocation accounting of the reader: stays 0 when a pathological length
  // prefix is rejected before the allocation step.
  uint64_t reader_alloc_bytes = 0;
  uint64_t reader_alloc_calls = 0;
};

// Validate one golden vector payload against the reference schema.
// `strict` == false models the compat arm (known-field wire mismatch is skipped
// as an unknown field instead of failing).
Result validate(const uint8_t* data, size_t size, const Limits& limits, bool strict);

// The reference schema, exposed for tests that want to see the field table.
struct FieldSpec {
  uint32_t number;
  const char* name;
  uint32_t wire;      // expected wire type
  bool repeated;
  const char* kind;   // "int32" | "string" | "fixed32" | "message" | "map"
};
const std::vector<FieldSpec>& reference_schema();

// Prototype-pollution key blacklist, identical to the reference model.
bool is_dangerous_map_key(const std::string& k);

}  // namespace conformance
}  // namespace q207
