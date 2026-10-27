#include "Isohypse.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

using namespace ffglex;

namespace isohypse
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

constexpr int kClockVotes = 4;
/// Host seconds. A bigger forward step, or any backward one, is a jump -- a
/// clip trigger or a scrub -- and no time passes across it.
constexpr double kMaxFrameDelta = 1.0;
/// A place's key, so a globe and a place never share one.
constexpr int kPlaceKeys = 10000;
/// The Generated world's map: plenty for a globe at 4K, cheap to redraw
/// every frame while it morphs.
constexpr int kGeneratedWidth = 2048, kGeneratedHeight = 1024;
/// Flow wraps here: a whole number of every Index Every, so the index
/// contours never jump when it does.
constexpr double kFlowWrap = 2520.0;

//---------------------------------------------------------------------------
// Colour. The styles are written in sRGB, as a designer picks colours, and
// handed to the GPU as linear light.
//---------------------------------------------------------------------------
struct Rgb
{
	float r, g, b;
};

float linearFromSrgb( float c )
{
	return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
}
Rgb toLinear( Rgb c )
{
	return { linearFromSrgb( c.r ), linearFromSrgb( c.g ), linearFromSrgb( c.b ) };
}
/// Hue, saturation, value to sRGB, hue 0..1 round the wheel from red.
Rgb hsv( double hue, double saturation, double value )
{
	hue -= std::floor( hue );
	const double h = hue * 6.0;
	const int i    = static_cast< int >( std::floor( h ) ) % 6;
	const double f = h - std::floor( h );
	const double p = value * ( 1.0 - saturation ), q = value * ( 1.0 - saturation * f ),
	             t = value * ( 1.0 - saturation * ( 1.0 - f ) );
	double r = 0, g = 0, b = 0;
	switch( i )
	{
	case 0: r = value, g = t, b = p; break;
	case 1: r = q, g = value, b = p; break;
	case 2: r = p, g = value, b = t; break;
	case 3: r = p, g = q, b = value; break;
	case 4: r = t, g = p, b = value; break;
	default: r = value, g = p, b = q; break;
	}
	return { static_cast< float >( r ), static_cast< float >( g ), static_cast< float >( b ) };
}

struct Palette
{
	Rgb ground, line, index, accent, seaLine, seaFill, flood, disc;
	bool darkGround;
	float glowBoost;
};

Palette paletteOf( Style style, double hue )
{
	switch( style )
	{
	case Style::Plotter:
		return { { 0.94f, 0.92f, 0.87f }, { 0.10f, 0.11f, 0.17f }, { 0.05f, 0.05f, 0.09f }, hsv( hue, 0.8, 0.7 ),
			     { 0.18f, 0.32f, 0.58f }, { 0.76f, 0.84f, 0.90f }, { 0.50f, 0.66f, 0.84f }, { 0.90f, 0.88f, 0.82f }, false, 0.5f };
	case Style::Neon:
		return { { 0.0f, 0.0f, 0.01f }, hsv( hue, 0.85, 1.0 ), hsv( hue, 0.6, 1.0 ), { 1.0f, 0.97f, 0.92f },
			     hsv( hue + 0.5, 0.8, 0.9 ), { 0.0f, 0.02f, 0.06f }, hsv( hue, 0.9, 0.35 ), { 0.01f, 0.01f, 0.03f }, true, 1.8f };
	case Style::Survey:
		return { { 0.97f, 0.95f, 0.89f }, { 0.66f, 0.42f, 0.22f }, { 0.46f, 0.27f, 0.12f }, hsv( hue, 0.85, 0.8 ),
			     { 0.30f, 0.50f, 0.78f }, { 0.74f, 0.86f, 0.95f }, { 0.45f, 0.65f, 0.88f }, { 0.93f, 0.91f, 0.84f }, false, 0.5f };
	case Style::Ink:
	default:
		return { { 0.0f, 0.0f, 0.0f }, { 0.90f, 0.90f, 0.88f }, { 1.0f, 1.0f, 1.0f }, hsv( hue, 0.85, 1.0 ),
			     { 0.50f, 0.66f, 0.92f }, { 0.04f, 0.08f, 0.16f }, hsv( hue, 0.7, 0.45 ), { 0.06f, 0.06f, 0.07f }, true, 1.0f };
	}
}

void setColour( FFGLShader& shader, const char* name, Rgb srgb )
{
	const Rgb c = toLinear( srgb );
	shader.Set( name, c.r, c.g, c.b );
}

