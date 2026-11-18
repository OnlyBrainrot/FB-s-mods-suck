# Auto Deco

A Geode mod for Geometry Dash 2.2091 (Geode v6.0.0-alpha.2). Select solid blocks in the editor, press one button, and it adds fill, outline and glow around them.

## How it works

- The selected blocks are snapped onto a 30-unit grid (the grid phase is taken from the selection, so off-grid structures work).
- `src/deco_core.hpp` decides where pieces go: an outline line on every exposed side, a corner dot in every concave corner, and glow in the surrounding empty cells (straight, outside-corner or inside-corner depending on the neighbours).
- The mod does not hard-code which way a decoration sprite faces. It renders each piece once, measures where its pixels are, and rotates and offsets the piece so the right side touches the block. That is why custom object IDs work.
- The result is pasted as a single undo step.

Slopes, slabs and blocks rotated off-axis are skipped.

## Node IDs

Not required. If the Node IDs mod is installed, the button joins the copy/paste menu's layout; otherwise it is placed just left of the Copy button's row.

## Object IDs used

| Set | Edge | Outside corner | Inside corner |
| --- | --- | --- | --- |
| Outline: Classic | 468 | - | 472 |
| Outline: Neon | 1191 | - | 1194 |
| Outline: Thick | 1202 | - | 1205 |
| Glow: Small | 1292 | 1009 | 1010 |
| Glow: Medium | 503 | 504 | 505 |
| Glow: Large | 1011 | 1012 | 1013 |

Fill is object 211. All of these can be overridden in the settings.

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
