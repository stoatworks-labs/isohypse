#include "Shaders.h"

namespace isohypse::shaders
{
const char* const kVersion = "#version 410 core\n";

//===========================================================================
// The library. No #version, no main.
//
// Randomness is integer: the PCG output mix, exact in 32 bits on every GPU.
//===========================================================================
const char* const kCommon = R"(
//smoothstep is undefined for edge0 >= edge1 (GLSL 4.10, 8.3): every fade
//down is written 1 - smoothstep( low, high, x ).
const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;

uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float unitFloat( uint h )
{
	return float( h >> 8u ) * ( 1.0 / 16777216.0 );
}
uint mixHash( uint a, uint b, uint c )
{
	return pcg( a ^ pcg( b ^ pcg( c ) ) );
}

float linearFromSrgb( float c )
{
	return c <= 0.04045 ? c / 12.92 : pow( ( c + 0.055 ) / 1.055, 2.4 );
}
float srgbFromLinear( float c )
{
	c = clamp( c, 0.0, 1.0 );
	return c <= 0.0031308 ? 12.92 * c : 1.055 * pow( c, 1.0 / 2.4 ) - 0.055;
}
vec3 linearFromSrgb3( vec3 c )
{
	return vec3( linearFromSrgb( c.r ), linearFromSrgb( c.g ), linearFromSrgb( c.b ) );
}
vec3 srgbFromLinear3( vec3 c )
{
	return vec3( srgbFromLinear( c.r ), srgbFromLinear( c.g ), srgbFromLinear( c.b ) );
}

/// Value noise in the plane, smoothstep-interpolated, in [0, 1].
float valueNoise2( vec2 p, uint salt )
{
	vec2 i  = floor( p );
	vec2 f  = p - i;
	vec2 s  = f * f * ( 3.0 - 2.0 * f );
	uint x  = uint( int( i.x ) ), y = uint( int( i.y ) );
	float a = unitFloat( mixHash( x, y, salt ) );
	float b = unitFloat( mixHash( x + 1u, y, salt ) );
	float c = unitFloat( mixHash( x, y + 1u, salt ) );
	float d = unitFloat( mixHash( x + 1u, y + 1u, salt ) );
	return mix( mix( a, b, s.x ), mix( c, d, s.x ), s.y );
}
)";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//===========================================================================
// Generate: a world from noise on the unit sphere, so the map has no seam
// at the date line and no pinch at the poles. Row 0 is the north pole and
// column 0 longitude -180, as the files are.
//===========================================================================
const char* const kGenerateFragment = R"(
uniform vec2 Size;
uniform int TerrainKind;
uniform float Frequency;
uniform float Gain;
uniform vec3 Offset;
uniform uint Seed;
out vec4 fragColour;