void setUint( FFGLShader& shader, const char* name, uint32_t value )
{
	glUniform1ui( glGetUniformLocation( shader.GetGLID(), name ), value );
}

/// The globe's orientation, world from view, row-major:
/// world = rotY( lon ) * rotX( -lat ) * rotZ( roll ), so the view's centre
/// lands on ( lat, lon ) and east is to the right.
void orientation( double lonDeg, double latDeg, double rollDeg, float out[ 9 ] )
{
	const double a = lonDeg * kPi / 180.0, b = -latDeg * kPi / 180.0, c = rollDeg * kPi / 180.0;
	const double Ry[ 9 ] = { std::cos( a ), 0, std::sin( a ), 0, 1, 0, -std::sin( a ), 0, std::cos( a ) };
	const double Rx[ 9 ] = { 1, 0, 0, 0, std::cos( b ), -std::sin( b ), 0, std::sin( b ), std::cos( b ) };
	const double Rz[ 9 ] = { std::cos( c ), -std::sin( c ), 0, std::sin( c ), std::cos( c ), 0, 0, 0, 1 };
	double t[ 9 ], m[ 9 ];
	auto mul = []( const double* x, const double* y, double* r ) {
		for( int i = 0; i < 3; ++i )
			for( int j = 0; j < 3; ++j )
				r[ i * 3 + j ] = x[ i * 3 ] * y[ j ] + x[ i * 3 + 1 ] * y[ 3 + j ] + x[ i * 3 + 2 ] * y[ 6 + j ];
	};
	mul( Ry, Rx, t );
	mul( t, Rz, m );
	for( int i = 0; i < 9; ++i )
		out[ i ] = static_cast< float >( m[ i ] );
}

double wrapDegrees( double d )
{
	return d - 360.0 * std::floor( ( d + 180.0 ) / 360.0 );
}

