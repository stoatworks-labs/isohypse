#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace isohypse
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
/// -1..+1 from 0..1, squared either side of the middle: the slow end of a
/// speed gets most of the slider.
double bipolarSquared( float v )
{
	const double x = 2.0 * clamp01( v ) - 1.0;
	return x * std::fabs( x );
}
float inverseBipolarSquared( double y )
{
	const double c = std::clamp( y, -1.0, 1.0 );
	const double x = c < 0.0 ? -std::sqrt( -c ) : std::sqrt( c );
	return static_cast< float >( 0.5 * ( x + 1.0 ) );
}
constexpr double kZoomLow = 1.0, kZoomHigh = 64.0;
constexpr double kSpinMax = 60.0;
constexpr double kLinesLow = 4.0, kLinesHigh = 300.0;
constexpr double kWidthLow = 0.4, kWidthHigh = 10.0;
constexpr double kSweepLow = 0.5, kSweepHigh = 120.0;
constexpr double kFlowMax = 4.0;
constexpr double kFrequencySmall = 6.0, kFrequencyBig = 0.6;
} // namespace

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		//Not Background on the Over: its ground is always opaque, the clip mixed
		//in by Clip Show, so the composite never reads it.
		for( unsigned int id = PT_WORLD; id <= PT_MORPH; ++id )
			if( !( over && id == PT_BACKGROUND ) )
				order.push_back( id );
		if( over )
			order.insert( order.end(), { PT_CLIP_SHOW, PT_INVERT, PT_MIX } );
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

const char* GroupOf( unsigned int id )
{
	if( id <= PT_ROTATION )
		return "World";
	if( id <= PT_DEPTHS )
		return "Contours";
	if( id <= PT_RESTART )
		return "Sweep";
	if( id <= PT_BACKGROUND )
		return "Look";
	if( id <= PT_MORPH )
		return "Generated";
	if( id <= PT_MIX )
		return "Clip";
	return "About";
}

const char* NameOf( unsigned int id )
{
	switch( id )
	{
	case PT_WORLD: return "World";
	case PT_PLACE: return "Place";
	case PT_VIEW: return "View";
	case PT_LONGITUDE: return "Longitude";
	case PT_LATITUDE: return "Latitude";
	case PT_ZOOM: return "Zoom";
	case PT_SPIN: return "Spin";
	case PT_ROTATION: return "Rotation";
	case PT_LINES: return "Lines";
	case PT_INDEX_EVERY: return "Index Every";
	case PT_LINE_WIDTH: return "Line Width";
	case PT_SMOOTH: return "Smooth";
	case PT_SEA_FILL: return "Sea Fill";
	case PT_DEPTHS: return "Depths";
	case PT_SWEEP: return "Sweep";
	case PT_SWEEP_TIME: return "Sweep Time";
	case PT_SWEEP_BARS: return "Sweep Bars";
	case PT_SWEEP_LEVEL: return "Sweep Level";
	case PT_SWEEP_GLOW: return "Sweep Glow";
	case PT_TRAIL: return "Trail";
	case PT_FLOOR: return "Floor";
	case PT_FLOOD: return "Flood";
	case PT_FLOW: return "Flow";
	case PT_RESTART: return "Restart";
	case PT_STYLE: return "Style";
	case PT_HUE: return "Hue";
	case PT_HEIGHT_COLOUR: return "Height Colour";
	case PT_GLOW: return "Glow";
	case PT_SHADE: return "Shade";
	case PT_TINTS: return "Tints";
	case PT_WOBBLE: return "Wobble";
	case PT_BACKGROUND: return "Background";
	case PT_TERRAIN: return "Terrain";
	case PT_SEED: return "Seed";
	case PT_FEATURE_SIZE: return "Feature Size";
	case PT_ROUGHNESS: return "Roughness";
	case PT_GENERATED_SEA: return "Sea";
	case PT_MORPH: return "Morph";
	case PT_CLIP_SHOW: return "Clip Show";
	case PT_INVERT: return "Invert";
	case PT_MIX: return "Mix";
	default: return "?";
	}
}

