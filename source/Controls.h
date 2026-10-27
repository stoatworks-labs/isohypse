#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in the map's units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could
    widen it. The conversions live in Controls.cpp, one function per control.
    Option, boolean and event parameters hold the element value itself; Index
    Every and Seed are real integers (`FF_TYPE_INTEGER`).

    Units: a LEVEL is one contour interval, and Lines is how many of them span
    the world's whole range of height, lowest point to highest. A PASS is one
    sweep from the bottom of that range to the top. Angles are degrees,
    widths are pixels at 1080 lines (scaled with the raster's height, so a
    look holds from 720p to 4K).
*/
namespace isohypse
{
/**
    Every control either plugin has, by what it is -- NOT by the index a host
    sees. Each plugin declares its own subset, densely, in the order of
    `HostOrder()`, with the About block LAST in both.

    The World and Place dropdowns are APPEND-ONLY: a composition stores the
    element's index, so a world inserted in the middle would swap every saved
    show's map for its neighbour.
*/
enum ParamId : unsigned int
{
	// -- World ---------------------------------------------------------------
	PT_WORLD = 0,
	PT_PLACE,
	PT_VIEW,
	PT_LONGITUDE,
	PT_LATITUDE,
	PT_ZOOM,
	PT_SPIN,
	PT_ROTATION,

	// -- Contours ------------------------------------------------------------
	PT_LINES,
	PT_INDEX_EVERY,
	PT_LINE_WIDTH,
	PT_SMOOTH,
	PT_SEA_FILL,
	PT_DEPTHS,

	// -- Sweep ---------------------------------------------------------------
	PT_SWEEP,
	PT_SWEEP_TIME,
	PT_SWEEP_BARS,
	PT_SWEEP_LEVEL,
	PT_SWEEP_GLOW,
	PT_TRAIL,
	PT_FLOOR,
	PT_FLOOD,
	PT_FLOW,
	PT_RESTART,

	// -- Look ----------------------------------------------------------------
	PT_STYLE,
	PT_HUE,
	PT_HEIGHT_COLOUR,
	PT_GLOW,
	PT_SHADE,
	PT_TINTS,
	PT_WOBBLE,
	PT_BACKGROUND,

	// -- Generated -----------------------------------------------------------
	PT_TERRAIN,
	PT_SEED,
	PT_FEATURE_SIZE,
	PT_ROUGHNESS,
	PT_GENERATED_SEA,
	PT_MORPH,

	// -- Clip (Over only) ----------------------------------------------------
	PT_CLIP_SHOW,
	PT_INVERT,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

/// The ids a plugin declares, in the order the host sees them. Host index i
/// is `HostOrder( effect )[ i ]`. The About block is last in both.
const std::vector< unsigned int >& HostOrder( bool effect );

/// The group each control is shown under.
const char* GroupOf( unsigned int id );

/// The display name of a control.
const char* NameOf( unsigned int id );

enum class View
{
	Map = 0,///< equirectangular: longitude across, latitude up
	Globe,  ///< orthographic: the world as a ball, seen from far off
	Count
};
const char* ViewName( View view );

enum class Sweep
{
	Off = 0,///< every line at full strength, nothing moving through them
	Rise,   ///< the sweep climbs from the lowest point to the highest, and again
	Fall,   ///< the other way
	Bounce, ///< up, then down, then up
	Manual, ///< Sweep Level puts it where you like
	Count
};
const char* SweepName( Sweep sweep );

/// Sweep Bars: Free runs on Sweep Time; the rest lock one pass to that many
/// bars of the host's tempo.
constexpr int kBarChoices = 8;
const char* BarsName( int index );
/// 0 for Free, else the bars in one pass.
int BarsOf( int index );

enum class Style
{
	Ink = 0,///< white lines on black
	Plotter,///< a pen on paper
	Neon,   ///< coloured light
	Survey, ///< the cartographer's: brown contours, blue water, cream paper
	Count
};
const char* StyleName( Style style );

enum class Terrain
{
	Continents = 0,///< land and sea
	Ridges,        ///< mountain chains
	Craters,       ///< a battered moon
	Count
};
const char* TerrainName( Terrain terrain );

int OptionIndex( float value, int count );

//---------------------------------------------------------------------------
// The mappings.
//---------------------------------------------------------------------------

/// Longitude: -180 to +180 degrees, 0 in the middle.
double LongitudeFromParam( float v );
float ParamFromLongitude( double degrees );
/// Latitude: -90 to +90 degrees, 0 in the middle.
double LatitudeFromParam( float v );
float ParamFromLatitude( double degrees );
/// Zoom: 1 (the whole world fits the height) to 64, geometrically.
double ZoomFromParam( float v );
float ParamFromZoom( double zoom );
/// Spin: -60 to +60 degrees a second, 0 in the middle, squared either side
/// so the slow end has room.
double SpinFromParam( float v );
float ParamFromSpin( double degreesPerSecond );
/// Rotation: -180 to +180 degrees.
double RotationFromParam( float v );

/// Lines: levels across the world's whole range, 4 to 300, geometrically.
double LinesFromParam( float v );
float ParamFromLines( double lines );
/// Index Every: 0 (none) to 10.
constexpr int kMaxIndexEvery = 10;
/// Line Width: 0.4 to 10 pixels at 1080 lines, geometrically.
double LineWidthFromParam( float v );
float ParamFromLineWidth( double pixels );
/// Smooth: extra mip levels on the heightfield, 0 to 6.
double SmoothFromParam( float v );

/// Sweep Time: one pass, 0.5 to 120 seconds, geometrically.
double SweepSecondsFromParam( float v );
float ParamFromSweepSeconds( double seconds );
/// Trail: how long a line stays lit after the sweep crosses it, in passes,
/// 0 to 1.
double TrailFromParam( float v );
/// Flow: the whole set of levels drifting, -4 to +4 levels a second, 0 in
/// the middle, squared either side.
double FlowFromParam( float v );
float ParamFromFlow( double levelsPerSecond );

/// Seed: a real integer, 0 to kMaxSeed.
constexpr int kMaxSeed = 999;
/// Feature Size (Generated): the base frequency of the terrain on the unit
/// sphere, 6 at 0 down to 0.6 at 1, geometrically -- a bigger Feature Size
/// is bigger continents, ranges and craters.
double FeatureFrequencyFromParam( float v );
/// Morph: how fast a generated world changes, 0 to 0.2 units a second.
double MorphFromParam( float v );

} // namespace isohypse
