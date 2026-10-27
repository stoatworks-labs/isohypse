#include "Isohypse.h"

/**
    The effect: the same map over the clip, or -- World set to Clip -- the
    clip's own brightness read as height and contoured, so live video becomes
    terrain with a sweep running through it.

    The name is the fleet's "Over". See SourcePlugin.cpp for why this file is
    listed in its own target.
*/
namespace
{
class IsohypseEffect : public isohypse::IsohypsePlugin
{
public:
	IsohypseEffect() :
		IsohypsePlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< IsohypseEffect >,// Create method
	"IH02",                         // Plugin unique ID of maximum length 4
	"SW Isohypse Over",             // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_EFFECT,                      // Plugin type
	"Your clip as a contour map: its brightness read as height, contoured, and swept through by a line climbing from "
	"black to white. Or any of the source's worlds drawn over the clip. Clip Show sets how much of the picture stays "
	"under the lines; Mix brings it back.",
	"Isohypse FFGL effect"          // About
);

extern "C" const char* IsohypseEffectBuildStamp()
{
	return "isohypse " ISOHYPSE_VERSION " effect, built " __DATE__ " " __TIME__;
}
