#include "Worlds.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#if defined( _WIN32 )
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

//Static, PNG only: a host that links its own stb_image must not get ours,
//and nothing here reads anything but the worlds' PNGs.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined( __clang__ )
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined( __clang__ )
	#pragma clang diagnostic pop
#endif

namespace isohypse
{
namespace
{
std::vector< WorldInfo > buildGlobes()
{
	std::vector< WorldInfo > list;
#define ISOHYPSE_GLOBE( id_, name_, file_, w_, h_, lo_, hi_, nodata_, sea_ ) \
	{                                                                      \
		WorldInfo w;                                                       \
		w.name      = name_;                                               \
		w.file      = file_;                                               \
		w.kind      = Kind::Globe;                                         \
		w.width     = w_;                                                  \
		w.height    = h_;                                                  \
		w.minMetres = lo_;                                                 \
		w.maxMetres = hi_;                                                 \
		w.hasNodata = ( nodata_ ) != 0;                                    \
		w.hasSea    = ( sea_ ) != 0;                                       \
		list.push_back( w );                                               \
	}
#define ISOHYPSE_PLACE( ... )
#include "WorldTable.inc"
#undef ISOHYPSE_GLOBE
#undef ISOHYPSE_PLACE
	return list;
}

std::vector< WorldInfo > buildPlaces()
{
	std::vector< WorldInfo > list;
#define ISOHYPSE_GLOBE( ... )
#define ISOHYPSE_PLACE( id_, name_, file_, w_, h_, lo_, hi_, sea_, lat_, lon_, mpp_ ) \
	{                                                                            \
		WorldInfo w;                                                             \
		w.name           = name_;                                                \
		w.file           = file_;                                                \
		w.kind           = Kind::Place;                                          \
		w.width          = w_;                                                   \
		w.height         = h_;                                                   \
		w.minMetres      = lo_;                                                  \
		w.maxMetres      = hi_;                                                  \
		w.hasSea         = ( sea_ ) != 0;                                        \
		w.centreLat      = lat_;                                                 \
		w.centreLon      = lon_;                                                 \
		w.metresPerPixel = mpp_;                                                 \
		list.push_back( w );                                                     \
	}
#include "WorldTable.inc"
#undef ISOHYPSE_GLOBE
#undef ISOHYPSE_PLACE
	return list;
}

std::string environmentVariable( const char* name )
{
#if defined( _WIN32 )
	char* value = nullptr;
	size_t size = 0;
	if( _dupenv_s( &value, &size, name ) != 0 || value == nullptr )
		return {};
	std::string result( value );
	std::free( value );
	return result;
#else
	const char* value = std::getenv( name );
	return value ? value : "";
#endif
}

bool readFile( const std::string& path, std::vector< unsigned char >& bytes )
{
	std::ifstream in( path, std::ios::binary );
	if( !in )
		return false;
	bytes.assign( std::istreambuf_iterator< char >( in ), std::istreambuf_iterator< char >() );
	return !bytes.empty();
}

#if defined( _WIN32 )
HMODULE thisModule()
{
	HMODULE module = nullptr;
	GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                    reinterpret_cast< LPCSTR >( &thisModule ), &module );
	return module;
}

bool readResource( const char* file, std::vector< unsigned char >& bytes )
{
	HMODULE module = thisModule();
	if( module == nullptr )
		return false;
	const std::string name = ResourceNameOf( file );
	HRSRC found            = FindResourceA( module, name.c_str(), MAKEINTRESOURCEA( 10 ) );// RT_RCDATA
	if( found == nullptr )
		return false;
	HGLOBAL loaded = LoadResource( module, found );
	const DWORD size = SizeofResource( module, found );
	const void* data = loaded ? LockResource( loaded ) : nullptr;
	if( data == nullptr || size == 0 )
		return false;
	const unsigned char* begin = static_cast< const unsigned char* >( data );
	bytes.assign( begin, begin + size );
	return true;
}
#else
/// The bundle this code was loaded from: .../X.bundle/Contents/MacOS/X, so
/// its worlds are in .../X.bundle/Contents/Resources/worlds.
std::string bundleWorlds()
{
	Dl_info info = {};
	if( dladdr( reinterpret_cast< const void* >( &bundleWorlds ), &info ) == 0 || info.dli_fname == nullptr )
		return {};
	std::string path( info.dli_fname );
	const size_t slash = path.rfind( '/' );
	if( slash == std::string::npos )
		return {};
	path.erase( slash );// .../Contents/MacOS
	return path + "/../Resources/worlds";
}
#endif

bool readWorldFile( const char* file, std::vector< unsigned char >& bytes )
{
	const std::string dir = environmentVariable( "ISOHYPSE_DATA_DIR" );
	if( !dir.empty() && readFile( dir + "/" + file, bytes ) )
		return true;
#if defined( _WIN32 )
	return readResource( file, bytes );
#else
	const std::string bundle = bundleWorlds();
	return !bundle.empty() && readFile( bundle + "/" + file, bytes );
#endif
}
} // namespace

