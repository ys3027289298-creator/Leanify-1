#include "avif.h"

#ifdef ENABLE_AVIF

#include <cstring>
#include <iostream>

#include <avif/avif.h>

#include "../utils.h"

using std::cerr;
using std::endl;

const uint8_t Avif::header_magic[] = { 'f', 't', 'y', 'p' };

namespace {

// Compare every decoded plane (YUV and alpha) byte by byte.
bool PixelsIdentical(const avifImage& a, const avifImage& b) {
  if (a.width != b.width || a.height != b.height || a.depth != b.depth ||
      a.yuvFormat != b.yuvFormat || a.yuvRange != b.yuvRange ||
      a.alphaPremultiplied != b.alphaPremultiplied) {
    return false;
  }

  const uint32_t bytes_per_sample = avifImageUsesU16(&a) ? 2 : 1;

  for (int plane = 0; plane < AVIF_PLANE_COUNT_YUV; ++plane) {
    const uint32_t plane_width = avifImagePlaneWidth(&a, plane);
    const uint32_t plane_height = avifImagePlaneHeight(&a, plane);
    const uint8_t* src = a.yuvPlanes[plane];
    const uint8_t* dst = b.yuvPlanes[plane];
    if (src == nullptr || dst == nullptr)
      return false;

    for (uint32_t y = 0; y < plane_height; ++y) {
      if (memcmp(src + y * a.yuvRowBytes[plane],
                 dst + y * b.yuvRowBytes[plane],
                 plane_width * bytes_per_sample) != 0)
        return false;
    }
  }

  if (a.alphaPlane != nullptr) {
    if (b.alphaPlane == nullptr)
      return false;
    for (uint32_t y = 0; y < a.height; ++y) {
      if (memcmp(a.alphaPlane + y * a.alphaRowBytes,
                 b.alphaPlane + y * b.alphaRowBytes,
                 a.width * bytes_per_sample) != 0)
        return false;
    }
  } else if (b.alphaPlane != nullptr) {
    return false;
  }

  return true;
}

}  // namespace

size_t Avif::Leanify(size_t size_leanified /*= 0*/) {
  // "ftyp" sits at offset 4, so the box size (4 bytes) has to fit as well.
  if (size_ < 8) {
    cerr << "AVIF file too small, skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  if (is_fast) {
    VerbosePrint("Fast mode, AVIF recompression skipped.");
    return Format::Leanify(size_leanified);
  }

  avifROData input = { fp_, size_ };
  if (!avifPeekCompatibleFileType(&input)) {
    cerr << "Unsupported AVIF subformat: ftyp brand is neither 'avif' nor 'avis', skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  avifDecoder* decoder = avifDecoderCreate();
  if (decoder == nullptr) {
    cerr << "AVIF decoder init failed, skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  avifResult result = avifDecoderSetIOMemory(decoder, fp_, size_);
  if (result == AVIF_RESULT_OK)
    result = avifDecoderParse(decoder);

  if (result != AVIF_RESULT_OK) {
    cerr << "AVIF parse failed (" << avifResultToString(result) << "): " << decoder->diag.error
         << ", skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  if (decoder->progressiveState == AVIF_PROGRESSIVE_STATE_AVAILABLE ||
      decoder->progressiveState == AVIF_PROGRESSIVE_STATE_ACTIVE) {
    cerr << "Unsupported AVIF subformat: progressive image, skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  if (decoder->imageCount != 1) {
    cerr << "Unsupported AVIF subformat: AVIF image sequence with " << decoder->imageCount
         << " frames (only single still images are supported), skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  result = avifDecoderNextImage(decoder);
  if (result != AVIF_RESULT_OK) {
    cerr << "AVIF decode failed (" << avifResultToString(result) << "): " << decoder->diag.error
         << ", skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  // Re-encode the decoded image with lossless AV1, keeping the original YUV format,
  // bit depth, range and alpha so no color conversion happens.
  avifEncoder* encoder = avifEncoderCreate();
  if (encoder == nullptr) {
    cerr << "AVIF encoder init failed, skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
  // Lower speed gives better lossless compression; 2 is libaom's high-efficiency
  // preset while keeping runtime reasonable.
  encoder->speed = 2;
  encoder->autoTiling = AVIF_TRUE;

  avifRWData output = AVIF_DATA_EMPTY;
  result = avifEncoderWrite(encoder, decoder->image, &output);
  avifEncoderDestroy(encoder);

  if (result != AVIF_RESULT_OK) {
    cerr << "AVIF lossless encode failed (" << avifResultToString(result) << "), skipped." << endl;
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  // Verify the re-encoded file decodes to exactly the same pixels before accepting it.
  avifDecoder* verify = avifDecoderCreate();
  bool identical = verify != nullptr;
  if (identical) {
    result = avifDecoderSetIOMemory(verify, output.data, output.size);
    if (result == AVIF_RESULT_OK)
      result = avifDecoderParse(verify);
    if (result == AVIF_RESULT_OK)
      result = avifDecoderNextImage(verify);
    if (result != AVIF_RESULT_OK) {
      cerr << "AVIF verification decode failed (" << avifResultToString(result) << "), skipped."
           << endl;
      identical = false;
    } else {
      identical = PixelsIdentical(*decoder->image, *verify->image);
    }
  }
  if (verify != nullptr)
    avifDecoderDestroy(verify);

  if (!identical) {
    cerr << "AVIF recompressed pixels differ from the original, skipped to keep lossless guarantee."
         << endl;
    avifRWDataFree(&output);
    avifDecoderDestroy(decoder);
    return Format::Leanify(size_leanified);
  }

  avifDecoderDestroy(decoder);

  // Never write a larger file: keep the original bytes unchanged otherwise.
  if (output.size >= size_) {
    VerbosePrint("AVIF recompressed size is not smaller, keeping original.");
    avifRWDataFree(&output);
    return Format::Leanify(size_leanified);
  }

  VerbosePrint("AVIF losslessly recompressed, ", size_ - output.size, " bytes removed.");
  // The output is fully built before touching the mapped buffer, and a smaller payload is
  // always written in one contiguous copy, so a failure can never leave a torn file.
  memmove(fp_ - size_leanified, output.data, output.size);
  size_t new_size = output.size;
  avifRWDataFree(&output);

  fp_ -= size_leanified;
  size_ = new_size;
  return size_;
}

#else

#include <iostream>

const uint8_t Avif::header_magic[] = { 'f', 't', 'y', 'p' };

size_t Avif::Leanify(size_t size_leanified /*= 0*/) {
  std::cerr << "AVIF support is not compiled in (libavif unavailable at build time), skipped."
            << std::endl;
  return Format::Leanify(size_leanified);
}

#endif  // ENABLE_AVIF
