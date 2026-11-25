# SubTick

Experimental Windows Geode mod for timestamp-based Geometry Dash input scheduling.

SubTick captures Geode's timestamped PlayerButtonCommand entries, maps them onto the game's actual player physics updates, and splits an input-bearing physics tick at the event timestamp.

## Current target

- Geometry Dash: 2.2081 (Windows)
- Geode SDK: 5.8.2
- C++: 23
- Buffered inputs: 2048

Other platforms and game versions are not currently declared supported.

## Modes

- **Compatibility Subtick** (default): splits only states where the player can be safely advanced in segments according to the current compatibility rules.
- **Exact Subtick**: attempts to split every input-bearing player tick. This is experimental and can change collision/integration behavior.

SubTick does not raise the game's physics rate and does not claim to remove device polling, display scanout, or render latency.

## Build

Use the Geode SDK and CLI versions listed above. The GitHub Actions workflow builds the Windows mod and independently runs the portable timeline tests.

Core tests can be run without the Geode SDK:

    cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
    cmake --build build-tests --config Release --parallel
    ctest --test-dir build-tests --output-on-failure

## Safety and compatibility

- Do not enable SubTick alongside another input-timing mod.
- Invalid timestamps, timing drift, and queue overflow switch back to the native input path.
- Pausing, leaving a level, completing a level, and resetting a level reset the scheduler.
- The timeline uses fixed-capacity storage; normal scheduling does not allocate.
- Physics splitting is not guaranteed to be bit-identical to vanilla.

## Verification status

The deterministic core tests cover ordering, same-timestamp inputs, tick boundaries, late inputs, invalid timing, queue capacity, and frame-rate invariance from 15 to 3000 FPS.

A passing CI build proves compilation, not that the mod has been runtime-tested in Geometry Dash. No live gameplay acceptance results or end-to-end latency claims are published yet.

## Credits

The player-update hook structure was developed with reference to the MIT-licensed Click Between Ticks project by wayago. See THIRD_PARTY_NOTICES.md for attribution. SubTick's timeline core and tests are maintained in this repository.
