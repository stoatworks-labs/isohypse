/**
    ihtest -- render Isohypse offline, and measure what it drew.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context. The checks read
    the picture the plugin draws into (premultiplied, linear, before the
    glow and the ground: alpha IS a contour's coverage), the output, and the
    plugin's own CPU state.

        ihtest --out /tmp/i.png           the source, the defaults (the Moon)
        ihtest --over --out /tmp/o.png    the Over on the harness's card
        ihtest --list                     every parameter and its default
        ihtest --set "World=Mars"         a control by name (an option by its
                                          element's name), repeatable
        ihtest --at 90 "Restart=1"        a setting from frame 90 on
        ihtest --frames N                 render N frames before --out (60)
        ihtest --size 1280x720
        ihtest --film N                   N frames, raw RGBA on stdout
        ihtest --over --pipe              raw frames in, raw frames out
        ihtest --script cues              the fleet's cue format
        ihtest --offline                  the checks that need no GL context
        ihtest --lines --width --sweep... one or more GL checks; --all for all
        ihtest --negative                 the wrong models, each of which a
                                          check must catch

    `--script` lines are `frame  Parameter Name  value`, held before the first
    key and after the last. A STANDARD (0..1) control is linear between its
    keys; an option, a boolean, an event or an integer STEPS.

    The worlds are read from ISOHYPSE_DATA_DIR, defaulting to the source
    tree's data/ (the plugin reads its bundle's Resources).
*/

#include "Controls.h"
#include "Isohypse.h"
#include "SweepClock.h"
#include "Worlds.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace isohypse;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;
bool g_quiet   = false;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	if( !g_quiet || !condition )
		std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

void Section( const char* name )
{
	if( !g_quiet )
		std::printf( "\n%s\n", name );
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written top
/// row first. The source is premultiplied: written over black, alpha opaque.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = c == 3 ? 1.0f : rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "IHTEST_RENDERER" );
	const bool software      = renderer != nullptr && std::strcmp( renderer, "software" ) == 0;
	if( CGLChoosePixelFormat( software ? generic : accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
		return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT || type == FF_TYPE_INTEGER;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

/// The Over's card: a soft night skyline -- graded sky, lit blocks, a moon --
/// so the clip has hills, plateaus and edges to contour. Rows bottom first.
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			double l = 0.15 + 0.45 * v;
			const double block = std::floor( u * 14.0 );
			const double top   = 0.25 + 0.3 * std::fabs( std::sin( block * 12.9898 ) );
			if( v < top )
				l = 0.08 + 0.5 * std::pow( std::sin( u * 3.14159 * 7.0 ) * 0.5 + 0.5, 2.0 ) * ( 1.0 - v / top );
			const double dx = ( u - 0.72 ) * width / height, dy = v - 0.72;
			l += 0.6 * std::exp( -( dx * dx + dy * dy ) / 0.01 );
			for( int c = 0; c < 3; ++c )
				card[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ] = static_cast< float >( std::clamp( l * ( c == 2 ? 1.1 : 1.0 ), 0.0, 1.0 ) );
			card[ ( static_cast< size_t >( y ) * width + x ) * 4 + 3 ] = 1.0f;
		}
	return card;
}

/// A grey card from a function of the pixel centre (x + 0.5, y + 0.5).
Floats greyCard( int width, int height, const std::function< double( double, double ) >& luma )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const float l = static_cast< float >( luma( x + 0.5, y + 0.5 ) );
			float* px     = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			px[ 0 ] = px[ 1 ] = px[ 2 ] = l;
			px[ 3 ]                     = 1.0f;
		}
	return card;
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic clock.
//---------------------------------------------------------------------------
struct Rig
{
	IsohypsePlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0, readFBO = 0;
	int frame  = 0;
	double fps = 60.0;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) : plugin( effect )
	{
	}
	~Rig()
	{
		plugin.DeInitGL();
		release();
		if( readFBO )
			glDeleteFramebuffers( 1, &readFBO );
	}
	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		for( GLuint* t : { &outputTexture, &sourceTexture } )
			if( *t )
				glDeleteTextures( 1, t );
		outputFBO = outputTexture = sourceTexture = 0;
	}
	/// `floatClip`: the clip as 32-bit float, for the checks that need its
	/// height exact; otherwise 8 bit, as Resolume's are.
	bool attach( int w, int h, const Floats* picture, bool floatClip )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr, GL_RGBA32F );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data(), floatClip ? GL_RGBA32F : GL_RGBA8 );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		return true;
	}
	bool Init( int w, int h, const Floats* picture = nullptr, bool floatClip = false )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/isohypse for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		plugin.SetSynchronousLoadForTest( true );
		return attach( w, h, picture, floatClip );
	}
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture, false );
	}
	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}
	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			plugin.SetTime( static_cast< double >( frame ) / fps );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}
	Floats Read( GLuint texture )
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		if( !readFBO )
			glGenFramebuffers( 1, &readFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, readFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return pixels;
	}
	Floats Output()
	{
		return Read( outputTexture );
	}
	/// One pixel of the picture, RGBA.
	std::array< float, 4 > PicturePixel( int x, int y )
	{
		std::array< float, 4 > pixel = { 0, 0, 0, 0 };
		if( !readFBO )
			glGenFramebuffers( 1, &readFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, readFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plugin.PictureTextureID(), 0 );
		glReadPixels( x, y, 1, 1, GL_RGBA, GL_FLOAT, pixel.data() );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		return pixel;
	}
	Floats Picture()
	{
		return Read( plugin.PictureTextureID() );
	}
	Floats Light()
	{
		return Read( plugin.LightTextureID() );
	}
};

float at( const Floats& image, int width, int x, int y, int c )
{
	return image[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
}

//---------------------------------------------------------------------------
// Parameters by name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( IsohypsePlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// `Name=Value`. An option takes its element's name or its index; anything
/// that is neither, or not wholly a number, is refused (polyhedral's trap:
/// strtof( "Fixed" ) is 0, silently).
bool applySetting( IsohypsePlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_OPTION )
			{
				const unsigned int count = plugin.GetNumParamElements( parameter.index );
				for( unsigned int e = 0; e < count; ++e )
					if( value == plugin.GetParamElementName( parameter.index, e ) )
					{
						plugin.SetFloatParameter( parameter.index, static_cast< float >( e ) );
						return true;
					}
			}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == nullptr || *end != '\0' )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