vec3 gradientAt( ivec3 c, uint salt )
{
	uint h  = mixHash( uint( c.x ), uint( c.y ), uint( c.z ) ^ salt );
	float a = unitFloat( h ) * TAU;
	float z = unitFloat( pcg( h ) ) * 2.0 - 1.0;
	float r = sqrt( max( 1.0 - z * z, 0.0 ) );
	return vec3( r * cos( a ), r * sin( a ), z );
}
/// Gradient (Perlin) noise, about -1..1.
float gradientNoise( vec3 p, uint salt )
{
	vec3 i  = floor( p );
	vec3 f  = p - i;
	ivec3 c = ivec3( i );
	vec3 u  = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float n000 = dot( gradientAt( c, salt ), f );
	float n100 = dot( gradientAt( c + ivec3( 1, 0, 0 ), salt ), f - vec3( 1.0, 0.0, 0.0 ) );
	float n010 = dot( gradientAt( c + ivec3( 0, 1, 0 ), salt ), f - vec3( 0.0, 1.0, 0.0 ) );
	float n110 = dot( gradientAt( c + ivec3( 1, 1, 0 ), salt ), f - vec3( 1.0, 1.0, 0.0 ) );
	float n001 = dot( gradientAt( c + ivec3( 0, 0, 1 ), salt ), f - vec3( 0.0, 0.0, 1.0 ) );
	float n101 = dot( gradientAt( c + ivec3( 1, 0, 1 ), salt ), f - vec3( 1.0, 0.0, 1.0 ) );
	float n011 = dot( gradientAt( c + ivec3( 0, 1, 1 ), salt ), f - vec3( 0.0, 1.0, 1.0 ) );
	float n111 = dot( gradientAt( c + ivec3( 1, 1, 1 ), salt ), f - vec3( 1.0, 1.0, 1.0 ) );
	float x00 = mix( n000, n100, u.x ), x10 = mix( n010, n110, u.x );
	float x01 = mix( n001, n101, u.x ), x11 = mix( n011, n111, u.x );
	return 1.6 * mix( mix( x00, x10, u.y ), mix( x01, x11, u.y ), u.z );
}
float fbm( vec3 p, int octaves, float gain, uint salt )
{
	float sum = 0.0, amp = 1.0, norm = 0.0;
	for( int i = 0; i < 10; ++i )
	{
		if( i >= octaves )
			break;
		sum += amp * gradientNoise( p, salt + uint( i ) * 0x9e3779b9u );
		norm += amp;
		amp *= gain;
		p = p * 2.03 + vec3( 1.7, 9.2, 4.1 );
	}
	return sum / norm;
}

float continents( vec3 P )
{
	float shape  = fbm( P * Frequency * 0.6 + Offset, 3, 0.5, Seed );
	float detail = fbm( P * Frequency * 2.0 + Offset * 1.3, 8, Gain, Seed ^ 0x51ed27u );
	return clamp( 0.5 + 0.9 * shape + 0.35 * detail, 0.0, 1.0 );
}

float ridges( vec3 P )
{
	vec3 p = P * Frequency + Offset;
	float sum = 0.0, amp = 0.6, weight = 1.0;
	for( int i = 0; i < 8; ++i )
	{
		float n = 1.0 - abs( gradientNoise( p, Seed + uint( i ) * 7919u ) );
		n *= n;
		n *= weight;
		weight = clamp( n * 1.5, 0.0, 1.0 );
		sum += n * amp;
		amp *= Gain;
		p = p * 2.1 + vec3( 3.1, 7.7, 1.3 );
	}
	return clamp( sum * 0.9 - 0.05, 0.0, 1.0 );
}

/// A crater d crater-radii from its centre: a bowl with its floor flattened,
/// a raised rim, and the rim's apron falling away outside.
float craterProfile( float d )
{
	if( d < 1.0 )
		return max( d * d - 1.0, -0.65 ) + 0.35 * smoothstep( 0.75, 1.0, d );
	return 0.35 * exp( -( d - 1.0 ) * 4.0 );
}
float craterField( vec3 q, float amplitude, uint salt )
{
	vec3 cell = floor( q );
	float h   = 0.0;
	for( int z = -1; z <= 1; ++z )
		for( int y = -1; y <= 1; ++y )
			for( int x = -1; x <= 1; ++x )
			{
				ivec3 c = ivec3( cell ) + ivec3( x, y, z );
				uint k  = mixHash( uint( c.x ), uint( c.y ), uint( c.z ) ^ salt );
				if( unitFloat( k ) > 0.55 )
					continue;
				vec3 centre  = vec3( c ) + vec3( unitFloat( pcg( k ) ), unitFloat( pcg( k + 1u ) ), unitFloat( pcg( k + 2u ) ) );
				float radius = mix( 0.2, 0.5, unitFloat( pcg( k + 3u ) ) );
				float d      = length( q - centre ) / radius;
				if( d < 3.0 )
					h += amplitude * radius * craterProfile( d );
			}
	return h;
}
float craters( vec3 P )
{
	float h = 0.5 + 0.15 * fbm( P * Frequency * 1.5 + Offset, 6, Gain, Seed );
	h += craterField( P * Frequency * 2.0 + Offset, 0.5, Seed ^ 1u );
	h += craterField( P * Frequency * 5.0 + Offset * 1.7, 0.3, Seed ^ 2u );
	h += craterField( P * Frequency * 12.0 + Offset * 2.3, 0.18, Seed ^ 3u );
	return clamp( h, 0.0, 1.0 );
}

