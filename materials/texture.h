#pragma once

#include "formats/vtf.h"
#include "render/render.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// VTF -> backend-neutral CPU texture (render::TextureData). Knows nothing about GPU backends.
namespace anvil::materials {

// Decodes one BC1/BC2/BC3 image (w x h, any size) to RGBA8 (R in the lowest byte).
// `blocks` must hold render::textureBytes(format, w, h) bytes. BC1 uses 1-bit-alpha semantics (DXT1).
void decodeBC(render::TextureFormat format, std::string_view blocks, uint32_t w, uint32_t h, uint8_t* rgba);

// Converts one uncompressed VTF image to RGBA8. False for formats without a defined conversion (P8).
bool convertToRGBA8(vtf::Format format, std::string_view src, uint32_t w, uint32_t h, uint8_t* rgba);

// Frame 0, face 0, slice 0, full mip chain (largest first). DXT1/3/5 stay BC1/2/3 when `allowBC`,
// otherwise they are decoded; every other stored format becomes RGBA8. Addressing follows the VTF
// CLAMPS/CLAMPT flags, filtering POINTSAMPLE. Null (with error) for unsupported formats.
std::optional<render::TextureData> textureFromVtf(const vtf::Texture& vtf, bool allowBC, std::string* error = nullptr);

} // namespace anvil::materials
