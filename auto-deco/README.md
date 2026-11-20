# Auto Deco

A Geode mod for Geometry Dash 2.2091 (Geode 6.0.0-alpha.3). Pick a style, preview it on your level, keep it.

## Using it

Open a level in the editor and press **Auto Deco** in the pause menu (or the pink button next to Copy). Choose a style with the arrows, adjust its four colours (Background, Blocks, Main, Details), choose what to add, and press **Preview** or **Decorate**. A preview shows a bar with **Keep**, **Undo** and **Other style**.

- **Blocks**: fill (object 211), outline and glow on every exposed edge and corner.
- **Details**: decoration on top faces, under ceilings (only where there are a few empty cells below), out of walls, and along the ground.
- **Background**: objects scattered behind the level on B4.
- **Colours**: set as the level's starting colours, plus colour triggers at the start as a backup.
- **Song pulses**: pulse triggers on each beat for the style's chosen channels, and on every 4th beat for the background.
- **Only selection**: decorate only the selected blocks instead of the whole level.
- **Shuffle**: a different random arrangement of details and background.
- **Remove**: deletes everything Auto Deco added (undoable).

Everything added is tagged with the **Auto Deco group** (9999 by default), which is how it replaces and removes its own work. It uses colour channels 990-995 by default. Change both in the settings if your level already uses them.

## Song sync

Beat source (settings): **Auto** uses the level's guidelines if there are at least 8, otherwise it analyses the song. Analysis decodes the song with FMOD on a background thread and runs an onset-envelope beat tracker (`src/beats.hpp`). Fast drum and bass is sometimes detected at half tempo; use **Fixed tempo** for that.

## Styles

Neon Night, Inferno, Frostbite, Overgrowth, Factory, Void, Synthwave, Monochrome, Candy, Abyss, Golden Hour, Cyber Grid, Starfield, Toxic, Royal, Sakura, Blackout, Storm, Retro Arcade, Bloodmoon. They are defined in `src/styles.hpp`; adding one is a few lines.

Details only use objects the game reports as pure decoration, so nothing with a hitbox is ever added.

## Limits

- Only full-size solid blocks on the grid are decorated; slopes, slabs, spikes and rotated blocks are skipped.
- Not yet tested in the game.

## Tests

The placement, style and beat code has no game dependency:

```
c++ -std=c++20 -O2 -I src test/core_test.cpp -o t1 && ./t1
c++ -std=c++20 -O2 -I src test/build_test.cpp -o t2 && ./t2
c++ -std=c++20 -O2 -I src test/beats_test.cpp -o t3 && ./t3
```

## Build

Push this folder to a GitHub repository. The included workflow builds Windows, macOS and Android and uploads the `.geode` file as the "Build Output" artifact.

To build locally, install the [Geode SDK and CLI](https://docs.geode-sdk.org/getting-started/) and run `geode build`.

## iOS

Not built for iOS on 2.2091: the bindings don't yet have iOS addresses for `EditorUI::getSelectedObjects`, `EditorUI::pasteObjects` and `GameObject::createWithKey`. Once they do, add `"ios": "2.2091"` back to `mod.json` and an iOS row to the workflow.

## Test the placement logic

The core has no game dependency:

```
c++ -std=c++20 -I src test/core_test.cpp -o core_test && ./core_test
```

## Before publishing

Change `id` and `developer` in `mod.json` to your own.

## License

MIT
