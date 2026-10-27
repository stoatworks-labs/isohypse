#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK and
# the maps are symlinked, not copied), built arm64-only, with only the named
# check run.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
MUTANTS=(
	"source/Shaders.cpp|abs( lf - k ) * 20.0 : abs( lf - k ) / g;|abs( lf - k ) * 20.0 : abs( lf - k ) * g;|--width|GLSL: a contour's distance times the gradient, / -> *"
	"source/Shaders.cpp|return clamp( 0.5 * w + 0.5 - d, 0.0, 1.0 ) * min( width, 1.0 );|return clamp( 0.5 * w + 0.6 - d, 0.0, 1.0 ) * min( width, 1.0 );|--width|GLSL: the line's box filter a tenth too wide, 0.5 -> 0.6"
	"source/Shaders.cpp|vec2 h0 = ( i - 0.5 + w1 / g0 ) / size;|vec2 h0 = ( i - 0.4 + w1 / g0 ) / size;|--lines|GLSL: the B-spline's first tap a tenth of a texel off, 0.5 -> 0.4"
	"source/Shaders.cpp|		return fract( SweepPos - at );|		return fract( SweepPos + at );|--trail|GLSL: Rise's age, - -> +"
	"source/Shaders.cpp|return at <= SweepPos ? SweepPos - at : SweepPos + at;|return at <= SweepPos ? SweepPos - at : SweepPos - at;|--trail|GLSL: Bounce's age above a climbing sweep, + -> -"
	"source/Shaders.cpp|		float lon = Centre.x + q.x * 90.0 / Zoom;|		float lon = Centre.x - q.x * 90.0 / Zoom;|--geo|GLSL: the map mirrored east to west, + -> -"
	"source/Shaders.cpp|return vec4( lon / TAU + 0.5, 0.5 - lat / PI, on, z );|return vec4( lon / TAU + 0.5, 0.5 + lat / PI, on, z );|--geo|GLSL: the globe upside down, - -> +"
	"source/Shaders.cpp|		fragColour = vec4( s * o.a, o.a );|		fragColour = vec4( s / o.a, o.a );|--alpha|GLSL: the source's output not premultiplied, * -> /"
	"source/Isohypse.cpp|const double rasterK = height / 1080.0;|const double rasterK = height / 1580.0;|--width|C++: Line Width scaled to the wrong raster, 1080 -> 1580"
	"source/Isohypse.cpp|b = -latDeg * kPi / 180.0|b = +latDeg * kPi / 180.0|--geo|C++: the globe tilted the wrong way, - -> +"
	"source/Isohypse.cpp|if( lastBarPhase >= 0.0 && phase < lastBarPhase - 0.5 )|if( lastBarPhase >= 0.0 && phase < lastBarPhase + 0.5 )|--clock|C++: every step back of the bar phase a new bar, - -> +"
	"source/SweepClock.cpp|ownPasses += dt / std::max( in.seconds, 1.0e-3 );|ownPasses += dt * std::max( in.seconds, 1.0e-3 );|--clock|C++: Sweep Time a rate, / -> *"
	"source/SweepClock.cpp|case Sweep::Fall: return fract( at - position );|case Sweep::Fall: return fract( at + position );|--trail|C++: the CPU's age of a Fall, - -> +"
)

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
	ln -s "$REPO/external/stb" "$tree/external/stb"
	ln -s "$REPO/data" "$tree/data"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target ihtest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	if "$tree/build/ihtest" "$check" >"$WORK/log" 2>&1; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant:\n' "$check"
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
