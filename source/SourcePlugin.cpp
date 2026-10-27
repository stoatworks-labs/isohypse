#include "Isohypse.h"

/**
    The source: a world's contours on the style's ground.

    Listed directly in the IsohypseSource target, not in isohypse_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins
    into both bundles. The core is an OBJECT library because this registers
    itself from a file-scope constructor nothing references (see
    CMakeLists.txt).

    `SW Isohypse` is eleven characters; the FFGL name field is char[ 16 ] and
    not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class IsohypseSource : public isohypse::IsohypsePlugin
{
public:
	IsohypseSource() :
		IsohypsePlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< IsohypseSource >,// Create method
	"IH01",                         // Plugin unique ID of maximum length 4
	"SW Isohypse",                  // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_SOURCE,                      // Plugin type
	"Contour lines of real worlds, swept through by a line climbing from the lowest ground to the highest: the Moon, "
	"Mars, Earth and the rest from their space missions' height maps, twenty-odd places on Earth, or a world it makes "
	"up. A flat map or a turning globe, in ink, on paper, in neon or as a survey sheet.",
	"Isohypse FFGL source"          // About
);

extern "C" const char* IsohypseSourceBuildStamp()
{
	return "isohypse " ISOHYPSE_VERSION " source, built " __DATE__ " " __TIME__;
}