void main()
{
	float lon = ( gl_FragCoord.x / Size.x ) * TAU - PI;
	float lat = 0.5 * PI - ( gl_FragCoord.y / Size.y ) * PI;
	vec3 P    = vec3( cos( lat ) * sin( lon ), sin( lat ), cos( lat ) * cos( lon ) );
	float h   = TerrainKind == 0 ? continents( P ) : ( TerrainKind == 1 ? ridges( P ) : craters( P ) );
	fragColour = vec4( h, 1.0, 0.0, 1.0 );
}
)";

//===========================================================================
// Intake (Over, Clip): the clip's luma as height. Luma of the sRGB values,
// not of linear light: the height is what the eye reads as bright.
//===========================================================================
const char* const kIntakeFragment = R"(
uniform sampler2D ClipIn;
uniform int InvertLuma;
out vec4 fragColour;
void main()
{
	vec4 c  = texelFetch( ClipIn, ivec2( gl_FragCoord.xy ), 0 );
	float y = clamp( dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ), 0.0, 1.0 );
	if( InvertLuma == 1 )
		y = 1.0 - y;
	fragColour = vec4( y, 1.0, 0.0, 1.0 );
}
)";

//===========================================================================
// The map pass, part one: where a pixel lands on the heightfield, and the
// height there.
//
// A LEVEL is one contour interval: level( h ) = ( h - Datum ) * Lines, with
// h the normalised height (0 the world's lowest point, 1 its highest), so the
// contours are where the level is a whole number. Datum puts a contour on
// sea level for a world with a sea, and half an interval off the bottom for
// one without (a black clip or a flat floor lying exactly on a contour would
// otherwise draw a solid sheet).
//===========================================================================
const char* const kMapLibrary = R"(
uniform sampler2D Height;
uniform vec2 HeightSize;
uniform float HeightTop;
uniform int Projection;
uniform int WrapX;
uniform vec2 Raster;
uniform vec2 ViewOrigin;
uniform vec2 Centre;
uniform float Zoom;
uniform float Roll;
uniform mat3 Orientation;
uniform float TexAspect;
uniform float SmoothBias;
uniform float Lines;
uniform float Datum;
/// The harness's negative controls, 0 in the plugin: 1 no plateau guard,
/// 2 longitude mirrored, 8 a contour's distance in levels, not pixels.
uniform int TestFlags;

const float GLOBE_FILL = 0.92;

vec2 rotate2( vec2 v, float a )
{
	float c = cos( a ), s = sin( a );
	return vec2( c * v.x - s * v.y, s * v.x + c * v.y );
}

/// The view's own coordinates: the centre of the raster at 0, the top and
/// bottom edges at y = +1 and -1.
vec2 viewOf( vec2 p )
{
	return ( p - 0.5 * Raster ) / ( 0.5 * Raster.y );
}

