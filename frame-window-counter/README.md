# Frame Window Counter

A Geode mod for Geometry Dash 2.2091 (Geode v6.0.0-alpha.2). Give it a macro and it:

1. **counts the frame window of every input** - how many ticks (240 per second) the input could happen on and the run still survives,
2. **plays the macro with an overlay** - a circle and the window size at each input, plus a running tally,
3. **renders that to a video** through ffmpeg (Windows and macOS).

## Using it

1. Mod settings -> **Macro file**: pick the macro.
2. **Mode -> Count windows**, then open the level. A progress line shows at the top. When it finishes, the windows are written next to the macro as `<macro>.fwc.json` and the mode switches itself to **Play macro**.
3. Open the level (or let it restart) to watch the macro with circles and the tally.
4. **Mode -> Render video**, then open the level: the run is written to `renders/` inside the mod's save folder, with the level's song added. Set the **ffmpeg** setting if ffmpeg is not on your PATH.

**Overlay only** shows the circles and tally while a different bot plays the macro.
Set **Mode -> Off** to play normally.

## Macro formats

| Format | Notes |
| --- | --- |
| `.gdr2` / `.gdr` binary (files starting with `GDR`) | GDR2, including platformer inputs and bot extensions (skipped) |
| `.gdr` MessagePack / GDR `.json` | legacy GDR, `inputs` with `frame`, `btn`, `2p`, `down` |
| `.slc` version 3 (files starting with `SLC3RPLY`) | Silicate; plain, repeated and "swift" input sections |
| `.slc` version 2 (files starting with `SILL`) | Silicate |
| `.fwc.json` | this mod's own format: inputs in ticks with their `window` |

The format is detected from the file's contents, so the extension does not matter.

- A macro that names a level ID only runs on that level (GDR only; SLC files carry no level ID).
- SLC tick-rate changes are converted to 240 TPS ticks by time. If an SLC file contains restarts, only the inputs after the last restart are used. Death and bugpoint markers are ignored.
- GDR2 and SLC parsing is tested against files written by the reference libraries ([GDReplayFormat](https://github.com/maxnut/GDReplayFormat), [slc](https://github.com/silicate-bot/slc)); see `test/fixtures.hpp`.

## How windows are counted

For each input, with every other input left where it is:

- move it one tick later, then two, ... until the run dies; then the same going earlier;
- a position counts if the run survives until **Survival check** ticks (default 48 = 0.2 s) after the following input;
- an input may not cross the previous or next input on the same button;
- the window is the number of surviving positions, including the original, capped at **Stop counting at** (default 10).

This is one reasonable definition, not the only one. A person counting by hand may allow later inputs to adjust, which this does not. Change **Survival check** to make it stricter or looser, or edit the numbers in the `.fwc.json`.

To keep it fast the counter runs in practice mode and replays each test from a recent checkpoint. Every checkpoint is verified by replaying the unchanged macro from it; if that dies, the checkpoint is dropped and an earlier one is used. Turn **Use practice checkpoints** off to replay everything from the level start (slow, safest).

If the macro does not survive when played back unchanged, counting stops and says which tick it died on.

## The precision number

The tally footer shows a precision estimate: assuming timing errors are normally distributed, it is the precision (1 / standard deviation, per second) at which the expected time to beat the level is 24 hours. Higher means the level demands tighter play. It uses only the counted windows and their times; treat it as a rough guide.

## Limits

- Playback is tick-accurate at 240 TPS. Macros that rely on sub-tick input mods or a different physics rate will not replay correctly.
- Start positions are not supported.
- The video contains the level's song but not in-game sound effects.
- No iOS build: the 2.2091 bindings lack iOS addresses for functions the counter needs.
- This has been compiled and the logic unit-tested, but not yet run in the game.

## Layout

- `src/fwc_core.hpp` - macro parsing, the counter, tallies. No game dependency.
- `src/main.cpp` - hooks, playback, overlay, rendering.
- `test/core_test.cpp` - tests for the core against a toy level.
- `test/fixtures.hpp` - sample macros written by the reference GDR and SLC libraries.

```
c++ -std=c++20 -I src test/core_test.cpp -o core_test && ./core_test
```

## Build

Push this folder to a GitHub repository. The included workflow builds Windows, macOS and Android and uploads the `.geode` file as the "Build Output" artifact. Or install the [Geode SDK and CLI](https://docs.geode-sdk.org/getting-started/) and run `geode build`.

Change `id` and `developer` in `mod.json` to your own before publishing.

## License

MIT
