#include "SweepClock.h"

#include <algorithm>
#include <cmath>

namespace isohypse
{
namespace
{
double fract( double x )
{
	return x - std::floor( x );
}
} // namespace

void SweepClock::Advance( double dt, const SweepInputs& in )
{
	currentMode = in.mode;
	manualLevel = std::clamp( in.level, 0.0, 1.0 );

	//The plugin's own count always runs, so a host that stops sending its
	//tempo halfway through a show hands over without a jump.
	const int bars = std::max( in.bars, 0 );
	if( bars == 0 )
		ownPasses += dt / std::max( in.seconds, 1.0e-3 );
	else
	{
		const double barSeconds = 4.0 * 60.0 / std::max( in.fallbackBpm, 1.0 );
		ownPasses += dt / ( bars * barSeconds );
	}

	if( bars > 0 && in.haveBeat )
	{
		//Restart puts the start of a pass on the downbeat of the bar it is
		//pressed in; until then passes start on the host's bar 0.
		if( in.restart )
			barOrigin = std::floor( in.barCount );
		barLocked = true;
		passes    = ( in.barCount - barOrigin ) / bars;
		ownPasses = passes;
	}
	else
	{
		barLocked = false;
		if( in.restart )
			ownPasses = 0.0;
		passes = ownPasses;
	}
	fold( in.mode, manualLevel );
}

void SweepClock::fold( Sweep mode, double level )
{
	switch( mode )
	{
	case Sweep::Rise:
		position  = fract( passes );
		direction = 1;
		break;
	case Sweep::Fall:
		position  = 1.0 - fract( passes );
		direction = -1;
		break;
	case Sweep::Bounce:
	{
		const double q = 2.0 * fract( 0.5 * passes );
		position       = q < 1.0 ? q : 2.0 - q;
		direction      = q < 1.0 ? 1 : -1;
		break;
	}
	case Sweep::Manual:
		position  = level;
		direction = 0;
		break;
	default:
		position  = 0.0;
		direction = 0;
		break;
	}
}

double SweepClock::AgeOf( Sweep mode, double position, int direction, double at )
{
	switch( mode )
	{
	case Sweep::Rise: return fract( position - at );
	case Sweep::Fall: return fract( at - position );
	case Sweep::Bounce:
		//Climbing: what is below was crossed on the way up; what is above
		//was last crossed on the way down, before the bottom. Falling, the
		//mirror of that.
		if( direction >= 0 )
			return at <= position ? position - at : position + at;
		return at >= position ? at - position : ( 1.0 - at ) + ( 1.0 - position );
	case Sweep::Manual: return std::fabs( position - at );
	default: return 0.0;
	}
}

} // namespace isohypse
