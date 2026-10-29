// HOST-ONLY: minimal SHA-256 (for artifact/data-card fingerprinting in evidence).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace q207 {

class Sha256 {
 public:
  Sha256();
  void update(const void* data, size_t len);
  // 32 bytes, lowercase hex
  std::string hexdigest();
  void reset();

 private:
  void compress(const uint8_t* block);
  uint32_t h_[8];
  uint64_t bits_ = 0;
  uint8_t buf_[64];
  size_t buf_len_ = 0;
};

std::string sha256_hex(const void* data, size_t len);
std::string sha256_file(const std::string& path);

}  // namespace q207
