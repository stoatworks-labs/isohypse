#pragma once

#include "Controls.h"

namespace isohypse
{
/**
    Where the sweep is: a position in the world's range of height, 0 at the
    lowest point and 1 at the highest, and which way it is going.

    It counts PASSES, in double: one pass is one climb (or fall) through the
    whole range. Free, a pass takes Sweep Time. Locked to bars, the count is
    the host's bars (counted from its bar phase wrapping) plus the phase,
    over the bars in a pass -- so a pass starts on a downbeat, and two layers
    set to the same bars agree, until Restart moves the start to the bar it
    is pressed in. A host that sends no tempo gets 120 BPM in 4/4, on the
    plugin's own clock.

    The shader needs, for any contour, how long ago the sweep last crossed it
    (Trail fades a line by that age). `AgeOf` is that function on the CPU,
    the shader's `ageOf` is the same function in GLSL, and the harness holds
    them to each other.
*/
struct SweepInputs
{
	Sweep mode       = Sweep::Rise;
	double seconds   = 8.0;///< Sweep Time, for Free
	int bars         = 0;  ///< 0 Free, else bars per pass
	double level     = 0.5;///< Manual
	bool restart     = false;
	bool haveBeat    = false;
	double barCount  = 0.0;///< the host's bars so far, whole bars plus the phase
	double fallbackBpm = 120.0;
};

class SweepClock
{
public:
	void Advance( double dt, const SweepInputs& in );

	/// 0 (lowest) .. 1 (highest).
	double Position() const
	{
		return position;
	}
	/// +1 climbing, -1 falling, 0 for Manual and Off.
	int Direction() const
	{
		return direction;
	}
	/// Passes so far: the count before Rise, Fall or Bounce fold it.
	double Passes() const
	{
		return passes;
	}
	void SetPassesForTest( double value )
	{
		passes = value;
		ownPasses = value;
		fold( currentMode, manualLevel );
	}

	/// How long ago, in passes, the sweep last crossed height `at`
	/// (0..1), for the sweep at `position` going `direction` in `mode`.
	/// Manual has no history: the age is the distance from the level.
	static double AgeOf( Sweep mode, double position, int direction, double at );

private:
	void fold( Sweep mode, double level );

	double passes = 0.0, ownPasses = 0.0, barOrigin = 0.0;
	bool barLocked = false;
	double position = 0.0;
	int direction   = 1;
	Sweep currentMode   = Sweep::Rise;
	double manualLevel  = 0.5;
};

} // namespace isohypse
