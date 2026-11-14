#ifndef FORMATS_AVIF_H_
#define FORMATS_AVIF_H_

#include <cstdint>

#include "format.h"

extern bool is_fast;
extern bool is_verbose;

class Avif : public Format {
 public:
  using Format::Format;

  size_t Leanify(size_t size_leanified = 0) override;

  // "ftyp" box type, every ISOBMFF/AVIF file starts with it.
  static const uint8_t header_magic[4];
};

#endif  // FORMATS_AVIF_H_