const std::vector< WorldInfo >& Globes()
{
	static const std::vector< WorldInfo > list = buildGlobes();
	return list;
}

const std::vector< WorldInfo >& Places()
{
	static const std::vector< WorldInfo > list = buildPlaces();
	return list;
}

int WorldChoiceCount( bool effect )
{
	return static_cast< int >( Globes().size() ) + 2 + ( effect ? 1 : 0 );
}

const char* WorldChoiceName( int index, bool effect )
{
	int globe          = 0;
	const Choice which = ChoiceOf( index, effect, globe );
	switch( which )
	{
	case Choice::Globe: return Globes()[ static_cast< size_t >( globe ) ].name;
	case Choice::Place: return "Place";
	case Choice::Generated: return "Generated";
	case Choice::Clip: return "Clip";
	}
	return "?";
}

Choice ChoiceOf( int index, bool effect, int& globe )
{
	const int globes = static_cast< int >( Globes().size() );
	index            = std::clamp( index, 0, WorldChoiceCount( effect ) - 1 );
	globe            = 0;
	if( index < globes )
	{
		globe = index;
		return Choice::Globe;
	}
	if( index == globes )
		return Choice::Place;
	if( index == globes + 1 )
		return Choice::Generated;
	return Choice::Clip;
}

int WorldIndexOf( Choice choice, int globe, bool effect )
{
	const int globes = static_cast< int >( Globes().size() );
	switch( choice )
	{
	case Choice::Globe: return std::clamp( globe, 0, std::max( globes - 1, 0 ) );
	case Choice::Place: return globes;
	case Choice::Generated: return globes + 1;
	case Choice::Clip: return effect ? globes + 2 : globes + 1;
	}
	return 0;
}

void FillGaps( int width, int height, std::vector< float >& values, const std::vector< uint8_t >& measured )
{
	struct Level
	{
		int w, h;
		std::vector< float > sum, weight;
	};
	std::vector< Level > levels;
	{
		Level base { width, height, std::vector< float >( values.size() ), std::vector< float >( values.size() ) };
		for( size_t i = 0; i < values.size(); ++i )
		{
			base.weight[ i ] = measured[ i ] ? 1.0f : 0.0f;
			base.sum[ i ]    = measured[ i ] ? values[ i ] : 0.0f;
		}
		levels.push_back( std::move( base ) );
	}
	while( levels.back().w > 1 || levels.back().h > 1 )
	{
		const Level& fine = levels.back();
		Level coarse;
		coarse.w = std::max( 1, ( fine.w + 1 ) / 2 );
		coarse.h = std::max( 1, ( fine.h + 1 ) / 2 );
		coarse.sum.assign( static_cast< size_t >( coarse.w ) * coarse.h, 0.0f );
		coarse.weight.assign( coarse.sum.size(), 0.0f );
		for( int y = 0; y < fine.h; ++y )
			for( int x = 0; x < fine.w; ++x )
			{
				const size_t from = static_cast< size_t >( y ) * fine.w + x;
				const size_t to   = static_cast< size_t >( y / 2 ) * coarse.w + x / 2;
				coarse.sum[ to ] += fine.sum[ from ];
				coarse.weight[ to ] += fine.weight[ from ];
			}
		levels.push_back( std::move( coarse ) );
	}
	//Push: each level's unmeasured cells take their parent's mean.
	for( size_t k = levels.size() - 1; k-- > 0; )
	{
		Level& fine         = levels[ k ];
		const Level& coarse = levels[ k + 1 ];
		for( int y = 0; y < fine.h; ++y )
			for( int x = 0; x < fine.w; ++x )
			{
				const size_t i = static_cast< size_t >( y ) * fine.w + x;
				if( fine.weight[ i ] > 0.0f )
					continue;
				const size_t parent = static_cast< size_t >( y / 2 ) * coarse.w + x / 2;
				const float mean    = coarse.weight[ parent ] > 0.0f ? coarse.sum[ parent ] / coarse.weight[ parent ] : 0.0f;
				fine.sum[ i ]       = mean;
				fine.weight[ i ]    = 1.0f;
			}
	}
	const Level& base = levels.front();
	for( size_t i = 0; i < values.size(); ++i )
		if( !measured[ i ] )
			values[ i ] = base.weight[ i ] > 0.0f ? base.sum[ i ] / base.weight[ i ] : 0.0f;
}

