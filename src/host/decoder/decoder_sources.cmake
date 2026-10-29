# Minimal decoder source set shared by the host NAPI .so and the HAP native .so.
# The parent CMakeLists must define Q207_REPO_ROOT before include().
#
# Deliberately excludes tape/conformance/sha256: they are host-only and would
# enlarge the shipped .so and its attack surface without being used by decode.
set(Q207_DECODER_SOURCES
  "${Q207_REPO_ROOT}/src/host/common/status.cpp"
  "${Q207_REPO_ROOT}/src/host/decoder/wire_reader.cpp"
  "${Q207_REPO_ROOT}/src/host/decoder/decode_user.cpp"
  "${Q207_REPO_ROOT}/src/host/decoder/map_order.cpp"
  "${Q207_REPO_ROOT}/src/host/decoder/object_model.cpp"
  "${Q207_REPO_ROOT}/src/host/decoder/norm.cpp"
)
set(Q207_DECODER_INCLUDE_DIRS "${Q207_REPO_ROOT}/src")
