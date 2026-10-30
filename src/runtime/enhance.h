/* The enhancements and their F10 menu: see enhance.c. */
#ifndef ROL_ENHANCE_H
#define ROL_ENHANCE_H
#include <windows.h>
#include <d3d9.h>
#include "native32.h"

/* Before the guest runs: read rol.ini, write the game's own settings
 * (resolution, shadows, MSAA, its stat toggles) into its profile. host_dir is
 * the folder rol.exe is in (rol.ini and rol_data\ live there); cwd is the
 * directory the host was started from (a restart relaunches there). */
void enhance_init(const char* host_dir, int headless);

/* hl_CreateDevice / Reset: settle the present parameters (windowed, vsync)
 * before the device is made; after, take the device's vtable hooks. */
void enhance_fix_params(D3DPRESENT_PARAMETERS* pp);
void enhance_device_created(IDirect3DDevice9* dev, HWND window);

/* Every Present, before the frame goes out: post-processing, the overlay,
 * a pending screenshot. */
void enhance_present(IDirect3DDevice9* dev);

/* The back buffer, or with antialiasing a resolved copy of it, for reading
 * back (--record, screenshots). AddRef'd; NULL on failure. */
IDirect3DSurface9* enhance_readable(IDirect3DDevice9* dev, IDirect3DSurface9* bb);

/* Guest shims: CreateFileA/W (the zoom archives), ScreenToClient and
 * SetWindowPos (render scale in a fullscreen window). */
#define ENHANCE_NSHIMS 4
extern native32_shim_t g_enhance_shims[ENHANCE_NSHIMS];

#endif
