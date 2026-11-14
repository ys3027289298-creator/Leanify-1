#include "bmp.h"

#include <cstdint>
#include <cstring>
#include <iostream>

const uint8_t Bmp::header_magic[] = { 0x42, 0x4D };

size_t Bmp::Leanify(size_t size_leanified /*= 0*/) {
  // Need at least the 14-byte BITMAPFILEHEADER.
  if (size_ < 14) {
    std::cerr << "BMP corrupted." << std::endl;
    return Format::Leanify(size_leanified);
  }

  uint32_t bmp_size;
  uint32_t data_offset;
  memcpy(&bmp_size, fp_ + 2, sizeof(bmp_size));        // bfSize
  memcpy(&data_offset, fp_ + 10, sizeof(data_offset));  // bfOffBits

  // bfSize == 0 is written by some encoders, then there is nothing to trim.
  // Reject implausible headers instead of trusting them and cutting pixels.
  if (bmp_size > size_ || (bmp_size != 0 && (bmp_size < 14 || data_offset > bmp_size))) {
    std::cerr << "BMP corrupted." << std::endl;
    return Format::Leanify(size_leanified);
  }

  uint32_t new_size = bmp_size == 0 ? size_ : bmp_size;

  // For uncompressed BMPs with at least a BITMAPINFOHEADER, verify that the
  // declared file size covers the whole pixel array. If it does not, the
  // file is corrupt and must be left untouched.
  if (size_ >= 14 + sizeof(BITMAPINFOHEADER)) {
    const auto* dib = reinterpret_cast<const BITMAPINFOHEADER*>(fp_ + 14);
    if (dib->biSize >= sizeof(BITMAPINFOHEADER) &&
        dib->biCompression == 0 &&  // BI_RGB aka no compression
        dib->biWidth > 0 && dib->biHeight != 0 && dib->biBitCount != 0 && dib->biBitCount <= 32) {
      const uint64_t height = dib->biHeight < 0 ? -static_cast<int64_t>(dib->biHeight) : dib->biHeight;
      const uint64_t row_size = (static_cast<uint64_t>(dib->biWidth) * dib->biBitCount + 31) / 32 * 4;
      if (static_cast<uint64_t>(data_offset) + row_size * height > new_size) {
        std::cerr << "BMP corrupted." << std::endl;
        return Format::Leanify(size_leanified);
      }
    }
  }

  size_ = new_size;

  if (size_leanified) {
    memmove(fp_ - size_leanified, fp_, size_);
    fp_ -= size_leanified;
  }

  return size_;
}