const char* ViewName( View view )
{
	switch( view )
	{
	case View::Map: return "Map";
	case View::Globe: return "Globe";
	default: return "?";
	}
}

const char* SweepName( Sweep sweep )
{
	switch( sweep )
	{
	case Sweep::Off: return "Off";
	case Sweep::Rise: return "Rise";
	case Sweep::Fall: return "Fall";
	case Sweep::Bounce: return "Bounce";
	case Sweep::Manual: return "Manual";
	default: return "?";
	}
}

const char* BarsName( int index )
{
	static const char* const names[ kBarChoices ] = { "Free", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars", "32 Bars", "64 Bars" };
	return index >= 0 && index < kBarChoices ? names[ index ] : "?";
}

int BarsOf( int index )
{
	return index <= 0 || index >= kBarChoices ? 0 : 1 << ( index - 1 );
}

const char* StyleName( Style style )
{
	switch( style )
	{
	case Style::Ink: return "Ink";
	case Style::Plotter: return "Plotter";
	case Style::Neon: return "Neon";
	case Style::Survey: return "Survey";
	default: return "?";
	}
}

const char* TerrainName( Terrain terrain )
{
	switch( terrain )
	{
	case Terrain::Continents: return "Continents";
	case Terrain::Ridges: return "Ridges";
	case Terrain::Craters: return "Craters";
	default: return "?";
	}
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double LongitudeFromParam( float v )
{
	return 360.0 * clamp01( v ) - 180.0;
}
float ParamFromLongitude( double degrees )
{
	return static_cast< float >( std::clamp( ( degrees + 180.0 ) / 360.0, 0.0, 1.0 ) );
}
double LatitudeFromParam( float v )
{
	return 180.0 * clamp01( v ) - 90.0;
}
float ParamFromLatitude( double degrees )
{
	return static_cast< float >( std::clamp( ( degrees + 90.0 ) / 180.0, 0.0, 1.0 ) );
}
double ZoomFromParam( float v )
{
	return geometric( v, kZoomLow, kZoomHigh );
}
float ParamFromZoom( double zoom )
{
	return inverseGeometric( zoom, kZoomLow, kZoomHigh );
}
double SpinFromParam( float v )
{
	return kSpinMax * bipolarSquared( v );
}
float ParamFromSpin( double degreesPerSecond )
{
	return inverseBipolarSquared( degreesPerSecond / kSpinMax );
}
double RotationFromParam( float v )
{
	return 360.0 * clamp01( v ) - 180.0;
}

double LinesFromParam( float v )
{
	return geometric( v, kLinesLow, kLinesHigh );
}
float ParamFromLines( double lines )
{
	return inverseGeometric( lines, kLinesLow, kLinesHigh );
}
double LineWidthFromParam( float v )
{
	return geometric( v, kWidthLow, kWidthHigh );
}
float ParamFromLineWidth( double pixels )
{
	return inverseGeometric( pixels, kWidthLow, kWidthHigh );
}
double SmoothFromParam( float v )
{
	return 6.0 * clamp01( v );
}

double SweepSecondsFromParam( float v )
{
	return geometric( v, kSweepLow, kSweepHigh );
}
float ParamFromSweepSeconds( double seconds )
{
	return inverseGeometric( seconds, kSweepLow, kSweepHigh );
}
double TrailFromParam( float v )
{
	return clamp01( v );
}
double FlowFromParam( float v )
{
	return kFlowMax * bipolarSquared( v );
}
float ParamFromFlow( double levelsPerSecond )
{
	return inverseBipolarSquared( levelsPerSecond / kFlowMax );
}

double FeatureFrequencyFromParam( float v )
{
	return geometric( v, kFrequencySmall, kFrequencyBig );
}
double MorphFromParam( float v )
{
	return 0.2 * clamp01( v ) * clamp01( v );
}

} // namespace isohypse
