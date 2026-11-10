#pragma once
// Minimal MD5 (RFC 1321). Used only for the Dark Souls Remastered save checksum,
// which is not a security feature.
#include <cstddef>
#include <cstdint>

namespace dsr {

void md5(const uint8_t* data, size_t len, uint8_t out[16]);

} // namespace dsr