/// Where pixel p lands: xy the heightfield's texture coordinate, z 1 on the
/// world and 0 off it, w the globe's depth (1 at the centre, 0 at the limb).
///   0 Map    equirectangular, Centre (lon, lat) in degrees, Zoom 1 = 180 deg
///            of latitude across the height
///   1 Globe  orthographic, Orientation the world from the view
///   2 Flat   a tile, north up, Centre a pan in tile widths
///   3 Screen the clip itself, pixel for pixel
vec4 project( vec2 p )
{
	if( Projection == 3 )
		return vec4( p / Raster, 1.0, 1.0 );
	vec2 view = viewOf( p );
	if( Projection == 1 )
	{
		vec2 q   = view / ( GLOBE_FILL * Zoom );
		float r  = length( q );
		float on = r <= 1.0 ? 1.0 : 0.0;
		//A neighbour past the limb reads the limb, so the derivative at the
		//edge stays finite.
		if( r > 1.0 )
			q /= r;
		float z   = sqrt( max( 1.0 - dot( q, q ), 0.0 ) );
		vec3 w    = Orientation * vec3( q, z );
		float lat = asin( clamp( w.y, -1.0, 1.0 ) );
		float lon = atan( w.x, w.z );
		if( ( TestFlags & 2 ) != 0 )
			lon = -lon;
		return vec4( lon / TAU + 0.5, 0.5 - lat / PI, on, z );
	}
	vec2 q = rotate2( view, Roll );
	if( Projection == 0 )
	{
		float lon = Centre.x + q.x * 90.0 / Zoom;
		float lat = Centre.y + q.y * 90.0 / Zoom;
		if( ( TestFlags & 2 ) != 0 )
			lon = -lon;
		float on  = abs( lat ) <= 90.0 ? 1.0 : 0.0;
		return vec4( ( lon + 180.0 ) / 360.0, ( 90.0 - lat ) / 180.0, on, 1.0 );
	}
	vec2 t = vec2( 0.5 + Centre.x + q.x * 0.5 / ( Zoom * TexAspect ), 0.5 - Centre.y - q.y * 0.5 / Zoom );
	//A tile's edge fades over 2% of it, so a map turned or panned past its
	//edge reads as a sheet laid down rather than a square cut out.
	float edge = min( min( t.x, 1.0 - t.x ), min( t.y, 1.0 - t.y ) );
	return vec4( t, smoothstep( 0.0, 0.02, edge ), 1.0 );
}

/// The difference of two texture coordinates, the short way round in x for
/// a world whose longitude wraps.
vec2 texDelta( vec2 a, vec2 b )
{
	vec2 d = a - b;
	if( WrapX == 1 )
		d.x -= floor( d.x + 0.5 );
	return d;
}

/// The heightfield at one mip level through a cubic B-spline, in four
/// bilinear taps (GPU Gems 2, ch. 20). C2: a contour through it is a smooth
/// curve however far it is magnified, where bilinear would kink at every
/// texel edge.
vec2 bspline( vec2 t, float level )
{
	vec2 size = max( floor( HeightSize / exp2( level ) ), vec2( 1.0 ) );
	vec2 st   = t * size - 0.5;
	vec2 i    = floor( st );
	vec2 f    = st - i;
	vec2 f2 = f * f, f3 = f2 * f;
	vec2 w0 = ( 1.0 - 3.0 * f + 3.0 * f2 - f3 ) / 6.0;
	vec2 w1 = ( 4.0 - 6.0 * f2 + 3.0 * f3 ) / 6.0;
	vec2 w2 = ( 1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3 ) / 6.0;
	vec2 w3 = f3 / 6.0;
	vec2 g0 = w0 + w1, g1 = w2 + w3;
	vec2 h0 = ( i - 0.5 + w1 / g0 ) / size;
	vec2 h1 = ( i + 1.5 + w3 / g1 ) / size;
	vec2 a  = textureLod( Height, vec2( h0.x, h0.y ), level ).rg;
	vec2 b  = textureLod( Height, vec2( h1.x, h0.y ), level ).rg;
	vec2 c  = textureLod( Height, vec2( h0.x, h1.y ), level ).rg;
	vec2 d  = textureLod( Height, vec2( h1.x, h1.y ), level ).rg;
	return g0.y * ( g0.x * a + g1.x * b ) + g1.y * ( g0.x * c + g1.x * d );
}

/// Height (x, normalised) and measured (y) at a fractional level of detail.
vec2 heightAt( vec2 t, float lod )
{
	float base = floor( lod );
	float frac = lod - base;
	vec2 h     = bspline( t, base );
	if( frac > 0.001 && base < HeightTop )
		h = mix( h, bspline( t, base + 1.0 ), frac );
	return h;
}