int globeNamed( const char* name )
{
	const auto& globes = Globes();
	for( size_t i = 0; i < globes.size(); ++i )
		if( std::strcmp( globes[ i ].name, name ) == 0 )
			return static_cast< int >( i );
	return 0;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
IsohypsePlugin::IsohypsePlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	//-------------------------------------------------------------------
	// Defaults. The source: the Moon, turning, a sweep climbing out of its
	// basins every eight seconds. The Over: the clip as its own map.
	//-------------------------------------------------------------------
	params[ PT_WORLD ] = static_cast< float >(
		isEffect ? WorldIndexOf( Choice::Clip, 0, true ) : WorldIndexOf( Choice::Globe, globeNamed( "Moon" ), false ) );
	params[ PT_PLACE ]         = 0.0f;
	params[ PT_VIEW ]          = static_cast< float >( View::Globe );
	params[ PT_LONGITUDE ]     = ParamFromLongitude( 0.0 );
	params[ PT_LATITUDE ]      = ParamFromLatitude( 0.0 );
	params[ PT_ZOOM ]          = ParamFromZoom( 1.0 );
	params[ PT_SPIN ]          = ParamFromSpin( isEffect ? 0.0 : 4.0 );
	params[ PT_ROTATION ]      = 0.5f;
	params[ PT_LINES ]         = ParamFromLines( isEffect ? 24.0 : 40.0 );
	params[ PT_INDEX_EVERY ]   = 5.0f;
	params[ PT_LINE_WIDTH ]    = ParamFromLineWidth( 1.4 );
	params[ PT_SMOOTH ]        = isEffect ? 0.45f : 0.25f;
	params[ PT_SEA_FILL ]      = 0.5f;
	params[ PT_DEPTHS ]        = 0.5f;
	params[ PT_SWEEP ]         = static_cast< float >( Sweep::Rise );
	params[ PT_SWEEP_TIME ]    = ParamFromSweepSeconds( 8.0 );
	params[ PT_SWEEP_BARS ]    = 0.0f;
	params[ PT_SWEEP_LEVEL ]   = 0.5f;
	params[ PT_SWEEP_GLOW ]    = 0.6f;
	params[ PT_TRAIL ]         = 0.35f;
	params[ PT_FLOOR ]         = 0.2f;
	params[ PT_FLOOD ]         = 0.0f;
	params[ PT_FLOW ]          = ParamFromFlow( 0.0 );
	params[ PT_STYLE ]         = static_cast< float >( Style::Ink );
	params[ PT_HUE ]           = 0.07f;
	params[ PT_HEIGHT_COLOUR ] = 0.0f;
	params[ PT_GLOW ]          = 0.35f;
	params[ PT_SHADE ]         = 0.0f;
	params[ PT_TINTS ]         = 0.0f;
	params[ PT_WOBBLE ]        = 0.0f;
	params[ PT_BACKGROUND ]    = 1.0f;
	params[ PT_TERRAIN ]       = static_cast< float >( Terrain::Continents );
	params[ PT_SEED ]          = 7.0f;
	params[ PT_FEATURE_SIZE ]  = 0.5f;
	params[ PT_ROUGHNESS ]     = 0.5f;
	params[ PT_GENERATED_SEA ] = 0.5f;
	params[ PT_MORPH ]         = 0.0f;
	params[ PT_CLIP_SHOW ]     = 0.6f;
	params[ PT_INVERT ]        = 0.0f;
	params[ PT_MIX ]           = 1.0f;

	auto option = [ this ]( unsigned int host, const char* name, float value, int count, auto nameOf ) {
		SetOptionParamInfo( host, name, static_cast< unsigned int >( count ), value );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( host, static_cast< unsigned int >( i ), nameOf( i ), static_cast< float >( i ) );
	};
	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		const char* name      = NameOf( id );
		switch( id )
		{
		case PT_WORLD:
			option( host, name, params[ id ], WorldChoiceCount( isEffect ),
			        [ this ]( int i ) { return WorldChoiceName( i, isEffect ); } );
			break;
		case PT_PLACE:
			if( Places().empty() )
				option( host, name, 0.0f, 1, []( int ) { return "None"; } );
			else
				option( host, name, params[ id ], static_cast< int >( Places().size() ),
				        []( int i ) { return Places()[ static_cast< size_t >( i ) ].name; } );
			break;
		case PT_VIEW:
			option( host, name, params[ id ], static_cast< int >( View::Count ), []( int i ) { return ViewName( static_cast< View >( i ) ); } );
			break;
		case PT_SWEEP:
			option( host, name, params[ id ], static_cast< int >( Sweep::Count ), []( int i ) { return SweepName( static_cast< Sweep >( i ) ); } );
			break;
		case PT_SWEEP_BARS:
			option( host, name, params[ id ], kBarChoices, []( int i ) { return BarsName( i ); } );
			break;
		case PT_STYLE:
			option( host, name, params[ id ], static_cast< int >( Style::Count ), []( int i ) { return StyleName( static_cast< Style >( i ) ); } );
			break;
		case PT_TERRAIN:
			option( host, name, params[ id ], static_cast< int >( Terrain::Count ),
			        []( int i ) { return TerrainName( static_cast< Terrain >( i ) ); } );
			break;
		case PT_INDEX_EVERY:
			//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an
			//integer is declared with its real default and range.
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, 0.0f, static_cast< float >( kMaxIndexEvery ) );
			break;
		case PT_SEED:
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, 0.0f, static_cast< float >( kMaxSeed ) );
			break;
		case PT_RESTART:
			SetParamInfo( host, name, FF_TYPE_EVENT, false );
			break;
		case PT_INVERT:
			SetParamInfo( host, name, FF_TYPE_BOOLEAN, params[ id ] > 0.5f );
			break;
		case PT_ABOUT_TEXT:
			SetParamInfo( host, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
			break;
		default:
			if( id > PT_ABOUT_TEXT )
				SetParamInfo( host, stoatworks::about::buttons()[ id - PT_ABOUT_TEXT - 1 ].label, FF_TYPE_EVENT, false );
			else
				SetParamInfo( host, name, FF_TYPE_STANDARD, params[ id ] );
			break;
		}
		SetParamGroup( host, GroupOf( id ) );
	}
}

IsohypsePlugin::~IsohypsePlugin()
{
	if( pending.valid() )
		pending.wait();
}

