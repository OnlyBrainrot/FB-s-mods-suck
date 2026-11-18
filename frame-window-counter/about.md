# Frame Window Counter

Load a macro and see the **frame window** of every input: how many ticks (at 240 per second) that click or release could have happened on and still survived.

## What it does

- **Counts the windows for you.** It replays the level with one input moved a tick at a time and records how many positions survive.
- **Shows them while the macro plays.** A circle with the window size appears where each input happens, coloured by how tight it is, with a running tally in the corner (1f, 2f, 3f, ...).
- **Renders a video** of the run with the overlay, through ffmpeg (Windows and macOS).

## How to use

1. Open the mod's settings and pick your **Macro file**.
2. Set **Mode** to **Count windows** and open the level. Wait for the progress to reach 100%. The windows are saved next to the macro as `<macro>.fwc.json` and the mode switches to **Play macro**.
3. Watch it back, or set **Mode** to **Render video** and open the level again. The video is saved in the mod's save folder under `renders`.

Set **Mode** to **Off** when you want to play normally.

## Supported macro files

`.gdr` and `.gdr2`, GDR `.json`, Silicate `.slc` (versions 2 and 3), and this mod's own `.fwc.json`.

## Good to know

- The mod plays the macro itself. If the macro dies early, try a **Frame offset** of 1 or -1.
- Start from the beginning of the level, not a start position.
- You can edit the `window` numbers in the `.fwc.json` file by hand to override a count.

Free and open source (MIT).