float levelOf( float h )
{
	return ( h - Datum ) * Lines;
}
)";

//===========================================================================
// The map pass, part two: the contours and everything drawn round them.
//
// A contour's distance from the pixel is |level - k| / |grad level|, the
// gradient in levels per pixel by central differences one pixel either side
// (through the projection, so it is right on a globe's limb and a rotated
// map alike). That makes a line the same width on a cliff and on a plain. A
// line is not drawn where the five samples are all one height (a plateau
// lying on a level), and lines closer than they can be read fade out, the
// index contours last.
//===========================================================================
const char* const kMapFragment = R"(
uniform float FlowPhase;
uniform int HasSea;
uniform int IndexEvery;
uniform float LineWidth;
uniform float SeaFill;
uniform float Depths;
uniform float WobbleAmount;
uniform float WobbleScale;

uniform int SweepMode;
uniform int SweepDir;
uniform float SweepPos;
uniform float Trail;
uniform float FloorLevel;
uniform float SweepGlow;
uniform float Flood;

uniform vec3 LineColour;
uniform vec3 IndexColour;
uniform vec3 AccentColour;
uniform vec3 SeaLineColour;
uniform vec3 SeaFillColour;
uniform vec3 FloodColour;
uniform vec3 DiscColour;
uniform float HeightColour;
uniform float Tints;
uniform float Shade;
uniform int DarkGround;
uniform int LandPalette;

layout( location = 0 ) out vec4 picture;
layout( location = 1 ) out vec4 light;

/// A line `width` pixels wide, `d` pixels away: box-filtered coverage. A
/// line under a pixel wide is drawn a pixel wide and that much fainter.
float coverage( float d, float width )
{
	float w = max( width, 1.0 );
	return clamp( 0.5 * w + 0.5 - d, 0.0, 1.0 ) * min( width, 1.0 );
}

vec3 ramp( float t, vec3 a, vec3 b, vec3 c, vec3 d, vec3 e )
{
	t = clamp( t, 0.0, 1.0 ) * 4.0;
	if( t < 1.0 )
		return mix( a, b, t );
	if( t < 2.0 )
		return mix( b, c, t - 1.0 );
	if( t < 3.0 )
		return mix( c, d, t - 2.0 );
	return mix( d, e, t - 3.0 );
}
/// Height as colour, sRGB in, linear out. With a sea: the bathymetric blues
/// below sea level, the hypsometric greens, browns and snow above. Without:
/// the same land colours from the lowest ground up for the Survey, and a
/// spectrum from violet up to red for the rest.
vec3 palette( float t )
{
	vec3 c;
	if( HasSea == 1 )
	{
		if( t < Datum )
			c = mix( vec3( 0.02, 0.06, 0.22 ), vec3( 0.32, 0.64, 0.86 ), pow( clamp( t / max( Datum, 1.0e-4 ), 0.0, 1.0 ), 1.5 ) );
		else
			c = ramp( ( t - Datum ) / max( 1.0 - Datum, 1.0e-4 ), vec3( 0.20, 0.50, 0.25 ), vec3( 0.62, 0.68, 0.34 ),
			          vec3( 0.68, 0.50, 0.30 ), vec3( 0.56, 0.45, 0.42 ), vec3( 0.97, 0.97, 0.97 ) );
	}
	else if( LandPalette == 1 )
		c = ramp( t, vec3( 0.20, 0.50, 0.25 ), vec3( 0.62, 0.68, 0.34 ), vec3( 0.68, 0.50, 0.30 ), vec3( 0.56, 0.45, 0.42 ),
		          vec3( 0.97, 0.97, 0.97 ) );
	else
		c = ramp( t, vec3( 0.22, 0.12, 0.50 ), vec3( 0.10, 0.45, 0.85 ), vec3( 0.15, 0.78, 0.55 ), vec3( 0.97, 0.84, 0.25 ),
		          vec3( 0.95, 0.30, 0.18 ) );
	return linearFromSrgb3( c );
}