//---------------------------------------------------------------------------
FFResult IsohypsePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer="
	            + glStringOrUnknown( GL_RENDERER ) + " version=" + glStringOrUnknown( GL_VERSION ) );
	diag::info( "worlds from " + DataLocation() );

	using namespace shaders;
	const std::string quadVertex = std::string( kVersion ) + kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		std::string fragment;
		const char* name;
	};
	const Stage stages[] = {
		{ &generateShader, Assemble( { kGenerateFragment } ), "generate" },
		{ &intakeShader, Assemble( { kIntakeFragment } ), "intake" },
		{ &mapShader, Assemble( { kMapLibrary, kMapFragment } ), "map" },
		{ &glowDownShader, Assemble( { kGlowDownFragment } ), "glow down" },
		{ &glowBlurShader, Assemble( { kGlowBlurFragment } ), "glow blur" },
		{ &compositeShader, Assemble( { kCompositeFragment } ), "composite" },
	};
	for( const Stage& stage : stages )
	{
		if( stage.shader->Compile( quadVertex.c_str(), stage.fragment.c_str() ) )
			continue;
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Isohypse: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//A sampler bound to texture 0 is "unloadable" to Apple's GL (boreal's
	//trap): every sampler a pass does not use gets this.
	GLint previous = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previous );
	glGenTextures( 1, &blankTexture );
	glBindTexture( GL_TEXTURE_2D, blankTexture );
	const float black[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previous ) );

	diag::info( isEffect ? "initialised (Over)" : "initialised (source)" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void IsohypsePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via langton).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

//---------------------------------------------------------------------------
// The world.
//---------------------------------------------------------------------------
void IsohypsePlugin::ensureWorld( Choice choice, int globe, int place )
{
	const WorldInfo* info = nullptr;
	int key               = -1;
	if( choice == Choice::Globe && !Globes().empty() )
	{
		info = &Globes()[ static_cast< size_t >( globe ) ];
		key  = globe;
	}
	else if( choice == Choice::Place && place >= 0 )
	{
		info = &Places()[ static_cast< size_t >( place ) ];
		key  = kPlaceKeys + place;
	}
	if( info == nullptr )
		return;

	auto finish = [ this ]( const Heightfield& field, const WorldInfo& from, int fromKey ) {
		if( !field.error.empty() )
		{
			worldError = field.error;
			failedKey  = fromKey;
			diag::error( field.error );
			return;
		}
		uploadWorld( from, field, fromKey );
	};

	if( pending.valid() && pending.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
	{
		const Heightfield field = pending.get();
		if( pendingKey == key )
		{
			finish( field, pendingInfo, pendingKey );
			return;
		}
		//Decoded for a world no longer wanted: dropped.
	}
	if( key == worldKey || key == failedKey || pending.valid() )
		return;
	//The first world is decoded before the first frame: a one-off wait of
	//40 to 110 ms where the alternative is a layer (or a host's thumbnail)
	//that starts black. After that, nothing waits.
	if( synchronousLoad || worldKey < 0 )
	{
		finish( LoadHeightfield( *info ), *info, key );
		return;
	}
	pendingKey  = key;
	pendingInfo = *info;
	pending     = std::async( std::launch::async, []( WorldInfo world ) { return LoadHeightfield( world ); }, *info );
}

void IsohypsePlugin::uploadWorld( const WorldInfo& info, const Heightfield& field, int key )
{
	GLint previousTexture = 0, previousUnpackBuffer = 0, alignment = 4, rowLength = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
	glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpackBuffer );
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
	glGetIntegerv( GL_UNPACK_ROW_LENGTH, &rowLength );
	//A host that leaves a pixel buffer bound would have our pointer read as
	//an offset into it.
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 2 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );

	if( worldTexture == 0 )
		glGenTextures( 1, &worldTexture );
	glBindTexture( GL_TEXTURE_2D, worldTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RG16, field.width, field.height, 0, GL_RG, GL_UNSIGNED_SHORT, field.texels.data() );
	glGenerateMipmap( GL_TEXTURE_2D );

	glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, rowLength );
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, static_cast< GLuint >( previousUnpackBuffer ) );
	glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previousTexture ) );
	SetHeightfieldSampling( worldTexture, info.kind == Kind::Globe );

	worldInfo  = info;
	worldKey   = key;
	worldTop   = TopMipLevel( field.width, field.height );
	worldError.clear();
	diag::info( std::string( "loaded " ) + info.name + " (" + std::to_string( field.width ) + "x"
	            + std::to_string( field.height ) + ")" );
}

void IsohypsePlugin::releaseWorld()
{
	if( worldTexture )
		glDeleteTextures( 1, &worldTexture );
	worldTexture = 0;
	worldKey     = -1;
	failedKey    = -1;
}