Heightfield LoadHeightfield( const WorldInfo& world )
{
	Heightfield out;
	if( world.file == nullptr )
	{
		out.error = std::string( world.name ) + " has no file";
		return out;
	}
	std::vector< unsigned char > bytes;
	if( !readWorldFile( world.file, bytes ) )
	{
		out.error = std::string( "could not read " ) + world.file + " (looked in " + DataLocation() + ")";
		return out;
	}
	int w = 0, h = 0, channels = 0;
	stbi_us* pixels = stbi_load_16_from_memory( bytes.data(), static_cast< int >( bytes.size() ), &w, &h, &channels, 1 );
	if( pixels == nullptr )
	{
		out.error = std::string( world.file ) + ": " + stbi_failure_reason();
		return out;
	}
	if( w != world.width || h != world.height )
	{
		char message[ 256 ];
		std::snprintf( message, sizeof( message ), "%s is %dx%d, the table says %dx%d", world.file, w, h, world.width,
		               world.height );
		out.error = message;
		stbi_image_free( pixels );
		return out;
	}
	const size_t count = static_cast< size_t >( w ) * h;
	out.width          = w;
	out.height         = h;
	out.texels.resize( count * 2 );
	if( world.hasNodata )
	{
		//0 is "never measured"; the data is 1..65535 for min..max.
		std::vector< float > values( count );
		std::vector< uint8_t > measured( count );
		for( size_t i = 0; i < count; ++i )
		{
			measured[ i ] = pixels[ i ] != 0;
			values[ i ]   = measured[ i ] ? ( pixels[ i ] - 1.0f ) / 65534.0f : 0.0f;
		}
		FillGaps( w, h, values, measured );
		for( size_t i = 0; i < count; ++i )
		{
			out.texels[ i * 2 ]     = static_cast< uint16_t >( std::lround( std::clamp( values[ i ], 0.0f, 1.0f ) * 65535.0f ) );
			out.texels[ i * 2 + 1 ] = measured[ i ] ? 65535 : 0;
		}
	}
	else
	{
		for( size_t i = 0; i < count; ++i )
		{
			out.texels[ i * 2 ]     = pixels[ i ];
			out.texels[ i * 2 + 1 ] = 65535;
		}
	}
	stbi_image_free( pixels );
	return out;
}

std::string ResourceNameOf( const char* file )
{
	std::string name;
	for( const char* c = file; *c; ++c )
	{
		const char ch = *c;
		if( ch >= 'a' && ch <= 'z' )
			name += static_cast< char >( ch - 'a' + 'A' );
		else if( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= '0' && ch <= '9' ) )
			name += ch;
		else
			name += '_';
	}
	return name;
}

std::string DataLocation()
{
	const std::string dir = environmentVariable( "ISOHYPSE_DATA_DIR" );
	if( !dir.empty() )
		return dir + " (ISOHYPSE_DATA_DIR)";
#if defined( _WIN32 )
	return "the plugin's own resources";
#else
	return bundleWorlds();
#endif
}

} // namespace isohypse