/// How long ago, in passes, the sweep last crossed height `at`. The same
/// function as SweepClock::AgeOf.
float ageOf( float at )
{
	if( SweepMode == 1 )
		return fract( SweepPos - at );
	if( SweepMode == 2 )
		return fract( at - SweepPos );
	if( SweepMode == 3 )
	{
		if( SweepDir >= 0 )
			return at <= SweepPos ? SweepPos - at : SweepPos + at;
		return at >= SweepPos ? at - SweepPos : ( 1.0 - at ) + ( 1.0 - SweepPos );
	}
	return abs( SweepPos - at );
}

/// A line at height `at`: x its strength (0..1), y the extra light it gives
/// the glow for having just been crossed.
vec2 sweepStrength( float at )
{
	if( SweepMode == 0 )
		return vec2( 1.0, 0.0 );
	float age   = ageOf( at );
	float trail = Trail > 0.0 ? clamp( 1.0 - age / Trail, 0.0, 1.0 ) : 0.0;
	float burn  = SweepGlow * exp( -age * Lines / 1.5 );
	return vec2( clamp( max( FloorLevel, trail * trail ) + burn, 0.0, 1.0 ), burn );
}

void over( inout vec4 acc, vec3 colour, float alpha )
{
	alpha = clamp( alpha, 0.0, 1.0 );
	acc   = vec4( colour * alpha, alpha ) + acc * ( 1.0 - alpha );
}