void IsohypsePlugin::regenerate()
{
	const float key[ 5 ] = { params[ PT_TERRAIN ], params[ PT_SEED ], params[ PT_FEATURE_SIZE ], params[ PT_ROUGHNESS ],
		                     static_cast< float >( morphClock ) };
	if( generatedValid && std::equal( std::begin( key ), std::end( key ), std::begin( generatedKey ) ) )
		return;
	if( !generated.Ensure( kGeneratedWidth, kGeneratedHeight, { GL_RG16, 1, true, true, true } ) )
		return;
	std::copy( std::begin( key ), std::end( key ), std::begin( generatedKey ) );
	generatedValid = true;

	const int seed       = std::clamp( static_cast< int >( std::lround( params[ PT_SEED ] ) ), 0, kMaxSeed );
	const uint32_t salt  = static_cast< uint32_t >( seed ) * 2654435761u + 0x6a09e667u;
	const double m       = morphClock;
	const float offset[ 3 ] = { static_cast< float >( 17.0 * ( ( salt >> 8 ) & 255u ) / 255.0 + m ),
		                        static_cast< float >( 17.0 * ( ( salt >> 16 ) & 255u ) / 255.0 + 0.61 * m ),
		                        static_cast< float >( 17.0 * ( salt >> 24 ) / 255.0 - 0.37 * m ) };
	generated.Bind();
	ScopedShaderBinding shader( generateShader.GetGLID() );
	generateShader.Set( "Size", static_cast< float >( kGeneratedWidth ), static_cast< float >( kGeneratedHeight ) );
	generateShader.Set( "TerrainKind", OptionIndex( params[ PT_TERRAIN ], static_cast< int >( Terrain::Count ) ) );
	generateShader.Set( "Frequency", static_cast< float >( FeatureFrequencyFromParam( params[ PT_FEATURE_SIZE ] ) ) );
	generateShader.Set( "Gain", 0.3f + 0.35f * std::clamp( params[ PT_ROUGHNESS ], 0.0f, 1.0f ) );
	generateShader.Set( "Offset", offset[ 0 ], offset[ 1 ], offset[ 2 ] );
	setUint( generateShader, "Seed", salt );
	quad.Draw();
	generated.GenerateMips();
}

GLuint IsohypsePlugin::HeightTextureID() const
{
	switch( frameState.kind )
	{
	case Kind::Generated: return generated.Texture();
	case Kind::Clip: return clipHeight.Texture();
	default: return worldTexture;
	}
}

