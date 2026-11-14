#ifndef FORMATS_WEBP_H_
#define FORMATS_WEBP_H_

#include <cstdint>

#include "format.h"

extern bool is_fast;
extern bool is_verbose;

class Webp : public Format {
 public:
  using Format::Format;

  size_t Leanify(size_t size_leanified = 0) override;

  // "RIFF"...."WEBP", every WebP file starts with it.
  static const uint8_t header_magic[12];
};

#endif  // FORMATS_WEBP_H_
