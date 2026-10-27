#pragma once

#include "Controls.h"
#include "SweepClock.h"
#include "Target.h"
#include "Worlds.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <future>
#include <string>
#include <vector>

namespace isohypse
{
/**
    What a frame drew with, for the harness and the log.
*/
struct FrameState
{
	Kind kind        = Kind::Globe;
	int projection   = 0;
	double lines     = 0.0;
	double datum     = 0.0;
	double flowPhase = 0.0;
	double sweepPosition = 0.0;
	int sweepDirection   = 0;
	double centreLon = 0.0, centreLat = 0.0, roll = 0.0, zoom = 1.0;
	bool hasSea      = false;
	bool drewWorld   = false;
	int heightWidth = 0, heightHeight = 0;
};

/**
    The plugin: the source (a world's contours on the style's ground) and,
    with `isEffect`, the Over (the same over the clip, or the clip's own luma
    as the height). One class, two registrations; see SourcePlugin.cpp and
    EffectPlugin.cpp.

    A world's heightfield is decoded off the GL thread when it is chosen and
    uploaded on the first frame after: the map keeps drawing the world it
    had until the new one is ready, so changing World mid-show never shows
    black. The first world of all is decoded before the first frame, so
    there is never a frame with no world. The sweep, the spin and the drift are CPU state in double.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate; everything
    inside works in ParamIds.
*/
class IsohypsePlugin : public CFFGLPlugin
{
public:
	explicit IsohypsePlugin( bool isEffect );
	~IsohypsePlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	unsigned int ParamCount() const
	{
		return static_cast< unsigned int >( hostOrder.size() );
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// Decode a newly chosen world on the GL thread, before the frame that
	/// asked for it: the harness wants the frame it renders to show it.
	void SetSynchronousLoadForTest( bool on )
	{
		synchronousLoad = on;
	}
	void SetSweepPassesForTest( double passes )
	{
		sweep.SetPassesForTest( passes );
	}
	/// The map shader's negative controls (Shaders.cpp, TestFlags). Flag 1
	/// also drops the half-level offset a world without a sea gets.
	void SetTestFlagsForTest( int flags )
	{
		testFlags = flags;
	}
	const FrameState& LastFrame() const
	{
		return frameState;
	}
	const std::string& WorldError() const
	{
		return worldError;
	}
	GLuint PictureTextureID() const
	{
		return scene.Texture( 0 );
	}
	GLuint LightTextureID() const
	{
		return scene.Texture( 1 );
	}
	GLuint HeightTextureID() const;

private:
	void UpdateClock();
	/// Make the chosen world's heightfield current, or start decoding it.
	void ensureWorld( Choice choice, int globe, int place );
	void uploadWorld( const WorldInfo& info, const Heightfield& field, int key );
	void releaseWorld();
	void regenerate();

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader generateShader, intakeShader, mapShader, glowDownShader, glowBlurShader, compositeShader;
	ffglex::FFGLScreenQuad quad;
	Target scene, glowA, glowB, generated, clipHeight;
	GLuint blankTexture = 0;

	//The loaded world.
	GLuint worldTexture = 0;
	int worldKey        = -1;
	WorldInfo worldInfo;
	int worldTop = 0;
	std::string worldError;
	//The one being decoded.
	std::future< Heightfield > pending;
	int pendingKey = -1;
	WorldInfo pendingInfo;
	int failedKey        = -1;
	bool synchronousLoad = false;

	//Generated: what the map was last drawn with.
	bool generatedValid = false;
	float generatedKey[ 5 ] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
	double morphClock    = 0.0;

	SweepClock sweep;
	double spinAngle = 0.0;///< degrees
	double flowPhase = 0.0;///< levels
	bool restartHeld = false;

	//The host's tempo: its bars counted from the bar phase wrapping.
	bool beatSeen        = false;
	float hostBpm        = 120.0f;
	double lastBarPhase  = -1.0;
	double barsCounted   = 0.0;
	double hostBarPhase  = 0.0;

	//Time. Resolume has sent both seconds and milliseconds (see millpond).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;

	FrameState frameState;
	int testFlags = 0;
};

} // namespace isohypse