//---------------------------------------------------------------------------
FFResult IsohypsePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = input ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height          = input ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	glDisable( GL_BLEND );

	//-------------------------------------------------------------------
	// Time: the host's, as real elapsed seconds, frame to frame, in double.
	//-------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	double dt = 0.0;
	if( lastNow >= 0.0 )
	{
		const double step = now - lastNow;
		if( step >= 0.0 && step <= kMaxFrameDelta )
			dt = step;
	}
	lastNow = now;

	//-------------------------------------------------------------------
	// What to draw, and the clocks.
	//-------------------------------------------------------------------
	int globe          = 0;
	const Choice choice = ChoiceOf( OptionIndex( params[ PT_WORLD ], WorldChoiceCount( isEffect ) ), isEffect, globe );
	const int place = Places().empty() ? -1 : OptionIndex( params[ PT_PLACE ], static_cast< int >( Places().size() ) );
	ensureWorld( choice, globe, place );

	SweepInputs in;
	in.mode    = static_cast< Sweep >( OptionIndex( params[ PT_SWEEP ], static_cast< int >( Sweep::Count ) ) );
	in.seconds = SweepSecondsFromParam( params[ PT_SWEEP_TIME ] );
	in.bars    = BarsOf( OptionIndex( params[ PT_SWEEP_BARS ], kBarChoices ) );
	in.level   = std::clamp( static_cast< double >( params[ PT_SWEEP_LEVEL ] ), 0.0, 1.0 );
	const bool restartDown = params[ PT_RESTART ] > 0.5f;
	in.restart             = restartDown && !restartHeld;
	restartHeld            = restartDown;
	in.haveBeat            = beatSeen;
	in.barCount            = barsCounted + hostBarPhase;
	in.fallbackBpm         = beatSeen ? hostBpm : 120.0;
	sweep.Advance( dt, in );
	spinAngle = wrapDegrees( spinAngle + dt * SpinFromParam( params[ PT_SPIN ] ) );
	flowPhase += dt * FlowFromParam( params[ PT_FLOW ] );
	flowPhase -= kFlowWrap * std::floor( flowPhase / kFlowWrap );
	morphClock += dt * MorphFromParam( params[ PT_MORPH ] );

	//-------------------------------------------------------------------
	// The heightfield, and how the frame is projected onto it.
	//-------------------------------------------------------------------
	const double lines = LinesFromParam( params[ PT_LINES ] );
	const View view    = static_cast< View >( OptionIndex( params[ PT_VIEW ], static_cast< int >( View::Count ) ) );
	FrameState state;
	state.lines     = lines;
	state.flowPhase = flowPhase;
	GLuint heightTexture = 0;
	int heightWidth = 1, heightHeight = 1, heightTop = 0;
	double datum = ( testFlags & 1 ) != 0 ? 0.0 : -0.5 / lines;
	bool hasSea  = false;
	Kind kind    = Kind::Globe;
	double aspect = 1.0;

	if( choice == Choice::Generated )
	{
		regenerate();
		kind          = Kind::Generated;
		heightTexture = generated.Texture();
		heightWidth   = kGeneratedWidth;
		heightHeight  = kGeneratedHeight;
		heightTop     = generated.TopLevel();
		if( OptionIndex( params[ PT_TERRAIN ], static_cast< int >( Terrain::Count ) ) == static_cast< int >( Terrain::Continents ) )
		{
			hasSea = true;
			datum  = std::clamp( static_cast< double >( params[ PT_GENERATED_SEA ] ), 0.0, 1.0 );
		}
	}
	else if( choice == Choice::Clip && input != nullptr )
	{
		kind = Kind::Clip;
		if( clipHeight.Ensure( width, height, { GL_RG16, 1, true, true, false } ) )
		{
			clipHeight.Bind();
			ScopedShaderBinding shader( intakeShader.GetGLID() );
			bindUnit( 0, input->Handle );
			intakeShader.Set( "ClipIn", 0 );
			intakeShader.Set( "InvertLuma", params[ PT_INVERT ] > 0.5f ? 1 : 0 );
			quad.Draw();
			bindUnit( 0, 0 );
			clipHeight.GenerateMips();
			heightTexture = clipHeight.Texture();
			heightWidth   = width;
			heightHeight  = height;
			heightTop     = clipHeight.TopLevel();
		}
	}
	else if( worldKey >= 0 )
	{
		//The world on the GPU: the one asked for, or the last one while the
		//new one decodes.
		kind          = worldInfo.kind;
		heightTexture = worldTexture;
		heightWidth   = worldInfo.width;
		heightHeight  = worldInfo.height;
		heightTop     = worldTop;
		hasSea        = worldInfo.hasSea;
		if( hasSea )
			datum = worldInfo.SeaLevel();
		aspect = static_cast< double >( worldInfo.width ) / std::max( worldInfo.height, 1 );
	}

	int projection = 0;
	double centreX = 0.0, centreY = 0.0, roll = RotationFromParam( params[ PT_ROTATION ] );
	if( kind == Kind::Globe || kind == Kind::Generated )
	{
		projection = view == View::Globe ? 1 : 0;
		centreX    = wrapDegrees( LongitudeFromParam( params[ PT_LONGITUDE ] ) + spinAngle );
		centreY    = LatitudeFromParam( params[ PT_LATITUDE ] );
	}
	else if( kind == Kind::Place )
	{
		projection = 2;
		centreX    = std::clamp( params[ PT_LONGITUDE ], 0.0f, 1.0f ) - 0.5;
		centreY    = std::clamp( params[ PT_LATITUDE ], 0.0f, 1.0f ) - 0.5;
		roll       = wrapDegrees( roll + spinAngle );
	}
	else
	{
		projection = 3;
		roll       = 0.0;
	}
	//A place at Zoom 1 covers the frame: fitted to the wider of the two.
	double zoom = ZoomFromParam( params[ PT_ZOOM ] );
	if( kind == Kind::Place )
		zoom *= std::max( 1.0, ( static_cast< double >( width ) / height ) / aspect );
	const double rasterK = height / 1080.0;

	state.kind           = kind;
	state.projection     = projection;
	state.datum          = datum;
	state.hasSea         = hasSea;
	state.sweepPosition  = sweep.Position();
	state.sweepDirection = sweep.Direction();
	state.centreLon      = centreX;
	state.centreLat      = centreY;
	state.roll           = roll;
	state.zoom           = zoom;
	state.drewWorld      = heightTexture != 0;
	state.heightWidth    = heightWidth;
	state.heightHeight   = heightHeight;
	frameState           = state;

	const Style style     = static_cast< Style >( OptionIndex( params[ PT_STYLE ], static_cast< int >( Style::Count ) ) );
	const Palette palette = paletteOf( style, params[ PT_HUE ] );

	//-------------------------------------------------------------------
	// The map.
	//-------------------------------------------------------------------
	if( !scene.Ensure( width, height, { GL_RGBA16F, 2, true, false, false } ) )
		return FF_FAIL;
	scene.Bind();
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	if( heightTexture != 0 )
	{
		ScopedShaderBinding shader( mapShader.GetGLID() );
		FFGLShader& s = mapShader;
		bindUnit( 0, heightTexture );
		s.Set( "Height", 0 );
		s.Set( "HeightSize", static_cast< float >( heightWidth ), static_cast< float >( heightHeight ) );
		s.Set( "HeightTop", static_cast< float >( heightTop ) );
		s.Set( "Projection", projection );
		s.Set( "WrapX", kind == Kind::Globe || kind == Kind::Generated ? 1 : 0 );
		s.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		s.Set( "ViewOrigin", 0.0f, 0.0f );
		s.Set( "Centre", static_cast< float >( centreX ), static_cast< float >( centreY ) );
		s.Set( "Zoom", static_cast< float >( zoom ) );
		s.Set( "Roll", static_cast< float >( roll * kPi / 180.0 ) );
		float m[ 9 ];
		orientation( centreX, centreY, roll, m );
		glUniformMatrix3fv( glGetUniformLocation( s.GetGLID(), "Orientation" ), 1, GL_TRUE, m );
		s.Set( "TexAspect", static_cast< float >( aspect ) );
		s.Set( "SmoothBias", static_cast< float >( SmoothFromParam( params[ PT_SMOOTH ] ) ) );
		s.Set( "Lines", static_cast< float >( lines ) );
		s.Set( "Datum", static_cast< float >( datum ) );
		s.Set( "FlowPhase", static_cast< float >( flowPhase ) );
		s.Set( "HasSea", hasSea ? 1 : 0 );
		s.Set( "IndexEvery", std::clamp( static_cast< int >( std::lround( params[ PT_INDEX_EVERY ] ) ), 0, kMaxIndexEvery ) );
		s.Set( "LineWidth", static_cast< float >( LineWidthFromParam( params[ PT_LINE_WIDTH ] ) * rasterK ) );
		s.Set( "SeaFill", std::clamp( params[ PT_SEA_FILL ], 0.0f, 1.0f ) );
		s.Set( "Depths", std::clamp( params[ PT_DEPTHS ], 0.0f, 1.0f ) );
		const float wobble = std::clamp( params[ PT_WOBBLE ], 0.0f, 1.0f );
		s.Set( "WobbleAmount", static_cast< float >( wobble * 4.0 * rasterK ) );
		s.Set( "WobbleScale", static_cast< float >( 90.0 * rasterK ) );
		s.Set( "SweepMode", static_cast< int >( in.mode ) );
		s.Set( "SweepDir", sweep.Direction() );
		s.Set( "SweepPos", static_cast< float >( sweep.Position() ) );
		s.Set( "Trail", static_cast< float >( TrailFromParam( params[ PT_TRAIL ] ) ) );
		s.Set( "FloorLevel", std::clamp( params[ PT_FLOOR ], 0.0f, 1.0f ) );
		s.Set( "SweepGlow", std::clamp( params[ PT_SWEEP_GLOW ], 0.0f, 1.0f ) );
		s.Set( "Flood", std::clamp( params[ PT_FLOOD ], 0.0f, 1.0f ) );
		setColour( s, "LineColour", palette.line );
		setColour( s, "IndexColour", palette.index );
		setColour( s, "AccentColour", palette.accent );
		setColour( s, "SeaLineColour", palette.seaLine );
		setColour( s, "SeaFillColour", palette.seaFill );
		setColour( s, "FloodColour", palette.flood );
		setColour( s, "DiscColour", palette.disc );
		s.Set( "HeightColour", std::clamp( params[ PT_HEIGHT_COLOUR ], 0.0f, 1.0f ) );
		s.Set( "Tints", std::clamp( params[ PT_TINTS ], 0.0f, 1.0f ) );
		s.Set( "Shade", std::clamp( params[ PT_SHADE ], 0.0f, 1.0f ) );
		s.Set( "DarkGround", palette.darkGround ? 1 : 0 );
		s.Set( "LandPalette", style == Style::Survey ? 1 : 0 );
		s.Set( "TestFlags", testFlags );
		quad.Draw();
		unbindTextureUnits( 1 );
	}

	//-------------------------------------------------------------------
	// The glow: the map's light, a quarter of the raster, blurred.
	//-------------------------------------------------------------------
	const float glowAmount = std::clamp( params[ PT_GLOW ], 0.0f, 1.0f ) * palette.glowBoost;
	const int glowWidth = ( width + 3 ) / 4, glowHeight = ( height + 3 ) / 4;
	const bool glowing  = glowAmount > 0.0f && heightTexture != 0
	                     && glowA.Ensure( glowWidth, glowHeight, { GL_RGBA16F, 1, true, false, false } )
	                     && glowB.Ensure( glowWidth, glowHeight, { GL_RGBA16F, 1, true, false, false } );
	if( glowing )
	{
		glowA.Bind();
		{
			ScopedShaderBinding shader( glowDownShader.GetGLID() );
			bindUnit( 0, scene.Texture( 1 ) );
			glowDownShader.Set( "Source", 0 );
			glowDownShader.Set( "SourceSize", static_cast< float >( width ), static_cast< float >( height ) );
			quad.Draw();
		}
		ScopedShaderBinding shader( glowBlurShader.GetGLID() );
		glowBlurShader.Set( "Source", 0 );
		glowB.Bind();
		bindUnit( 0, glowA.Texture() );
		glowBlurShader.Set( "Step", 1.0f / glowWidth, 0.0f );
		quad.Draw();
		glowA.Bind();
		bindUnit( 0, glowB.Texture() );
		glowBlurShader.Set( "Step", 0.0f, 1.0f / glowHeight );
		quad.Draw();
		bindUnit( 0, 0 );
	}

	//-------------------------------------------------------------------
	// Composite, into the host's framebuffer.
	//-------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		ScopedShaderBinding shader( compositeShader.GetGLID() );
		FFGLShader& s = compositeShader;
		bindUnit( 0, scene.Texture( 0 ) );
		bindUnit( 1, glowing ? glowA.Texture() : blankTexture );
		bindUnit( 2, input ? input->Handle : blankTexture );
		s.Set( "Picture", 0 );
		s.Set( "Glow", 1 );
		s.Set( "ClipIn", 2 );
		setColour( s, "Ground", palette.ground );
		s.Set( "GroundAlpha", isEffect ? 1.0f : std::clamp( params[ PT_BACKGROUND ], 0.0f, 1.0f ) );
		s.Set( "GlowAmount", glowing ? glowAmount : 0.0f );
		s.Set( "IsEffect", isEffect ? 1 : 0 );
		s.Set( "ClipShow", std::clamp( params[ PT_CLIP_SHOW ], 0.0f, 1.0f ) );
		s.Set( "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
		s.Set( "Raster", static_cast< float >( width ), static_cast< float >( height ) );
		s.Set( "ViewOrigin", static_cast< float >( hostViewport[ 0 ] ), static_cast< float >( hostViewport[ 1 ] ) );
		quad.Draw();
		unbindTextureUnits( 3 );
	}
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult IsohypsePlugin::DeInitGL()
{
	if( pending.valid() )
		pending.wait();
	for( FFGLShader* shader : { &generateShader, &intakeShader, &mapShader, &glowDownShader, &glowBlurShader, &compositeShader } )
		shader->FreeGLResources();
	quad.Release();
	for( Target* target : { &scene, &glowA, &glowB, &generated, &clipHeight } )
		target->Destroy();
	releaseWorld();
	if( blankTexture )
		glDeleteTextures( 1, &blankTexture );
	blankTexture   = 0;
	generatedValid = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult IsohypsePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void IsohypsePlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f )
	{
		beatSeen = true;
		hostBpm  = newBpm;
	}
	const double phase = std::clamp( static_cast< double >( newBarPhase ), 0.0, 1.0 );
	//The phase wraps at every downbeat: a fall of more than half a bar is
	//one, a small step back is the host's jitter.
	if( lastBarPhase >= 0.0 && phase < lastBarPhase - 0.5 )
		barsCounted += 1.0;
	lastBarPhase = phase;
	hostBarPhase = phase;
}

char* IsohypsePlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult IsohypsePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult IsohypsePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	params[ id ] = value;
	return FF_SUCCESS;
}

float IsohypsePlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

void IsohypsePlugin::SetById( unsigned int id, float value )
{
	const int host = HostIndexOf( id );
	if( host >= 0 )
		SetFloatParameter( static_cast< unsigned int >( host ), value );
}

} // namespace isohypse