bool applyAll( IsohypsePlugin& plugin, const std::vector< std::string >& settings )
{
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( std::istream& in, const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		if( parts.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( parts.back().c_str(), &end );
		if( end == nullptr || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + parts.back() + "' is not a number";
			return {};
		}
		parts.pop_back();
		std::string name = parts.front();
		for( size_t i = 1; i < parts.size(); ++i )
			name += " " + parts[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::stable_sort( entry.second.begin(), entry.second.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return tracks;
}

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

struct Cue
{
	Track track;
	bool ramp;
};

bool bindScript( IsohypsePlugin& plugin, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	const std::map< std::string, Track > tracks = loadScript( file, path, error );
	if( !error.empty() )
		return false;
	const std::vector< NamedParameter > known = listParameters( plugin );
	for( const auto& entry : tracks )
	{
		bool found = false;
		for( const NamedParameter& parameter : known )
			if( parameter.name == entry.first )
			{
				out[ parameter.index ] = Cue { entry.second, !stepsBetweenCues( parameter.type ) };
				found                  = true;
			}
		if( !found )
		{
			error = "script names '" + entry.first + "', which is not a parameter (try --list)";
			return false;
		}
	}
	return true;
}

struct Timed
{
	int frame;
	std::string setting;
};

//===========================================================================
// The checks.
//===========================================================================

/// The contours a row crosses: runs of picture alpha above `threshold`, each
/// as its coverage-weighted centre and its total coverage (its width in
/// pixels, for a line square to the row).
struct Run
{
	double centre, coverage;
};
std::vector< Run > runsAlong( const Floats& picture, int width, int y, double threshold = 0.02 )
{
	std::vector< Run > runs;
	double sum = 0.0, moment = 0.0;
	bool inside = false;
	for( int x = 0; x <= width; ++x )
	{
		const double a = x < width ? at( picture, width, x, y, 3 ) : 0.0;
		if( a > threshold )
		{
			inside = true;
			sum += a;
			moment += a * ( x + 0.5 );
		}
		else if( inside )
		{
			runs.push_back( { moment / sum, sum } );
			sum = moment = 0.0;
			inside       = false;
		}
	}
	return runs;
}

/// A clean bench for the Over on a grey card: the clip as the map, the ramp's
/// own contours and nothing else.
void benchSettings( Rig& rig, double lines )
{
	rig.Set( PT_WORLD, static_cast< float >( WorldIndexOf( Choice::Clip, 0, true ) ) );
	rig.Set( PT_LINES, ParamFromLines( lines ) );
	rig.Set( PT_INDEX_EVERY, 0.0f );
	rig.Set( PT_SMOOTH, 0.0f );
	rig.Set( PT_SWEEP, static_cast< float >( Sweep::Off ) );
	rig.Set( PT_GLOW, 0.0f );
	rig.Set( PT_WOBBLE, 0.0f );
	rig.Set( PT_CLIP_SHOW, 0.0f );
	rig.Set( PT_LINE_WIDTH, ParamFromLineWidth( 1.4 ) );
}

constexpr int kW = 1280, kH = 720;

/// --lines and --spacing: a ramp of brightness 0..1 left to right crosses
/// exactly Lines contours, each where ( k - 1/2 ) / Lines of the way along.
void checkLines( bool wrongDatum )
{
	Section( "lines: a ramp crosses Lines contours, each where the level says" );
	for( double lines : { 8.0, 20.0, 50.0 } )
	{
		const Floats ramp = greyCard( kW, kH, []( double x, double ) { return x / kW; } );
		Rig rig( true );
		if( !rig.Init( kW, kH, &ramp, true ) )
			return Check( false, "init" );
		benchSettings( rig, lines );
		rig.Render( 2 );
		const Floats picture = rig.Picture();
		//The control's own rounding: Lines is geometric in the slider.
		const double L        = LinesFromParam( ParamFromLines( lines ) );
		const auto runs       = runsAlong( picture, kW, kH / 2 );
		const int expected    = static_cast< int >( std::floor( L + 0.5 - 1e-9 ) );
		Check( static_cast< int >( runs.size() ) == expected,
		       fmt( "Lines %.0f: %zu contours across the ramp, expected %d", L, runs.size(), expected ) );
		double worst = 0.0;
		for( size_t k = 0; k < runs.size(); ++k )
		{
			const double datum = wrongDatum ? 0.0 : 0.5;
			const double x     = kW * ( ( k + 1.0 ) - datum ) / L;
			worst              = std::max( worst, std::fabs( runs[ k ].centre - x ) );
		}
		Check( worst < 0.05, fmt( "Lines %.0f: every contour within %.3f px of ( k - 1/2 ) W / Lines (bound 0.05)", L, worst ) );
	}
}

/// --width: a line is as wide on a cliff as on a plain. The ramp's slope
/// runs 1 to 8 across the frame; every contour's coverage across the row is
/// its width in pixels.
void checkWidth()
{
	Section( "width: contours the same width on slopes 8:1 apart" );
	for( double width : { 1.0, 1.4, 3.0, 6.0 } )
	{
		const Floats ramp = greyCard( kW, kH, []( double x, double ) {
			const double u = x / kW;
			return ( u + 3.5 * u * u ) / 4.5;
		} );
		Rig rig( true );
		if( !rig.Init( kW, kH, &ramp, true ) )
			return Check( false, "init" );
		benchSettings( rig, 20.0 );
		rig.Set( PT_LINE_WIDTH, ParamFromLineWidth( width ) );
		rig.Render( 2 );
		const auto runs = runsAlong( rig.Picture(), kW, kH / 2, 0.001 );
		const double w  = LineWidthFromParam( ParamFromLineWidth( width ) ) * kH / 1080.0;
		double lo = 1e9, hi = 0.0;
		for( const Run& run : runs )
		{
			lo = std::min( lo, run.coverage );
			hi = std::max( hi, run.coverage );
		}
		Check( !runs.empty() && std::fabs( lo - w ) < 0.03 * w + 0.02 && std::fabs( hi - w ) < 0.03 * w + 0.02,
		       fmt( "Line Width %.1f (%.3f px at 720): every contour %.3f..%.3f px wide over %zu lines", width, w, lo, hi,
		            runs.size() ) );
	}
}

/// --flat: a flat clip draws nothing, at any brightness -- black included,
/// which is where a contour would sit without the half-level offset.
void checkFlat( int flags )
{
	Section( "flat: a clip of one brightness draws no contour" );
	int drew = 0, tried = 0;
	for( double luma : { 0.0, 0.025, 0.1, 0.375, 0.5, 0.71, 1.0 } )
	{
		const Floats card = greyCard( 320, 180, [ luma ]( double, double ) { return luma; } );
		Rig rig( true );
		if( !rig.Init( 320, 180, &card, true ) )
			return Check( false, "init" );
		benchSettings( rig, 20.0 );
		rig.plugin.SetTestFlagsForTest( flags );
		rig.Render( 2 );
		const Floats picture = rig.Picture();
		double alpha         = 0.0;
		for( size_t i = 3; i < picture.size(); i += 4 )
			alpha = std::max( alpha, static_cast< double >( picture[ i ] ) );
		++tried;
		if( alpha > 0.01 )
			++drew;
	}
	Check( drew == 0, fmt( "%d of %d flat clips drew a contour", drew, tried ) );
}

/// --sweep: Manual puts the sweep's contour at Sweep Level of the range; on
/// the ramp that is a vertical line at Level x W.
void checkSweep()
{
	Section( "sweep: the sweep's contour is at its level" );
	for( double level : { 0.13, 0.37, 0.5, 0.81 } )
	{
		const Floats ramp = greyCard( kW, kH, []( double x, double ) { return x / kW; } );
		Rig rig( true );
		if( !rig.Init( kW, kH, &ramp, true ) )
			return Check( false, "init" );
		benchSettings( rig, 20.0 );
		rig.Set( PT_SWEEP, static_cast< float >( Sweep::Manual ) );
		rig.Set( PT_SWEEP_LEVEL, static_cast< float >( level ) );
		rig.Set( PT_FLOOR, 0.0f );
		rig.Set( PT_TRAIL, 0.0f );
		rig.Set( PT_SWEEP_GLOW, 0.0f );
		rig.Render( 2 );
		const auto runs = runsAlong( rig.Picture(), kW, kH / 2 );
		const double x  = level * kW;
		Check( runs.size() == 1 && std::fabs( runs[ 0 ].centre - x ) < 0.05,
		       fmt( "Level %.2f: %zu line(s), at %.3f px, expected %.3f", level, runs.size(), runs.empty() ? -1.0 : runs[ 0 ].centre, x ) );
	}
}

/// --trail: each contour's strength is ( 1 - age / Trail )^2 with the age
/// from SweepClock::AgeOf, the CPU's copy of the shader's ageOf -- in every
/// mode, both ways through a bounce.
void checkTrail( bool wrongModel )
{
	Section( "trail: every contour as strong as the CPU's age of it says" );
	struct Case
	{
		Sweep mode;
		double passes;
		const char* what;
	};
	const Case cases[] = { { Sweep::Rise, 0.4, "Rise at 0.4" },       { Sweep::Rise, 3.15, "Rise at 3.15" },
		                   { Sweep::Fall, 0.3, "Fall at 0.3 of a pass" }, { Sweep::Bounce, 0.6, "Bounce, climbing" },
		                   { Sweep::Bounce, 1.35, "Bounce, falling" },     { Sweep::Manual, 0.0, "Manual at 0.5" } };
	const double lines = 20.0, trail = 0.6;
	for( const Case& c : cases )
	{
		const Floats ramp = greyCard( kW, kH, []( double x, double ) { return x / kW; } );
		Rig rig( true );
		if( !rig.Init( kW, kH, &ramp, true ) )
			return Check( false, "init" );
		benchSettings( rig, lines );
		rig.Set( PT_LINE_WIDTH, ParamFromLineWidth( 3.0 ) );
		rig.Set( PT_SWEEP, static_cast< float >( c.mode ) );
		rig.Set( PT_SWEEP_LEVEL, 0.5f );
		rig.Set( PT_FLOOR, 0.0f );
		rig.Set( PT_TRAIL, static_cast< float >( trail ) );
		rig.Set( PT_SWEEP_GLOW, 0.0f );
		rig.Set( PT_SWEEP_TIME, ParamFromSweepSeconds( 1.0e3 ) );
		rig.plugin.SetSweepPassesForTest( c.passes );
		rig.Render( 1 );
		const FrameState& state = rig.plugin.LastFrame();
		const Floats picture    = rig.Picture();
		const double L          = LinesFromParam( ParamFromLines( lines ) );
		//The sweep's own line is drawn on top; read each contour at its
		//centre column, away from it.
		double worst = 0.0;
		int read     = 0;
		for( int k = 1; k <= static_cast< int >( L ); ++k )
		{
			const double height = ( k - 0.5 ) / L;
			const double x      = height * kW;
			if( std::fabs( height - state.sweepPosition ) * kW < 12.0 )
				continue;
			double peak = 0.0;
			for( int dx = -3; dx <= 3; ++dx )
				peak = std::max( peak, static_cast< double >( at( picture, kW, static_cast< int >( x ) + dx, kH / 2, 3 ) ) );
			const Sweep model = wrongModel && c.mode == Sweep::Bounce ? Sweep::Rise : c.mode;
			const double age  = SweepClock::AgeOf( model, state.sweepPosition, state.sweepDirection, height );
			double expect     = std::clamp( 1.0 - age / trail, 0.0, 1.0 );
			expect *= expect;
			worst = std::max( worst, std::fabs( peak - expect ) );
			++read;
		}
		Check( read > 10 && worst < 0.01, fmt( "%s (sweep at %.3f): %d contours, worst %.4f from ( 1 - age / Trail )^2", c.what,
		                                     state.sweepPosition, read, worst ) );
	}
}

//---------------------------------------------------------------------------
// Real worlds: the data, and the projections onto it.
//---------------------------------------------------------------------------

/// The CPU's copy of the shader's B-spline, on a decoded heightfield at level
/// 0: the height the GPU reads at a texture coordinate.
double bsplineHeight( const Heightfield& field, double u, double v, bool wrapX )
{
	const double sx = u * field.width - 0.5, sy = v * field.height - 0.5;
	const int ix = static_cast< int >( std::floor( sx ) ), iy = static_cast< int >( std::floor( sy ) );
	const double fx = sx - ix, fy = sy - iy;
	auto weights = []( double f, double w[ 4 ] ) {
		w[ 0 ] = ( 1 - f ) * ( 1 - f ) * ( 1 - f ) / 6.0;
		w[ 1 ] = ( 4 - 6 * f * f + 3 * f * f * f ) / 6.0;
		w[ 2 ] = ( 1 + 3 * f + 3 * f * f - 3 * f * f * f ) / 6.0;
		w[ 3 ] = f * f * f / 6.0;
	};
	double wx[ 4 ], wy[ 4 ];
	weights( fx, wx );
	weights( fy, wy );
	double h = 0.0;
	for( int j = 0; j < 4; ++j )
		for( int i = 0; i < 4; ++i )
		{
			int x = ix - 1 + i, y = std::clamp( iy - 1 + j, 0, field.height - 1 );
			x     = wrapX ? ( ( x % field.width ) + field.width ) % field.width : std::clamp( x, 0, field.width - 1 );
			h += wx[ i ] * wy[ j ] * field.texels[ ( static_cast< size_t >( y ) * field.width + x ) * 2 ] / 65535.0;
		}
	return h;
}

double heightAtLatLon( const Heightfield& field, double lat, double lon )
{
	return bsplineHeight( field, ( lon + 180.0 ) / 360.0, ( 90.0 - lat ) / 180.0, true );
}

int globeIndex( const char* name )
{
	for( size_t i = 0; i < Globes().size(); ++i )
		if( std::strcmp( Globes()[ i ].name, name ) == 0 )
			return static_cast< int >( i );
	return -1;
}

struct Feature
{
	const char* world;
	const char* name;
	double lat, lon;
	double lowMetres, highMetres;///< where its height must be
};

const Feature kFeatures[] = {
	{ "Earth", "Everest", 27.988, 86.925, 4000.0, 9000.0 },
	{ "Earth", "the Mariana Trench", 11.35, 142.2, -11500.0, -6000.0 },
	{ "Earth", "the mid-Atlantic", 0.0, -30.0, -6000.0, -2500.0 },
	{ "Earth", "the Sahara", 23.0, 13.0, 0.0, 2500.0 },
	{ "Earth", "the Pacific", 0.0, -150.0, -6500.0, -3000.0 },
	{ "Earth", "Australia", -25.0, 134.0, 0.0, 1500.0 },
	{ "Mars", "Olympus Mons", 18.65, -133.8, 15000.0, 23000.0 },
	{ "Mars", "Hellas", -42.4, 70.5, -8500.0, -5000.0 },
	{ "Moon", "the far-side highlands", 5.4, -158.6, 5000.0, 11000.0 },
	{ "Moon", "South Pole-Aitken", -53.0, -169.0, -9500.0, -2000.0 },
	{ "Venus", "Maxwell Montes", 65.2, 3.3, 5000.0, 12000.0 },
};

/// --geo: the data has each feature where it is (the PNG decoded on the CPU),
/// and the projections put it where they say: centred on it in a globe and
/// in a map, and at its own pixel of the whole map centred on (0, 0). A probe
/// floods everything below a level a hair either side of the height the CPU
/// reads there, and asks whether the probe pixel flooded -- by comparing it
/// with Flood off, so the sweep's own contour passing through cannot fool it.
void checkGeo( int flags )
{
	Section( "geo: the features are where the worlds keep them" );
	for( const char* worldName : { "Earth", "Mars", "Moon", "Venus" } )
	{
		const int g = globeIndex( worldName );
		if( g < 0 )
		{
			Check( false, fmt( "no world called %s", worldName ) );
			continue;
		}
		const WorldInfo& info   = Globes()[ static_cast< size_t >( g ) ];
		const Heightfield field = LoadHeightfield( info );
		if( !field.error.empty() )
		{
			Check( false, field.error );
			continue;
		}
		//Centred probes at zoom 16, where a pixel is a small part of a
		//texel; the whole map at one texel of the widest world per pixel,
		//so the GPU reads level 0 there too.
		Rig close( false ), whole( false );
		if( !close.Init( 640, 360 ) || !whole.Init( 4096, 2048 ) )
			return Check( false, "init" );
		for( Rig* rig : { &close, &whole } )
		{
			rig->plugin.SetTestFlagsForTest( flags );
			rig->Set( PT_WORLD, static_cast< float >( WorldIndexOf( Choice::Globe, g, false ) ) );
			rig->Set( PT_SPIN, ParamFromSpin( 0.0 ) );
			rig->Set( PT_SMOOTH, 0.0f );
			rig->Set( PT_SWEEP, static_cast< float >( Sweep::Manual ) );
			rig->Set( PT_FLOOR, 0.0f );
			rig->Set( PT_TRAIL, 0.0f );
			rig->Set( PT_SWEEP_GLOW, 0.0f );
			rig->Set( PT_SEA_FILL, 0.0f );
			rig->Set( PT_LINE_WIDTH, 0.0f );
			rig->Set( PT_GLOW, 0.0f );
		}
		auto flooded = []( Rig& rig, double level, int px, int py ) {
			rig.Set( PT_SWEEP_LEVEL, static_cast< float >( std::clamp( level, 0.0, 1.0 ) ) );
			float red[ 2 ];
			for( int flood = 0; flood < 2; ++flood )
			{
				rig.Set( PT_FLOOD, static_cast< float >( flood ) );
				rig.Render( 1 );
				red[ flood ] = rig.PicturePixel( px, py )[ 0 ];
			}
			return std::fabs( red[ 1 ] - red[ 0 ] ) > 0.02f;
		};
		for( const Feature& f : kFeatures )
		{
			if( std::strcmp( f.world, worldName ) != 0 )
				continue;
			const double h      = heightAtLatLon( field, f.lat, f.lon );
			const double metres = info.minMetres + h * ( info.maxMetres - info.minMetres );
			Check( metres >= f.lowMetres && metres <= f.highMetres,
			       fmt( "%s: %s at %.0f m (expected %.0f..%.0f)", f.world, f.name, metres, f.lowMetres, f.highMetres ) );
			for( View view : { View::Globe, View::Map } )
			{
				close.Set( PT_VIEW, static_cast< float >( view ) );
				close.Set( PT_LONGITUDE, ParamFromLongitude( f.lon ) );
				close.Set( PT_LATITUDE, ParamFromLatitude( f.lat ) );
				close.Set( PT_ZOOM, ParamFromZoom( 16.0 ) );
				const bool dry = !flooded( close, h - 0.01, 320, 180 ), wet = flooded( close, h + 0.01, 320, 180 );
				Check( dry && wet, fmt( "%s: %s, %s centred on it: dry 1%% below its height, flooded 1%% above (%s/%s)", f.world,
				                        f.name, ViewName( view ), dry ? "dry" : "wet", wet ? "wet" : "dry" ) );
			}
			whole.Set( PT_VIEW, static_cast< float >( View::Map ) );
			whole.Set( PT_LONGITUDE, ParamFromLongitude( 0.0 ) );
			whole.Set( PT_LATITUDE, ParamFromLatitude( 0.0 ) );
			whole.Set( PT_ZOOM, ParamFromZoom( 1.0 ) );
			const double H = whole.height, W = whole.width;
			const int px = static_cast< int >( std::floor( W / 2.0 + f.lon / 90.0 * H / 2.0 ) );
			const int py = static_cast< int >( std::floor( H / 2.0 + f.lat / 90.0 * H / 2.0 ) );
			const double plon = ( px + 0.5 - W / 2.0 ) / ( H / 2.0 ) * 90.0;
			const double plat = ( py + 0.5 - H / 2.0 ) / ( H / 2.0 ) * 90.0;
			const double ph   = heightAtLatLon( field, plat, plon );
			//On a steep pixel the sweep's own contour, a level a hair away, is
			//drawn over the probe and hides the flood under it: the levels
			//stand three pixels' worth of the local slope off instead.
			const double step  = 90.0 / ( H / 2.0 );
			const double slope = std::max( std::fabs( heightAtLatLon( field, plat, plon + step ) - heightAtLatLon( field, plat, plon - step ) ),
			                               std::fabs( heightAtLatLon( field, plat + step, plon ) - heightAtLatLon( field, plat - step, plon ) ) )
			                     * 0.5;
			const double off = 0.002 + 3.0 * slope;
			const bool dry = !flooded( whole, ph - off, px, py ), wet = flooded( whole, ph + off, px, py );
			Check( dry && wet, fmt( "%s: %s at its own pixel (%d, %d) of the whole map, +/-%.4f (%s/%s)", f.world, f.name, px, py,
			                        off, dry ? "dry" : "wet", wet ? "wet" : "dry" ) );
		}
	}
}

/// --globe: the disc is 0.92 Zoom of the half-height, its limb antialiased.
void checkGlobe()
{
	Section( "globe: the disc's radius and its limb" );
	const int W = 1280, H = 720;
	Rig rig( false );
	if( !rig.Init( W, H ) )
		return Check( false, "init" );
	rig.Set( PT_VIEW, static_cast< float >( View::Globe ) );
	rig.Set( PT_ZOOM, ParamFromZoom( 1.0 ) );
	rig.Render( 1 );
	const Floats picture = rig.Picture();
	double covered       = 0.0;
	for( int x = 0; x < W; ++x )
		covered += at( picture, W, x, H / 2, 3 );
	const double expected = 2.0 * 0.92 * H / 2.0;
	Check( std::fabs( covered - expected ) < 0.6, fmt( "the middle row covers %.3f px of disc, expected %.3f", covered, expected ) );
	int partial = 0;
	for( int x = 0; x < W; ++x )
	{
		const float a = at( picture, W, x, H / 2, 3 );
		if( a > 0.02f && a < 0.98f )
			++partial;
	}
	Check( partial >= 2 && partial <= 4, fmt( "%d partly covered pixels across the two limbs (antialiased, 2..4)", partial ) );
}

/// --alpha: the source on no ground is transparent off the world and opaque
/// on it (a globe's disc), and premultiplied throughout.
void checkAlpha()
{
	Section( "alpha: Background 0 leaves the world on transparency" );
	const int W = 640, H = 360;
	Rig rig( false );
	if( !rig.Init( W, H ) )
		return Check( false, "init" );
	rig.Set( PT_BACKGROUND, 0.0f );
	rig.Set( PT_VIEW, static_cast< float >( View::Globe ) );
	rig.Render( 2 );
	const Floats out = rig.Output();
	const float corner = at( out, W, 2, 2, 3 ), middle = at( out, W, W / 2, H / 2, 3 );
	Check( corner == 0.0f, fmt( "a corner off the globe: alpha %.4f", corner ) );
	Check( middle > 0.99f, fmt( "the globe's centre: alpha %.4f", middle ) );
	int unpremultiplied = 0;
	for( size_t i = 0; i < out.size(); i += 4 )
		if( out[ i ] > out[ i + 3 ] + 1e-4f || out[ i + 1 ] > out[ i + 3 ] + 1e-4f || out[ i + 2 ] > out[ i + 3 ] + 1e-4f )
			++unpremultiplied;
	Check( unpremultiplied == 0, fmt( "%d pixels brighter than their alpha", unpremultiplied ) );
}

/// --state: what the plugin changes, it puts back.
void checkState()
{
	Section( "state: the host's GL state comes back as it went in" );
	Rig rig( true );
	if( !rig.Init( 640, 360 ) )
		return Check( false, "init" );
	rig.Set( PT_GLOW, 0.8f );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glActiveTexture( GL_TEXTURE0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 8 );
	rig.Render( 1 );
	GLint src = 0, dst = 0, active = 0, unpack = 0, program = 0, viewport[ 4 ];
	glGetIntegerv( GL_BLEND_SRC_RGB, &src );
	glGetIntegerv( GL_BLEND_DST_RGB, &dst );
	glGetIntegerv( GL_ACTIVE_TEXTURE, &active );
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &unpack );
	glGetIntegerv( GL_CURRENT_PROGRAM, &program );
	glGetIntegerv( GL_VIEWPORT, viewport );
	Check( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blending as the host left it" );
	Check( active == GL_TEXTURE0, "texture unit 0 active" );
	Check( unpack == 8, "the host's unpack alignment" );
	Check( program == 0, "no program left bound" );
	Check( viewport[ 2 ] == 640 && viewport[ 3 ] == 360, "the host's viewport" );
	GLint bound = -1;
	for( int unit = 0; unit < 4; ++unit )
	{
		glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + unit ) );
		GLint t = 0;
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &t );
		bound = std::max( bound, t );
	}
	glActiveTexture( GL_TEXTURE0 );
	Check( bound == 0, "no texture left on units 0-3" );
	glDisable( GL_BLEND );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	const GLenum error = glGetError();
	Check( error == GL_NO_ERROR, fmt( "no GL error (0x%04x)", error ) );
}

/// --resize: the host's raster changes under a running instance.
void checkResize()
{
	Section( "resize: a new raster under a running instance" );
	Rig rig( true );
	if( !rig.Init( 640, 360 ) )
		return Check( false, "init" );
	bool ok = rig.Render( 2 );
	for( auto size : { std::make_pair( 1920, 1080 ), std::make_pair( 333, 777 ), std::make_pair( 640, 360 ) } )
	{
		ok = ok && rig.Resize( size.first, size.second ) && rig.Render( 2 );
		const auto runs = runsAlong( rig.Picture(), rig.width, rig.height / 2, 0.02 );
		Check( ok && !runs.empty(), fmt( "%dx%d: rendered, %zu contours on the middle row", size.first, size.second, runs.size() ) );
	}
}

double lightSum( Rig& rig )
{
	const Floats light = rig.Light();
	double sum         = 0.0;
	for( size_t i = 0; i < light.size(); i += 4 )
		sum += light[ i ] + light[ i + 1 ] + light[ i + 2 ];
	return sum / ( rig.width * rig.height );
}

/// --worlds: every world and every place loads, matches its table, and
/// draws contours at the defaults.
void checkWorlds()
{
	Section( "worlds: every one loads and draws" );
	const int W = 480, H = 270;
	auto one = [ & ]( Choice choice, int index, const WorldInfo& info ) {
		Rig rig( false );
		if( !rig.Init( W, H ) )
			return Check( false, "init" );
		rig.Set( PT_WORLD, static_cast< float >( WorldIndexOf( choice, index, false ) ) );
		if( choice == Choice::Place )
			rig.Set( PT_PLACE, static_cast< float >( index ) );
		rig.Set( PT_SWEEP, static_cast< float >( Sweep::Off ) );
		rig.Render( 1 );
		const double light = lightSum( rig );
		Check( rig.plugin.WorldError().empty() && rig.plugin.LastFrame().drewWorld && light > 0.004,
		       fmt( "%-20s %4dx%-4d %8.0f..%-6.0f m  lines' light %.4f %s", info.name, info.width, info.height, info.minMetres,
		            info.maxMetres, light, rig.plugin.WorldError().c_str() ) );
	};
	for( size_t g = 0; g < Globes().size(); ++g )
		one( Choice::Globe, static_cast< int >( g ), Globes()[ g ] );
	for( size_t p = 0; p < Places().size(); ++p )
		one( Choice::Place, static_cast< int >( p ), Places()[ p ] );
}

/// --generated: each terrain draws, a seed is a different world, Morph moves
/// it and no Morph holds it still.
void checkGenerated()
{
	Section( "generated: three terrains, seeds, morph" );
	const int W = 480, H = 270;
	auto render = [ & ]( int terrain, int seed, float morph, int frames, Floats* first ) {
		Rig rig( false );
		rig.Init( W, H );
		rig.Set( PT_WORLD, static_cast< float >( WorldIndexOf( Choice::Generated, 0, false ) ) );
		rig.Set( PT_TERRAIN, static_cast< float >( terrain ) );
		rig.Set( PT_SEED, static_cast< float >( seed ) );
		rig.Set( PT_MORPH, morph );
		rig.Set( PT_SPIN, ParamFromSpin( 0.0 ) );
		rig.Set( PT_SWEEP, static_cast< float >( Sweep::Off ) );
		rig.Render( 1 );
		if( first )
			*first = rig.Picture();
		rig.Render( frames );
		return std::make_pair( lightSum( rig ), rig.Picture() );
	};
	auto difference = []( const Floats& a, const Floats& b ) {
		double d = 0.0;
		for( size_t i = 0; i < a.size(); ++i )
			d += std::fabs( a[ i ] - b[ i ] );
		return d / a.size();
	};
	for( int terrain = 0; terrain < static_cast< int >( Terrain::Count ); ++terrain )
	{
		Floats first;
		const auto still = render( terrain, 7, 0.0f, 30, &first );
		Check( still.first > 0.004, fmt( "%s: lines' light %.4f", TerrainName( static_cast< Terrain >( terrain ) ), still.first ) );
		Check( difference( first, still.second ) == 0.0, fmt( "%s, Morph 0: still after 30 frames", TerrainName( static_cast< Terrain >( terrain ) ) ) );
		const auto other = render( terrain, 8, 0.0f, 0, nullptr );
		Check( difference( still.second, other.second ) > 0.005,
		       fmt( "%s: seed 8 is not seed 7 (%.4f)", TerrainName( static_cast< Terrain >( terrain ) ), difference( still.second, other.second ) ) );
		Floats morphFirst;
		const auto moving = render( terrain, 7, 1.0f, 30, &morphFirst );
		Check( difference( morphFirst, moving.second ) > 0.001,
		       fmt( "%s, Morph 1: moved in 30 frames (%.4f)", TerrainName( static_cast< Terrain >( terrain ) ), difference( morphFirst, moving.second ) ) );
	}
}

/// --clock: a pass takes Sweep Time; Restart starts one; Bars lock to the host.
void checkClock()
{
	Section( "clock: Sweep Time, Restart, and Sweep Bars on the host's bars" );
	{
		Rig rig( false );
		rig.Init( 160, 90 );
		rig.Set( PT_SWEEP_TIME, ParamFromSweepSeconds( 8.0 ) );
		rig.Render( 121 );
		const double expect = 2.0 / SweepSecondsFromParam( ParamFromSweepSeconds( 8.0 ) );
		Check( std::fabs( rig.plugin.LastFrame().sweepPosition - expect ) < 1e-9,
		       fmt( "Rise, 8 s a pass, 2 s in: at %.9f, expected %.9f", rig.plugin.LastFrame().sweepPosition, expect ) );
		rig.Set( PT_RESTART, 1.0f );
		rig.Render( 1 );
		Check( rig.plugin.LastFrame().sweepPosition < 1e-9, fmt( "Restart: at %.9f", rig.plugin.LastFrame().sweepPosition ) );
	}
	for( int choice : { 1, 3 } )
	{
		Rig rig( false );
		rig.Init( 160, 90 );
		rig.Set( PT_SWEEP_BARS, static_cast< float >( choice ) );
		const int bars = BarsOf( choice );
		double worst   = 0.0;
		//120 BPM: a bar every two seconds, the host's phase read off a clock
		//of its own that started a third of a bar in.
		for( int f = 0; f < 600; ++f )
		{
			const double t     = f / 60.0;
			const double count = t / 2.0 + 1.0 / 3.0;
			rig.plugin.SetBeatInfo( 120.0f, static_cast< float >( count - std::floor( count ) ) );
			rig.Render( 1 );
			if( f < 2 )
				continue;
			//The plugin counts the host's bars from the first phase it saw,
			//so its count is the host's own here, 1/3 of a bar in.
			const double expect = std::fmod( count / bars, 1.0 );
			const double got = rig.plugin.LastFrame().sweepPosition;
			double d    = std::fabs( got - expect );
			d           = std::min( d, 1.0 - d );
			worst       = std::max( worst, d );
		}
		Check( worst < 1e-5, fmt( "%s: the pass follows the host's bars, worst %.2e of a pass", BarsName( choice ), worst ) );
	}
}


/// --async: the plugin's own way of loading. The first world is up on the
/// first frame; after that a world decodes off the GL thread, and a change of
/// World keeps the old one on screen, every frame, until the new one is up.
void checkAsync()
{
	Section( "async: a new world decodes in the background, the old one stays up" );
	Rig rig( false );
	if( !rig.Init( 320, 180 ) )
		return Check( false, "init" );
	rig.plugin.SetSynchronousLoadForTest( false );
	auto until = [ & ]( const std::function< bool() >& done, int& frames, bool& blackFrame ) {
		blackFrame = false;
		for( frames = 0; frames < 600 && !done(); ++frames )
		{
			usleep( 10000 );
			rig.Render( 1 );
			if( frames > 0 && !rig.plugin.LastFrame().drewWorld )
				blackFrame = true;
		}
		return done();
	};
	int frames = 0;
	bool black = false;
	rig.Render( 1 );
	Check( rig.plugin.LastFrame().drewWorld, "the first world is drawn on the very first frame" );
	rig.Set( PT_WORLD, static_cast< float >( WorldIndexOf( Choice::Globe, globeIndex( "Earth" ), false ) ) );
	const bool earth = until( [ & ] { return rig.plugin.LastFrame().heightWidth == 4096; }, frames, black );
	Check( earth && !black, fmt( "Earth is up after %d frames, and every frame in between drew the Moon (%s)", frames,
	                             black ? "a frame drew nothing" : "none blank" ) );
	rig.Set( PT_WORLD, static_cast< float >( WorldIndexOf( Choice::Place, 0, false ) ) );
	rig.Set( PT_PLACE, 3.0f );
	const bool place = until( [ & ] { return rig.plugin.LastFrame().kind == Kind::Place; }, frames, black );
	Check( place && !black && rig.plugin.WorldError().empty(), fmt( "a place is up after %d frames, none blank", frames ) );
}

/// --controls: no dead control. Each one, in a context where it should
/// matter, changes the output (colour or alpha) over 30 frames. The only
/// check that catches a uniform whose name does not match the C++.
void checkControls()
{
	Section( "controls: every one changes the picture where it should" );
	struct Probe
	{
		unsigned int id;
		bool over;
		std::vector< std::string > context;
		float a, b;
	};
	const std::string generated = "World=Generated", place = "World=Place", earth = "World=Earth";
	//The sweep half way up, where the Moon's near side has contours to show it.
	const std::vector< std::string > midway = { "Sweep=Manual", "Sweep Level=0.5" };
	const std::vector< Probe > probes = {
		{ PT_WORLD, false, {}, -1.0f, -2.0f },
		{ PT_PLACE, false, { place }, 0.0f, 1.0f },
		{ PT_VIEW, false, {}, 0.0f, 1.0f },
		{ PT_LONGITUDE, false, { "Spin=0.5" }, 0.5f, 0.7f },
		{ PT_LATITUDE, false, { "Spin=0.5" }, 0.5f, 0.7f },
		{ PT_ZOOM, false, {}, 0.0f, 0.3f },
		{ PT_SPIN, false, {}, 0.5f, 0.9f },
		{ PT_ROTATION, false, { "View=Map" }, 0.5f, 0.6f },
		{ PT_LINES, false, {}, 0.6f, 0.4f },
		{ PT_INDEX_EVERY, false, {}, 5.0f, 0.0f },
		{ PT_LINE_WIDTH, false, {}, 0.3f, 0.6f },
		{ PT_SMOOTH, false, {}, 0.25f, 0.6f },
		{ PT_SEA_FILL, false, { earth }, 0.5f, 1.0f },
		{ PT_DEPTHS, false, { earth }, 0.5f, 1.0f },
		{ PT_SWEEP, false, {}, 1.0f, 0.0f },
		{ PT_SWEEP_TIME, false, {}, 0.4f, 0.2f },
		{ PT_SWEEP_BARS, false, {}, 0.0f, 1.0f },
		{ PT_SWEEP_LEVEL, false, { "Sweep=Manual" }, 0.3f, 0.6f },
		{ PT_SWEEP_GLOW, false, midway, 0.6f, 0.0f },
		{ PT_TRAIL, false, midway, 0.35f, 0.9f },
		{ PT_FLOOR, false, {}, 0.2f, 0.8f },
		{ PT_FLOOD, false, midway, 0.0f, 1.0f },
		{ PT_FLOW, false, {}, 0.5f, 0.9f },
		{ PT_RESTART, false, { "Sweep Time=0.2" }, 0.0f, 1.0f },
		{ PT_STYLE, false, {}, 0.0f, 1.0f },
		{ PT_HUE, false, midway, 0.07f, 0.5f },
		{ PT_HEIGHT_COLOUR, false, {}, 0.0f, 1.0f },
		{ PT_GLOW, false, {}, 0.35f, 0.0f },
		{ PT_SHADE, false, {}, 0.0f, 1.0f },
		{ PT_TINTS, false, {}, 0.0f, 1.0f },
		{ PT_WOBBLE, false, {}, 0.0f, 1.0f },
		{ PT_BACKGROUND, false, {}, 1.0f, 0.0f },
		{ PT_TERRAIN, false, { generated }, 0.0f, 1.0f },
		{ PT_SEED, false, { generated }, 7.0f, 8.0f },
		{ PT_FEATURE_SIZE, false, { generated }, 0.5f, 0.8f },
		{ PT_ROUGHNESS, false, { generated }, 0.5f, 0.9f },
		{ PT_GENERATED_SEA, false, { generated }, 0.5f, 0.3f },
		{ PT_MORPH, false, { generated }, 0.0f, 1.0f },
		{ PT_CLIP_SHOW, true, {}, 0.6f, 0.0f },
		{ PT_INVERT, true, {}, 0.0f, 1.0f },
		{ PT_MIX, true, {}, 1.0f, 0.5f },
	};
	std::vector< bool > covered( PT_ABOUT_TEXT, false );
	const int W = 320, H = 180;
	for( const Probe& probe : probes )
	{
		Floats out[ 2 ];
		for( int side = 0; side < 2; ++side )
		{
			Rig rig( probe.over );
			rig.Init( W, H );
			applyAll( rig.plugin, probe.context );
			float value = side == 0 ? probe.a : probe.b;
			//World: a and b < 0 name the defaults' neighbours.
			if( probe.id == PT_WORLD )
				value = static_cast< float >( WorldIndexOf( Choice::Globe, side == 0 ? globeIndex( "Moon" ) : globeIndex( "Mars" ), false ) );
			if( probe.id == PT_RESTART )
			{
				rig.Render( 45 );
				rig.Set( PT_RESTART, value );
				rig.Render( 15 );
			}
			else
			{
				rig.Set( probe.id, value );
				rig.Render( 30 );
			}
			out[ side ] = rig.Output();
		}
		double d = 0.0;
		for( size_t i = 0; i < out[ 0 ].size(); ++i )
			d += std::fabs( out[ 0 ][ i ] - out[ 1 ][ i ] );
		d /= out[ 0 ].size();
		covered[ probe.id ] = true;
		Check( d > 1.0e-4, fmt( "%-14s %s: %.5f", NameOf( probe.id ), probe.over ? "(Over)" : "      ", d ) );
	}
	int missing = 0;
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		if( !covered[ id ] )
		{
			++missing;
			std::printf( "  no probe for %s\n", NameOf( id ) );
		}
	Check( missing == 0, fmt( "every control has a probe (%d without)", missing ) );
}

/// --bench: what a frame costs on this GPU, at 1080p and 4K.
void runBench()
{
	Section( "bench: milliseconds a frame, GPU finished" );
	struct Case
	{
		const char* what;
		bool over;
		std::vector< std::string > settings;
	};
	const Case cases[] = {
		{ "Moon globe (defaults)", false, {} },
		{ "Earth map, glow 1", false, { "World=Earth", "View=Map", "Glow=1" } },
		{ "Generated, morphing", false, { "World=Generated", "Morph=1" } },
		{ "Generated craters, morphing", false, { "World=Generated", "Terrain=Craters", "Morph=1" } },
		{ "Over, the clip as the map", true, {} },
	};
	for( const Case& c : cases )
		for( auto size : { std::make_pair( 1920, 1080 ), std::make_pair( 3840, 2160 ) } )
		{
			Rig rig( c.over );
			rig.Init( size.first, size.second );
			applyAll( rig.plugin, c.settings );
			rig.Render( 5 );
			glFinish();
			const int frames = 30;
			const auto start = std::chrono::steady_clock::now();
			rig.Render( frames );
			glFinish();
			const double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() / frames;
			std::printf( "  %-30s %4dx%-4d  %6.2f ms\n", c.what, size.first, size.second, ms );
		}
}

//---------------------------------------------------------------------------
// No GL.
//---------------------------------------------------------------------------
void checkOffline()
{
	Section( "offline: the sweep's arithmetic, the controls, the table" );
	{
		//Bounce: the age is continuous through both turns.
		double worst = 0.0;
		for( double at : { 0.1, 0.5, 0.9 } )
		{
			worst = std::max( worst, std::fabs( SweepClock::AgeOf( Sweep::Bounce, 1.0 - 1e-9, 1, at )
			                                    - SweepClock::AgeOf( Sweep::Bounce, 1.0 - 1e-9, -1, at ) ) );
			worst = std::max( worst, std::fabs( SweepClock::AgeOf( Sweep::Bounce, 1e-9, -1, at ) - SweepClock::AgeOf( Sweep::Bounce, 1e-9, 1, at ) ) );
		}
		Check( worst < 1e-6, fmt( "Bounce: a line's age carries straight through the turns (%.2e)", worst ) );
		Check( SweepClock::AgeOf( Sweep::Rise, 0.3, 1, 0.3 ) == 0.0 && std::fabs( SweepClock::AgeOf( Sweep::Rise, 0.3, 1, 0.31 ) - 0.99 ) < 1e-12,
		       "Rise: zero at the sweep, nearly a pass just above it" );
	}
	{
		SweepClock clock;
		SweepInputs in;
		in.mode    = Sweep::Bounce;
		in.seconds = 4.0;
		for( int i = 0; i < 300; ++i )
			clock.Advance( 1.0 / 60.0, in );
		Check( std::fabs( clock.Passes() - 1.25 ) < 1e-9 && clock.Direction() == -1 && std::fabs( clock.Position() - 0.75 ) < 1e-9,
		       fmt( "Bounce, 4 s a pass, 5 s in: passes %.6f, at %.6f, going %+d", clock.Passes(), clock.Position(), clock.Direction() ) );
	}
	{
		double worst = 0.0;
		for( int i = 0; i <= 20; ++i )
		{
			const float v = i / 20.0f;
			const float back[] = { ParamFromLines( LinesFromParam( v ) ),   ParamFromZoom( ZoomFromParam( v ) ),
				                   ParamFromSpin( SpinFromParam( v ) ),     ParamFromFlow( FlowFromParam( v ) ),
				                   ParamFromSweepSeconds( SweepSecondsFromParam( v ) ), ParamFromLineWidth( LineWidthFromParam( v ) ) };
			for( float b : back )
				worst = std::max( worst, std::fabs( static_cast< double >( b ) - v ) );
		}
		Check( worst < 1e-5, fmt( "every mapping inverts (%.2e)", worst ) );
		Check( SpinFromParam( 0.5f ) == 0.0 && FlowFromParam( 0.5f ) == 0.0, "Spin and Flow stand still at the middle" );
	}
	{
		std::vector< float > values = { 1, 2, 0, 0, 0, 3, 4, 0, 0 };
		std::vector< uint8_t > measured = { 1, 1, 0, 0, 0, 1, 1, 0, 0 };
		FillGaps( 3, 3, values, measured );
		bool inside = true;
		for( float v : values )
			inside = inside && v >= 1.0f && v <= 4.0f;
		Check( inside && values[ 0 ] == 1 && values[ 5 ] == 3, "FillGaps fills holes from the measured values, and keeps them" );
	}
	{
		int bad = 0;
		std::string first;
		auto look = [ & ]( const WorldInfo& w ) {
			const Heightfield field = LoadHeightfield( w );
			const bool ok = field.error.empty() && w.maxMetres > w.minMetres && ( w.kind != Kind::Place || w.metresPerPixel > 0.0 );
			if( !ok )
			{
				++bad;
				if( first.empty() )
					first = std::string( w.name ) + ": " + field.error;
			}
		};
		for( const WorldInfo& w : Globes() )
			look( w );
		for( const WorldInfo& w : Places() )
			look( w );
		Check( bad == 0, fmt( "%zu globes and %zu places decode at their table's size (%d bad%s%s)", Globes().size(), Places().size(), bad,
		                      first.empty() ? "" : ": ", first.c_str() ) );
		int longest = 0;
		for( int i = 0; i < WorldChoiceCount( true ); ++i )
			longest = std::max( longest, static_cast< int >( std::strlen( WorldChoiceName( i, true ) ) ) );
		for( const WorldInfo& w : Places() )
			longest = std::max( longest, static_cast< int >( std::strlen( w.name ) ) );
		Check( longest <= 20, fmt( "every dropdown name is 20 characters or fewer (%d)", longest ) );
	}
}

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock.
//===========================================================================
int runPipe( bool effect, int width, int height, double fps, const std::string& scriptPath, int count, bool readStdin,
             const std::vector< std::string >& settings, const std::vector< Timed >& timed )
{
	Rig rig( effect );
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	if( !applyAll( rig.plugin, settings ) )
		return 2;
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		if( !bindScript( rig.plugin, scriptPath, automation, error ) )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}
	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	Bytes out( in.size() );
	for( int index = 0; readStdin || count < 0 || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			if( filled < in.size() )
				break;
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			glBindTexture( GL_TEXTURE_2D, rig.sourceTexture );
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}
		for( const auto& entry : automation )
			rig.plugin.SetFloatParameter( entry.first, valueAt( entry.second.track, index, entry.second.ramp ) );
		for( const Timed& t : timed )
			if( t.frame == index )
			{
				std::string error;
				applySetting( rig.plugin, t.setting, error );
			}
		if( !rig.Render( 1 ) )
			return 1;
		const Floats frame = rig.Output();
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width; ++x )
				for( int c = 0; c < 4; ++c )
				{
					const float v = c == 3 ? 1.0f : frame[ ( static_cast< size_t >( height - 1 - y ) * width + x ) * 4 + c ];
					out[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ] = static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
				}
		if( std::fwrite( out.data(), 1, out.size(), stdout ) != out.size() )
			return 1;
	}
	return 0;
}