void main()
{
	vec2 p = gl_FragCoord.xy - ViewOrigin;
	if( WobbleAmount > 0.0 )
	{
		vec2 n = vec2( valueNoise2( p / WobbleScale, 0x2545f491u ), valueNoise2( p / WobbleScale + vec2( 31.7, 17.3 ), 0x9e3779b9u ) );
		p += ( n * 2.0 - 1.0 ) * WobbleAmount;
	}
	vec4 c  = project( p );
	vec4 xp = project( p + vec2( 1.0, 0.0 ) ), xm = project( p - vec2( 1.0, 0.0 ) );
	vec4 yp = project( p + vec2( 0.0, 1.0 ) ), ym = project( p - vec2( 0.0, 1.0 ) );

	float world = c.z;
	if( Projection == 1 )
	{
		float radius = GLOBE_FILL * Zoom * 0.5 * Raster.y;
		world        = clamp( radius - length( p - 0.5 * Raster ) + 0.5, 0.0, 1.0 );
	}
	if( world <= 0.0 )
	{
		picture = vec4( 0.0 );
		light   = vec4( 0.0 );
		return;
	}

	//The level of detail: texels under a pixel, plus Smooth.
	vec2 du   = texDelta( xp.xy, xm.xy ) * 0.5 * HeightSize;
	vec2 dv   = texDelta( yp.xy, ym.xy ) * 0.5 * HeightSize;
	float lod = clamp( log2( max( max( length( du ), length( dv ) ), 1.0e-6 ) ) + SmoothBias, 0.0, HeightTop );

	vec2 hc     = heightAt( c.xy, lod );
	float lc    = levelOf( hc.x );
	float lxp   = levelOf( heightAt( xp.xy, lod ).x ), lxm = levelOf( heightAt( xm.xy, lod ).x );
	float lyp   = levelOf( heightAt( yp.xy, lod ).x ), lym = levelOf( heightAt( ym.xy, lod ).x );
	vec2 grad   = 0.5 * vec2( lxp - lxm, lyp - lym );
	float g     = max( length( grad ), 1.0e-6 );
	float hi    = max( max( max( lxp, lxm ), max( lyp, lym ) ), lc );
	float lo    = min( min( min( lxp, lxm ), min( lyp, lym ) ), lc );
	float slope = hi - lo > 1.0e-5 || ( TestFlags & 1 ) != 0 ? 1.0 : 0.0;
	float measured = smoothstep( 0.35, 0.65, hc.y );
	bool sea       = HasSea == 1 && lc < 0.0;

	vec4 acc       = vec4( 0.0 );
	vec3 glowLight = vec3( 0.0 );

	if( Projection == 1 )
		over( acc, DiscColour, 1.0 );
	if( sea )
		over( acc, SeaFillColour, SeaFill * measured );

	//Tints: each band between two contours in its height's colour.
	float lf = lc - FlowPhase;
	if( Tints > 0.0 )
	{
		float band = ( floor( lf ) + 0.5 + FlowPhase ) / Lines + Datum;
		over( acc, palette( clamp( band, 0.0, 1.0 ) ), Tints * measured );
	}

	float sweepLevel = ( SweepPos - Datum ) * Lines;
	if( SweepMode != 0 && Flood > 0.0 && lc < sweepLevel )
		over( acc, FloodColour, Flood * measured );

	//Shade: hill-shading lit from the upper left, in the lines' colour --
	//light faces lit on a dark ground, shadowed faces inked on a pale one.
	if( Shade > 0.0 )
	{
		vec3 n    = normalize( vec3( -grad * 6.0, 1.0 ) );
		float lit = clamp( dot( n, normalize( vec3( -1.0, 1.0, 1.2 ) ) ), 0.0, 1.0 );
		over( acc, LineColour, Shade * 0.45 * ( DarkGround == 1 ? lit : 1.0 - lit ) * measured );
	}

	//The contours.
	float spacing    = 1.0 / g;
	float minSpacing = max( 2.5, 1.5 * LineWidth );
	{
		float k       = floor( lf + 0.5 );
		float d       = ( TestFlags & 8 ) != 0 ? abs( lf - k ) * 20.0 : abs( lf - k ) / g;
		int ki        = int( k );
		bool isIndex  = IndexEvery > 0 && ki - IndexEvery * int( floor( float( ki ) / float( IndexEvery ) ) ) == 0;
		float width   = isIndex ? LineWidth * 1.8 : LineWidth;
		float density = smoothstep( 0.6 * minSpacing, minSpacing, spacing * ( isIndex ? float( IndexEvery ) : 1.0 ) );
		float at      = ( k + FlowPhase ) / Lines + Datum;
		vec2 strength = sweepStrength( at );
		bool below    = HasSea == 1 && k + FlowPhase < 0.0;
		vec3 colour   = below ? SeaLineColour : ( isIndex ? IndexColour : LineColour );
		colour        = mix( colour, palette( clamp( at, 0.0, 1.0 ) ), HeightColour );
		float alpha   = coverage( d, width ) * density * slope * measured * strength.x * ( below ? Depths : 1.0 );
		over( acc, colour, alpha );
		glowLight += colour * alpha + AccentColour * alpha * strength.y;
	}

	//The coast: sea level, always, and at index weight.
	if( HasSea == 1 )
	{
		float d     = abs( lc ) / g;
		float alpha = coverage( d, LineWidth * 1.8 ) * slope * measured * sweepStrength( Datum ).x;
		over( acc, IndexColour, alpha );
		glowLight += IndexColour * alpha;
	}

	//The sweep itself: the contour at the sweep's height, in the accent.
	if( SweepMode != 0 )
	{
		float d     = abs( lc - sweepLevel ) / g;
		float alpha = coverage( d, LineWidth * 2.2 ) * slope * measured;
		float halo  = SweepGlow * 0.3 * exp( -0.5 * pow( d / ( 3.0 * LineWidth + 3.0 ), 2.0 ) ) * slope * measured;
		over( acc, AccentColour, halo );
		over( acc, AccentColour, alpha );
		glowLight += AccentColour * ( alpha * ( 1.0 + 3.0 * SweepGlow ) + halo );
	}

	picture = acc * world;
	light   = vec4( glowLight * world, 1.0 );
}
)";

