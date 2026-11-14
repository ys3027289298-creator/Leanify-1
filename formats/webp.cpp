#include "webp.h"

#ifdef ENABLE_WEBP

#include <cstring>
#include <iostream>
#include <vector>

#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/mux_types.h>

#include "../utils.h"

using std::cerr;
using std::endl;
using std::vector;

const uint8_t Webp::header_magic[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                       'W', 'E', 'B', 'P' };

namespace {

constexpr uint32_t kFourCC(const char tag[4]) {
  return static_cast<uint32_t>(tag[0]) | (static_cast<uint32_t>(tag[1]) << 8) |
         (static_cast<uint32_t>(tag[2]) << 16) |
         (static_cast<uint32_t>(tag[3]) << 24);
}

const uint32_t kChunkVP8 = kFourCC("VP8 ");
const uint32_t kChunkVP8L = kFourCC("VP8L");
const uint32_t kChunkVP8X = kFourCC("VP8X");
const uint32_t kChunkALPH = kFourCC("ALPH");
const uint32_t kChunkANIM = kFourCC("ANIM");
const uint32_t kChunkANMF = kFourCC("ANMF");
const uint32_t kChunkICCP = kFourCC("ICCP");
const uint32_t kChunkEXIF = kFourCC("EXIF");
const uint32_t kChunkXMP = kFourCC("XMP ");

// A parsed RIFF chunk: fourcc plus payload bounds inside the input buffer.
struct Chunk {
  uint32_t fourcc;
  const uint8_t* data;
  uint32_t size;
};

uint32_t ReadU32LE(const uint8_t* p) {
  uint32_t value = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  return value;
}

// Append a RIFF chunk (fourcc, little-endian size, payload and even padding).
void AppendChunk(vector<uint8_t>* out, uint32_t fourcc, const uint8_t* data, uint32_t size) {
  out->insert(out->end(), reinterpret_cast<const uint8_t*>(&fourcc),
              reinterpret_cast<const uint8_t*>(&fourcc) + 4);
  out->insert(out->end(), reinterpret_cast<const uint8_t*>(&size),
              reinterpret_cast<const uint8_t*>(&size) + 4);
  out->insert(out->end(), data, data + size);
  if (size & 1)
    out->push_back(0);
}

}  // namespace

