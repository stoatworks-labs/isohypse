#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
    The maps: real heightfields of whole worlds and of places on Earth, and
    the two that are not files (Generated, and the Over's Clip).

    The table is source/WorldTable.inc, which tools/make-worlds.py writes
    beside the PNGs in data/. Every file is a 16-bit greyscale PNG whose value
    is linear in height between the world's lowest and highest point:

        h = min + v / 65535 * ( max - min )        (metres)

    except a world with gaps (Pluto and Charon, which New Horizons saw one
    side of), where 0 means "never measured" and the data runs 1..65535.

    A globe is equirectangular: column 0 is longitude -180, east positive,
    row 0 is the north pole, pixel-is-area. A place is a square Web Mercator
    tile centred on its point, north up, with square ground pixels.

    Where the files live: on macOS in the bundle's Resources/worlds; on
    Windows as RCDATA resources in the DLL itself (a plugin folder gets
    copied about one file at a time); for the harness, ISOHYPSE_DATA_DIR.
*/
namespace isohypse
{
enum class Kind
{
	Globe = 0,
	Place,
	Generated,
	Clip
};

struct WorldInfo
{
	const char* name = "";
	const char* file = nullptr;///< nullptr for Generated and Clip
	Kind kind        = Kind::Generated;
	int width = 0, height = 0;
	double minMetres = 0.0, maxMetres = 1.0;
	bool hasNodata = false, hasSea = false;
	double centreLat = 0.0, centreLon = 0.0, metresPerPixel = 0.0;

	/// The coast as a normalised height: half a metre above sea level. Most
	/// coastal places' tiles have a sea that is exactly 0 m, flat; a coast at 0
	/// would lie on it, and the sea (everything below the coast) would be
	/// empty. Half a metre is nothing to a world.
	double SeaLevel() const
	{
		return maxMetres > minMetres ? ( kCoastMetres - minMetres ) / ( maxMetres - minMetres ) : 0.0;
	}
	static constexpr double kCoastMetres = 0.5;
};

const std::vector< WorldInfo >& Globes();
const std::vector< WorldInfo >& Places();

/// The World dropdown: every globe, then Place (which the Place dropdown
/// picks), then Generated, then -- the Over only -- Clip.
enum class Choice
{
	Globe = 0,
	Place,
	Generated,
	Clip
};
int WorldChoiceCount( bool effect );
const char* WorldChoiceName( int index, bool effect );
/// What a World index is; `globe` gets the globe's index for Choice::Globe.
Choice ChoiceOf( int index, bool effect, int& globe );
int WorldIndexOf( Choice choice, int globe, bool effect );

/// A decoded heightfield: RG16, interleaved, row 0 the north (top) edge. R is
/// the height 0..65535 between the world's min and max; G is 65535 where the
/// world was measured and 0 where it was not. Unmeasured texels carry the
/// height of the measured ground around them, so a texture filter that
/// reaches across the edge of a gap finds no cliff.
struct Heightfield
{
	int width = 0, height = 0;
	std::vector< uint16_t > texels;
	std::string error;
};

/// Read and decode a world's file. Safe off the GL thread.
Heightfield LoadHeightfield( const WorldInfo& world );

/// The folder or module the files are read from, for the log.
std::string DataLocation();

/// A world's resource name in the Windows DLL: the file name upper-cased,
/// everything but A-Z and 0-9 an underscore ("naples-vesuvius.png" is
/// NAPLES_VESUVIUS_PNG). CMakeLists.txt names the RCDATA entries by the same
/// rule, tools/wincheck.ps1 checks the DLL by it, and ihtest --offline holds
/// this function to it.
std::string ResourceNameOf( const char* file );

/// Fill a gap mask's holes from the measured texels around them, by pull-push
/// (averaging down a pyramid of the measured ones, then filling each hole
/// from the coarsest level that saw anything). Exposed for the harness.
void FillGaps( int width, int height, std::vector< float >& values, const std::vector< uint8_t >& measured );

} // namespace isohypse