//===========================================================================
// Glow: the map's light, a quarter of the raster, then a Gaussian.
//===========================================================================
const char* const kGlowDownFragment = R"(
uniform sampler2D Source;
uniform vec2 SourceSize;
out vec4 fragColour;
void main()
{
	//Each texel here is a 4x4 block there: four bilinear taps, each the
	//mean of a 2x2, centred on the block's four quarters.
	vec2 centre = gl_FragCoord.xy * 4.0;
	vec2 t      = 1.0 / SourceSize;
	vec4 s = texture( Source, ( centre + vec2( -1.0, -1.0 ) ) * t ) + texture( Source, ( centre + vec2( 1.0, -1.0 ) ) * t )
	       + texture( Source, ( centre + vec2( -1.0, 1.0 ) ) * t ) + texture( Source, ( centre + vec2( 1.0, 1.0 ) ) * t );
	fragColour = vec4( 0.25 * s.rgb, 1.0 );
}
)";

const char* const kGlowBlurFragment = R"(
uniform sampler2D Source;
uniform vec2 Step;
out vec4 fragColour;
void main()
{
	//Sigma 3 texels of the quarter raster: 12 pixels of the full one.
	const float weights[ 7 ] = float[]( 0.13298, 0.12579, 0.10648, 0.08065, 0.05467, 0.03315, 0.01799 );
	vec2 at  = gl_FragCoord.xy / vec2( textureSize( Source, 0 ) );
	vec3 sum = texture( Source, at ).rgb * weights[ 0 ];
	float norm = weights[ 0 ];
	for( int i = 1; i < 7; ++i )
	{
		sum += ( texture( Source, at + Step * float( i ) ).rgb + texture( Source, at - Step * float( i ) ).rgb ) * weights[ i ];
		norm += 2.0 * weights[ i ];
	}
	fragColour = vec4( sum / norm, 1.0 );
}
)";

//===========================================================================
// Composite: the ground (the style's paper or black, or the clip), the map
// over it, the glow added, out to sRGB.
//===========================================================================
const char* const kCompositeFragment = R"(
uniform sampler2D Picture;
uniform sampler2D Glow;
uniform sampler2D ClipIn;
uniform vec3 Ground;
uniform float GroundAlpha;
uniform float GlowAmount;
uniform int IsEffect;
uniform float ClipShow;
uniform float MixAmount;
uniform vec2 Raster;
uniform vec2 ViewOrigin;
out vec4 fragColour;

void main()
{
	vec2 p = gl_FragCoord.xy - ViewOrigin;
	vec4 c = texelFetch( Picture, ivec2( p ), 0 );
	vec3 g = GlowAmount > 0.0 ? texture( Glow, ( p + 0.5 ) / Raster ).rgb * GlowAmount : vec3( 0.0 );
	vec4 clip = vec4( 0.0 );
	vec4 base;
	if( IsEffect == 1 )
	{
		clip = texelFetch( ClipIn, ivec2( p ), 0 );
		base = vec4( mix( Ground, linearFromSrgb3( clip.rgb ), ClipShow ), 1.0 );
	}
	else
		base = vec4( Ground * GroundAlpha, GroundAlpha );
	vec4 o = c + base * ( 1.0 - c.a );
	o.rgb += g;
	o.a = clamp( o.a + max( max( g.r, g.g ), g.b ), 0.0, 1.0 );
	vec3 straight = o.a > 1.0e-5 ? o.rgb / o.a : vec3( 0.0 );
	vec3 s        = srgbFromLinear3( straight );
	if( IsEffect == 1 )
		fragColour = vec4( mix( clip.rgb, s, MixAmount ), mix( clip.a, 1.0, MixAmount ) );
	else
		fragColour = vec4( s * o.a, o.a );
}
)";

std::string Assemble( std::initializer_list< const char* > pieces )
{
	std::string text = std::string( kVersion ) + kCommon;
	for( const char* piece : pieces )
		text += piece;
	return text;
}

} // namespace isohypse::shaders
