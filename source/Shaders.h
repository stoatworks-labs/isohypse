#pragma once

#include <initializer_list>
#include <string>

/**
    The GLSL, as text.

    `kCommon` is a LIBRARY, not a shader: no #version, no main. Every pass is
    assembled as kVersion + kCommon + its pieces, so the harness's checks run
    the text the plugin runs. No piece may pass 15 000 bytes (MSVC's C2026
    limit on one string literal), which is why the map pass is two.

    Colour is linear light inside, sRGB at the edges: the style's colours are
    converted on the CPU, the clip on its way in, the output on its way out.

    Passes, in order:

      generate   (Generated) the world's height, from noise on the sphere,
                 into an equirectangular RG16 map, mipmapped
      intake     (Over, Clip) the clip's luma into an RG16 heightfield,
                 mipmapped
      map        the projection, the height (cubic B-spline at the level
                 the pixel's footprint asks for), the contours, the sweep,
                 the fills: the picture premultiplied into attachment 0 and
                 the light the glow spreads into attachment 1
      glow down  attachment 1 to a quarter of the raster, 4x4 averaged
      glow blur  a separable Gaussian, twice
      composite  the ground (or the clip), the picture, the glow, sRGB, Mix
*/
namespace isohypse::shaders
{
extern const char* const kVersion;
extern const char* const kCommon;

extern const char* const kQuadVertex;
extern const char* const kGenerateFragment;
extern const char* const kIntakeFragment;
extern const char* const kMapLibrary;
extern const char* const kMapFragment;
extern const char* const kGlowDownFragment;
extern const char* const kGlowBlurFragment;
extern const char* const kCompositeFragment;

/// kVersion + kCommon + the pieces, in order.
std::string Assemble( std::initializer_list< const char* > pieces );

} // namespace isohypse::shaders