void usage()
{
	std::fprintf( stderr, "usage: ihtest [--over] [--size WxH] [--set Name=Value]... [--at N Name=Value]... [--frames N]\n"
	                      "              [--out file.png | --film N | --pipe | --list] [--script cues] [--fps F]\n"
	                      "       ihtest --offline | --all | --negative | --lines --width --flat --sweep --trail --geo\n"
	                      "              --globe --alpha --state --resize --worlds --generated --clock --controls --async --bench\n" );
}
} // namespace

int main( int argc, char** argv )
{
	if( std::getenv( "ISOHYPSE_DATA_DIR" ) == nullptr )
		setenv( "ISOHYPSE_DATA_DIR", ISOHYPSE_SOURCE_DATA, 1 );

	//A reader that hangs up ends the run with exit 1, not SIGPIPE's 141.
	std::signal( SIGPIPE, SIG_IGN );
	bool over = false, list = false, pipe = false, offline = false, all = false, negative = false, framesGiven = false;
	int width = 1280, height = 720, frames = 60, film = 0;
	double fps = 60.0;
	std::string outPath, scriptPath;
	std::vector< std::string > settings, checks;
	std::vector< Timed > timed;
	const char* const kChecks[] = { "--lines", "--width", "--flat",  "--sweep",  "--trail",     "--geo",   "--globe",   "--alpha",
		                            "--state", "--resize", "--worlds", "--generated", "--clock", "--controls", "--async", "--bench" };
	for( int i = 1; i < argc; ++i )
	{
		const std::string a = argv[ i ];
		auto next           = [ & ]() -> std::string {
            if( i + 1 >= argc )
            {
                usage();
                std::exit( 2 );
            }
            return argv[ ++i ];
		};
		if( a == "--over" )
			over = true;
		else if( a == "--list" )
			list = true;
		else if( a == "--pipe" )
			pipe = true;
		else if( a == "--offline" )
			offline = true;
		else if( a == "--all" )
			all = true;
		else if( a == "--negative" )
			negative = true;
		else if( a == "--quiet" )
			g_quiet = true;
		else if( a == "--out" )
			outPath = next();
		else if( a == "--script" )
			scriptPath = next();
		else if( a == "--set" )
			settings.push_back( next() );
		else if( a == "--at" )
		{
			const int f = std::atoi( next().c_str() );
			timed.push_back( { f, next() } );
		}
		else if( a == "--frames" )
		{
			frames      = std::max( 1, std::atoi( next().c_str() ) );
			framesGiven = true;
		}
		else if( a == "--film" )
			film = std::max( 1, std::atoi( next().c_str() ) );
		else if( a == "--fps" )
			fps = std::atof( next().c_str() );
		else if( a == "--size" )
		{
			const std::string s = next();
			if( std::sscanf( s.c_str(), "%dx%d", &width, &height ) != 2 || width <= 0 || height <= 0 )
			{
				usage();
				return 2;
			}
		}
		else if( std::find_if( std::begin( kChecks ), std::end( kChecks ), [ & ]( const char* c ) { return a == c; } ) != std::end( kChecks ) )
			checks.push_back( a );
		else
		{
			usage();
			return 2;
		}
	}

	if( offline )
	{
		checkOffline();
		std::printf( "\n  %s (%d checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks );
		return g_failures == 0 ? 0 : 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "no GL 4.1 context\n" );
		return 1;
	}

	if( negative )
	{
		//Each wrong model must fail at least one check of its own.
		struct Wrong
		{
			const char* what;
			std::function< void() > run;
		};
		const Wrong wrongs[] = {
			{ "no half-level offset and no plateau guard: black is a contour", [] { checkFlat( 1 ); } },
			{ "longitude mirrored", [] { checkGeo( 2 ); } },
			{ "a contour's distance in levels, not pixels", [] {
				 Section( "width (distance in levels)" );
				 const Floats ramp = greyCard( kW, kH, []( double x, double ) { const double u = x / kW; return ( u + 3.5 * u * u ) / 4.5; } );
				 Rig rig( true );
				 rig.Init( kW, kH, &ramp, true );
				 benchSettings( rig, 20.0 );
				 rig.plugin.SetTestFlagsForTest( 8 );
				 rig.Render( 2 );
				 const auto runs = runsAlong( rig.Picture(), kW, kH / 2, 0.001 );
				 double lo = 1e9, hi = 0.0;
				 for( const Run& run : runs )
					 lo = std::min( lo, run.coverage ), hi = std::max( hi, run.coverage );
				 Check( !runs.empty() && hi < 1.08 * lo, fmt( "widths %.3f..%.3f px", lo, hi ) );
			 } },
			{ "Bounce's ages read as Rise's", [] { checkTrail( true ); } },
			{ "contours expected on whole levels of a world without a sea", [] { checkLines( true ); } },
		};
		int caught = 0;
		const bool wasQuiet = g_quiet;
		for( const Wrong& wrong : wrongs )
		{
			const int before = g_failures;
			g_quiet          = true;
			wrong.run();
			g_quiet            = wasQuiet;
			const bool failed  = g_failures > before;
			g_failures         = before;
			caught += failed ? 1 : 0;
			std::printf( "  %s  %s\n", failed ? "ok  " : "MISS", wrong.what );
		}
		const int count = static_cast< int >( sizeof( wrongs ) / sizeof( wrongs[ 0 ] ) );
		std::printf( "\n  %s: %d of %d wrong models caught\n", caught == count ? "PASS" : "FAIL", caught, count );
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return caught == count ? 0 : 1;
	}

	if( all || !checks.empty() )
	{
		auto wants = [ & ]( const char* name ) { return all || std::find( checks.begin(), checks.end(), name ) != checks.end(); };
		if( wants( "--lines" ) )
			checkLines( false );
		if( wants( "--width" ) )
			checkWidth();
		if( wants( "--flat" ) )
			checkFlat( 0 );
		if( wants( "--sweep" ) )
			checkSweep();
		if( wants( "--trail" ) )
			checkTrail( false );
		if( wants( "--geo" ) )
			checkGeo( 0 );
		if( wants( "--globe" ) )
			checkGlobe();
		if( wants( "--alpha" ) )
			checkAlpha();
		if( wants( "--state" ) )
			checkState();
		if( wants( "--resize" ) )
			checkResize();
		if( wants( "--worlds" ) )
			checkWorlds();
		if( wants( "--generated" ) )
			checkGenerated();
		if( wants( "--clock" ) )
			checkClock();
		if( wants( "--controls" ) )
			checkControls();
		if( wants( "--async" ) )
			checkAsync();
		//The bench measures; it has nothing to pass. Not part of --all.
		if( std::find( checks.begin(), checks.end(), "--bench" ) != checks.end() )
			runBench();
		std::printf( "\n  %s (%d checks, %d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures );
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return g_failures == 0 ? 0 : 1;
	}

	int status = 0;
	if( list )
	{
		IsohypsePlugin plugin( over );
		for( const NamedParameter& p : listParameters( plugin ) )
		{
			std::printf( "%3u  %-16s %-8s %g", p.index, p.name.c_str(),
			             p.type == FF_TYPE_OPTION ? "option" : p.type == FF_TYPE_INTEGER ? "integer" : p.type == FF_TYPE_EVENT ? "event" : p.type == FF_TYPE_BOOLEAN ? "bool" : p.type == FF_TYPE_TEXT ? "text" : "standard",
			             p.value );
			if( p.type == FF_TYPE_OPTION )
			{
				std::printf( "  [" );
				for( unsigned int e = 0; e < plugin.GetNumParamElements( p.index ); ++e )
					std::printf( "%s%s", e ? ", " : "", plugin.GetParamElementName( p.index, e ) );
				std::printf( "]" );
			}
			std::printf( "\n" );
		}
	}
	else if( pipe || film > 0 )
		//The Over's --pipe reads frames until its input ends; the source's
		//writes --frames of them, or for ever.
		status = runPipe( over, width, height, fps, scriptPath, film > 0 ? film : ( framesGiven ? frames : -1 ), pipe && over,
		                  settings, timed );
	else
	{
		Rig rig( over );
		rig.fps = fps;
		if( !rig.Init( width, height ) || !applyAll( rig.plugin, settings ) )
			status = 1;
		else
		{
			std::map< unsigned int, Cue > automation;
			std::string error;
			if( !scriptPath.empty() && !bindScript( rig.plugin, scriptPath, automation, error ) )
			{
				std::fprintf( stderr, "%s\n", error.c_str() );
				status = 2;
			}
			for( int f = 0; status == 0 && f < frames; ++f )
			{
				for( const auto& entry : automation )
					rig.plugin.SetFloatParameter( entry.first, valueAt( entry.second.track, f, entry.second.ramp ) );
				for( const Timed& t : timed )
					if( t.frame == f && !applySetting( rig.plugin, t.setting, error ) )
						std::fprintf( stderr, "--at %d %s: %s\n", t.frame, t.setting.c_str(), error.c_str() );
				if( !rig.Render( 1 ) )
					status = 1;
			}
			if( status == 0 )
			{
				const Floats out = rig.Output();
				double sum = 0.0, peak = 0.0;
				for( size_t i = 0; i < out.size(); i += 4 )
				{
					const double l = 0.2126 * out[ i ] + 0.7152 * out[ i + 1 ] + 0.0722 * out[ i + 2 ];
					sum += l;
					peak = std::max( peak, l );
				}
				const FrameState& s = rig.plugin.LastFrame();
				std::printf( "%dx%d, %d frames: mean %.4f, peak %.4f; sweep at %.4f (%+d), lines %.1f, datum %.4f%s%s\n", width, height,
				             frames, sum / ( out.size() / 4 ), peak, s.sweepPosition, s.sweepDirection, s.lines, s.datum,
				             rig.plugin.WorldError().empty() ? "" : "; ", rig.plugin.WorldError().c_str() );
				if( !outPath.empty() && !writePng( outPath, width, height, out ) )
				{
					std::fprintf( stderr, "could not write %s\n", outPath.c_str() );
					status = 1;
				}
			}
		}
	}
	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return status;
}
