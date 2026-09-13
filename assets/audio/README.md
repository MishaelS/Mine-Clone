# Audio assets

`sounds.json` is the only audio manifest. Every path is relative to `assets/`.
An event may contain any number of variants; one is selected randomly and its
pitch is varied slightly on every playback.

Recommended layout:

```
audio/blocks/grass/step1.ogg
audio/blocks/grass/step2.ogg
audio/blocks/grass/hit1.ogg
audio/blocks/grass/break1.ogg
audio/ambient/ambient1.ogg
audio/music/music1.ogg
```

Then list those paths in the corresponding arrays in `sounds.json`. Empty
arrays are valid and silent. Missing files are skipped with a warning instead
of stopping the game. OGG and WAV are the preferred formats supported by
raylib.
