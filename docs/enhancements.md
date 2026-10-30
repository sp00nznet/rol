# Enhancements

Everything here is optional, sits in the host (`src/runtime/enhance.c`), and
changes nothing in the game's install. **F10** opens the menu in the game;
settings live in `rol.ini` next to `rol.exe` and are written there with their
defaults the first time the game starts.

| Menu | Setting (`rol.ini`) | Default | How |
|---|---|---|---|
| Display | `fullscreen` | 1 | a borderless window over the monitor, not an exclusive mode |
| | `render` | screen size; 1920x1080 on a 4K screen | see *Render scale* |
| | `vsync` | 1 | the device's present interval |
| Graphics | `anisotropy` | 16 | see *Texture filtering* |
| | `shadows` | 4096 | the game's `shadowmap_size`, and its hardware cap |
| | `msaa` | 4 | the game's own `multisample_passes` |
| Picture | `sharpen` | 1 (2 on a 4K screen) | see *Post-processing* |
| | `vivid` | 0 | |
| Camera | `zoom` | 1 (farther) | see *Zoom* |
| Stats | `fps` | 0 | frame rate and time of day, top right |
| | `game_clock`, `score`, `health_bars`, `gather_rates` | 1, 0, 0, 0 | the game's own options |
| Photo mode | | | hide the interface (the game's Alt+Z); **F9** saves a PNG to Pictures\Rise of Legends |

Settings the game reads only when it starts are marked "(restart)"; choosing
one offers to restart the game.

## The game's own settings

Rise of Legends keeps its settings in `Documents\My Games\Rise Of Legends`:
`personal.ini` (window size, `AllowAllResolutions`, VSync, `SkipIntroVideos`)
and `<name>\<name>.profile`, an XML file whose attributes hold the rest. The
`GRAPHICS` element has `graphics_resolution`, `shadowmap_size` and
`multisample_passes`; a per-machine feature list records what the 2006
hardware probe found, including `ShadowMapMaxSize` 2048. At launch the host
writes the menu's choices into both files (`profile_apply`), so the game starts
with them; the game's own options screen still works, but `rol.ini` wins at the
next launch.

The game supports more than it lets the options screen show: antialiasing
(`multisample_passes` 4 gives a 4x multisampled device), resolutions past
1600x1200, and shadow maps past 2048 (8192 works).

## Render scale

The interface is drawn at a fixed pixel size. At 3840x2160 the main menu is a
strip in the top-left corner. So the game renders at a smaller size (by default
1920x1080 on a 4K screen, exactly half), its window covers the monitor, and the
present stretches the back buffer over it: the interface comes out 2x, the 3D
is sharpened by the post-process. The game thinks in back-buffer pixels, so
`ScreenToClient` and the window's mouse messages are scaled back to them, and
its `SetWindowPos` calls keep the window over the monitor.

## Texture filtering

The game sets `MINFILTER` LINEAR and never sets `MAXANISOTROPY`. The host hooks
`IDirect3DDevice9::SetSamplerState`: a LINEAR minification filter becomes
ANISOTROPIC at the chosen level; POINT samplers (lookups) are left alone. The
ground at the RTS camera's angle is where it shows.

## Post-processing

The game ships its shaders compiled (`BIGS/fxobjects.big`, 117 effects, no
source), so a better shader cannot be dropped in. Instead the host draws over
the finished frame: each `Present` copies the back buffer into a texture and
draws it back through one ps_2_0 pixel shader, compiled at start with Windows'
`d3dcompiler_47.dll`. Sharpening is AMD CAS's edge-aware weight (strong where
contrast is low, nothing on edges that are already hard); vivid colour is a
saturation and contrast lift.

## Zoom

The camera's range is data: `.\data\gamecamera.xml` in `multiplayer_data.big`
(8 zoom levels from 2000 to 7500 units). Fog is per environment in
`.\art\terrain\tilesets\default_tilesets.xml` in `art.big`, and a farther camera
would look into it. Loose files do not override the archives (the loader asks
the archives first), so `tools/make_enhanced_data.py` rewrites both archives,
from the player's own install, into `rol_data\far\` (1.5x: 13 levels to 11250)
and `rol_data\farthest\` (2x: 18 levels to 15000), with fog pushed out by the
same factor, and the host's `CreateFileA/W` shims open those in place of the
originals. `tools/bigtool.py` reads and writes the archives and their binary
XML.

## Photo mode

The game already has the parts: Alt+Z hides the whole interface, Ctrl+C
changes the camera mode, Ctrl+Alt+S is its own screenshot (all in
`data\keymap.xml`). The menu sends Alt+Z as real keyboard input, since the game
reads keys through DirectInput. F9 is the host's: the frame, before the
overlay, as a PNG through WIC, at the render size.
