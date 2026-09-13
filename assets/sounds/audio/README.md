# Audio assets

`sounds.json` is the only audio manifest. Every path is relative to `assets/`.
An event may contain any number of variants; one is selected randomly and its
pitch is varied slightly on every playback.

Recommended layout (paths below are still written relative to `assets/` in
the manifest):

```
sounds/audio/blocks/grass/step1.ogg
sounds/audio/blocks/grass/step2.ogg
sounds/audio/ui/click1.ogg
sounds/audio/ui/hover1.ogg
sounds/audio/water/swim1.ogg
sounds/audio/water/ambient1.ogg
sounds/audio/items/pickup1.ogg
sounds/audio/music/music1.ogg
```

Then list those paths in the corresponding arrays in `sounds.json`. Empty
arrays are valid and silent. Missing files are skipped with a warning instead
of stopping the game. OGG and WAV are the preferred formats supported by
raylib.

UI and water hooks are already active. Put file paths into `ui.click`,
`ui.hover`, `water.swim`, and `water.ambient`. Swimming variants play only
while the player moves through water; water ambience uses a slower random
interval while the hitbox touches water.

Item pickup uses `drop["pick up"]`. One source file is automatically played
at one of four nearby pitch values, so it produces four audible variations
without duplicating the asset.