size_t Webp::Leanify(size_t size_leanified /*= 0*/) {
  if (size_ < sizeof(header_magic)) {
    cerr << "WebP file too small, skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  if (is_fast) {
    VerbosePrint("Fast mode, WebP recompression skipped.");
    return Format::Leanify(size_leanified);
  }

  // ---- Parse the RIFF container and every chunk, validating all bounds. ----
  uint32_t riff_size = 0;
  riff_size = ReadU32LE(fp_ + 4);
  if (riff_size + 8 > size_) {
    cerr << "WebP RIFF size field (" << riff_size << ") exceeds file size (" << size_
         << "), skipped." << endl;
    return Format::Leanify(size_leanified);
  }
  // Bytes past the declared RIFF payload are trailing junk; they are dropped
  // when (and only when) a smaller verified replacement is written.
  const size_t declared_size = riff_size + 8;

  vector<Chunk> chunks;
  const uint8_t* cursor = fp_ + 12;
  const uint8_t* file_end = fp_ + declared_size;

  while (cursor < file_end) {
    // fourcc (4) + size (4)
    if (file_end - cursor < 8) {
      cerr << "WebP chunk header truncated at offset " << cursor - fp_ << ", skipped." << endl;
      return Format::Leanify(size_leanified);
    }

    uint32_t fourcc = 0;
    uint32_t chunk_size = 0;
    memcpy(&fourcc, cursor, 4);
    chunk_size = ReadU32LE(cursor + 4);

    const uint8_t* payload = cursor + 8;
    // odd-sized chunks carry one padding byte
    uint64_t total = 8ull + chunk_size + (chunk_size & 1);
    if (payload + chunk_size > file_end || total > static_cast<uint64_t>(file_end - cursor)) {
      cerr << "WebP chunk '" << static_cast<char>(fourcc & 0xFF)
           << static_cast<char>((fourcc >> 8) & 0xFF) << static_cast<char>((fourcc >> 16) & 0xFF)
           << static_cast<char>((fourcc >> 24) & 0xFF) << "' payload exceeds file end, skipped."
           << endl;
      return Format::Leanify(size_leanified);
    }

    chunks.push_back({ fourcc, payload, chunk_size });
    cursor += static_cast<size_t>(total);
  }

  bool has_vp8x = false;
  bool has_bitstream = false;
  bool has_lossy_vp8 = false;
  bool has_alpha_flag = false;
  const Chunk* iccp = nullptr;
  const Chunk* exif = nullptr;
  const Chunk* xmp = nullptr;

  for (const Chunk& chunk : chunks) {
    if (chunk.fourcc == kChunkANIM || chunk.fourcc == kChunkANMF) {
      cerr << "Unsupported WebP subformat: animated image, skipped." << endl;
      return Format::Leanify(size_leanified);
    }
    if (chunk.fourcc == kChunkVP8X) {
      if (has_vp8x || chunk.size < 10) {
        cerr << "WebP VP8X chunk is missing or duplicated/malformed, skipped." << endl;
        return Format::Leanify(size_leanified);
      }
      has_vp8x = true;
      // flags byte: bit 4 is the alpha flag.
      has_alpha_flag = (chunk.data[0] & 0x10) != 0;
    } else if (chunk.fourcc == kChunkVP8 || chunk.fourcc == kChunkALPH) {
      has_bitstream = true;
      if (chunk.fourcc == kChunkVP8)
        has_lossy_vp8 = true;
    } else if (chunk.fourcc == kChunkVP8L) {
      has_bitstream = true;
    } else if (chunk.fourcc == kChunkICCP) {
      iccp = &chunk;
    } else if (chunk.fourcc == kChunkEXIF) {
      exif = &chunk;
    } else if (chunk.fourcc == kChunkXMP) {
      xmp = &chunk;
    }
  }

  if (!has_bitstream) {
    cerr << "WebP contains no image bitstream (VP8/VP8L/ALPH), skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  if (has_lossy_vp8) {
    // A VP8 (lossy) bitstream cannot be re-encoded losslessly at equal or
    // smaller size in general; lossless WebP optimization targets VP8L only.
    VerbosePrint("Lossy (VP8) WebP, lossless recompression skipped.");
    return Format::Leanify(size_leanified);
  }

  // ---- Decode the still image to RGBA using libwebp. ----
  WebPBitstreamFeatures features;
  VP8StatusCode status = WebPGetFeatures(fp_, size_, &features);
  if (status != VP8_STATUS_OK) {
    cerr << "WebP bitstream probe failed (VP8 status " << status << "), skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  int width = features.width;
  int height = features.height;
  if (width <= 0 || height <= 0) {
    cerr << "WebP image dimensions are invalid (" << width << "x" << height << "), skipped."
         << endl;
    return Format::Leanify(size_leanified);
  }

  uint8_t* orig_rgba = WebPDecodeRGBA(fp_, size_, &width, &height);
  if (orig_rgba == nullptr) {
    cerr << "WebP decode failed, skipped." << endl;
    return Format::Leanify(size_leanified);
  }

  // ---- Re-encode losslessly. exact=1 keeps RGB under fully transparent pixels. ----
  WebPConfig config;
  WebPPicture picture;
  WebPMemoryWriter writer;

  bool ok = WebPConfigInit(&config) != 0 && WebPConfigLosslessPreset(&config, 6) != 0;
  if (ok) {
    config.exact = 1;
    config.quality = 100;
    ok = WebPValidateConfig(&config) != 0;
  }

  if (ok)
    ok = WebPPictureInit(&picture) != 0;
  if (ok) {
    picture.width = width;
    picture.height = height;
    picture.use_argb = 1;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    ok = WebPPictureImportRGBA(&picture, orig_rgba, width * 4) != 0;
  }
  if (ok)
    ok = WebPEncode(&config, &picture) != 0;

  // Keep |orig_rgba| alive for the byte-for-byte verification below.
  if (!ok) {
    cerr << "WebP lossless encode failed, skipped." << endl;
    WebPFree(orig_rgba);
    WebPPictureFree(&picture);
    WebPMemoryWriterClear(&writer);
    return Format::Leanify(size_leanified);
  }
  WebPPictureFree(&picture);

  // WebPMemoryWriter emits a complete RIFF/WEBP file; extract the VP8L payload
  // so the new container can be rebuilt with VP8X and metadata chunks.
  const uint8_t* vp8l_data = nullptr;
  uint32_t vp8l_size = 0;
  {
    const uint8_t* p = writer.mem + 12;
    const uint8_t* end = writer.mem + writer.size;
    while (p + 8 <= end) {
      uint32_t fourcc = 0;
      uint32_t chunk_size = 0;
      memcpy(&fourcc, p, 4);
      chunk_size = ReadU32LE(p + 4);
      if (p + 8 + chunk_size > end)
        break;
      if (fourcc == kChunkVP8L) {
        vp8l_data = p + 8;
        vp8l_size = chunk_size;
        break;
      }
      p += 8 + chunk_size + (chunk_size & 1);
    }
  }
  if (vp8l_data == nullptr) {
    cerr << "WebP encoder produced no VP8L bitstream, skipped." << endl;
    WebPFree(orig_rgba);
    WebPMemoryWriterClear(&writer);
    return Format::Leanify(size_leanified);
  }

  vector<uint8_t> encoded;
  encoded.reserve(12 + vp8l_size + 8);

  const bool has_alpha = features.has_alpha != 0 || has_alpha_flag;
  const bool extended = has_vp8x || has_alpha || iccp || exif || xmp;

  if (extended) {
    // VP8X payload: flags(1) + 3 reserved bytes + canvas width-1 (3) + height-1 (3)
    uint8_t vp8x[10] = { 0 };
    if (has_alpha)
      vp8x[0] |= 0x10;
    if (iccp)
      vp8x[0] |= 0x20;
    if (exif)
      vp8x[0] |= 0x08;
    if (xmp)
      vp8x[0] |= 0x04;
    uint32_t canvas_minus_one = static_cast<uint32_t>(width - 1);
    vp8x[4] = canvas_minus_one & 0xFF;
    vp8x[5] = (canvas_minus_one >> 8) & 0xFF;
    vp8x[6] = (canvas_minus_one >> 16) & 0xFF;
    uint32_t height_minus_one = static_cast<uint32_t>(height - 1);
    vp8x[7] = height_minus_one & 0xFF;
    vp8x[8] = (height_minus_one >> 8) & 0xFF;
    vp8x[9] = (height_minus_one >> 16) & 0xFF;
    AppendChunk(&encoded, kChunkVP8X, vp8x, sizeof(vp8x));
    if (iccp)
      AppendChunk(&encoded, kChunkICCP, iccp->data, iccp->size);
  }

  AppendChunk(&encoded, kChunkVP8L, vp8l_data, vp8l_size);
  WebPMemoryWriterClear(&writer);

  if (exif)
    AppendChunk(&encoded, kChunkEXIF, exif->data, exif->size);
  if (xmp)
    AppendChunk(&encoded, kChunkXMP, xmp->data, xmp->size);

  // ---- Rebuild the RIFF header around the new chunk list. ----
  vector<uint8_t> output;
  output.reserve(8 + encoded.size());
  static const uint8_t kRiffHeader[4] = { 'R', 'I', 'F', 'F' };
  static const uint8_t kWebpType[4] = { 'W', 'E', 'B', 'P' };
  uint32_t new_riff_size = static_cast<uint32_t>(encoded.size() + 4);
  output.insert(output.end(), kRiffHeader, kRiffHeader + 4);
  output.insert(output.end(), reinterpret_cast<const uint8_t*>(&new_riff_size),
                reinterpret_cast<const uint8_t*>(&new_riff_size) + 4);
  output.insert(output.end(), kWebpType, kWebpType + 4);
  output.insert(output.end(), encoded.begin(), encoded.end());

  // ---- Verify the rebuilt file decodes to byte-identical RGBA pixels. ----
  int verify_width = 0;
  int verify_height = 0;
  uint8_t* verify_rgba = WebPDecodeRGBA(output.data(), output.size(), &verify_width,
                                        &verify_height);
  bool identical = verify_rgba != nullptr && verify_width == width && verify_height == height;
  if (identical) {
    identical = memcmp(verify_rgba, orig_rgba, static_cast<size_t>(width) * height * 4) == 0;
  }
  if (verify_rgba != nullptr)
    WebPFree(verify_rgba);
  WebPFree(orig_rgba);

  if (!identical) {
    cerr << "WebP verification decode failed, skipped to keep lossless guarantee." << endl;
    return Format::Leanify(size_leanified);
  }

  if (output.size() >= size_) {
    VerbosePrint("WebP recompressed size is not smaller, keeping original.");
    return Format::Leanify(size_leanified);
  }

  VerbosePrint("WebP losslessly recompressed, ", size_ - output.size(), " bytes removed.");
  // The output is fully built before touching the mapped buffer, and a smaller payload is
  // always written in one contiguous copy, so a failure can never leave a torn file.
  memmove(fp_ - size_leanified, output.data(), output.size());
  fp_ -= size_leanified;
  size_ = output.size();
  return size_;
}

#else

#include <iostream>

const uint8_t Webp::header_magic[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                       'W', 'E', 'B', 'P' };

size_t Webp::Leanify(size_t size_leanified /*= 0*/) {
  std::cerr << "WebP support is not compiled in (libwebp unavailable at build time), skipped."
            << std::endl;
  return Format::Leanify(size_leanified);
}

#endif  // ENABLE_WEBP
