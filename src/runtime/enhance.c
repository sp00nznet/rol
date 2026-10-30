/*
 * Enhancements, and the F10 menu that drives them. Settings live in rol.ini
 * next to rol.exe.
 *
 *   Display        fullscreen (a borderless window over the monitor) or a
 *                  window; the game's render resolution. On a 4K screen the
 *                  game's interface is drawn at a fixed pixel size and is tiny,
 *                  so the default renders at half the screen and lets the
 *                  present stretch it (render scale), sharpened.
 *   Filtering      anisotropic texture filtering, which the game never sets:
 *                  each LINEAR minification filter becomes ANISOTROPIC.
 *   Picture        sharpening (contrast-adaptive) and a vivid colour grade,
 *                  a pixel shader over the finished frame.
 *   Game graphics  shadow-map size and multisample passes, both the game's
 *                  own settings, written into its profile at launch.
 *   Camera         zoom out 1.5x or 2x farther: the game reads its zoom range
 *                  and fog from its archives, so CreateFile opens rewritten
 *                  copies (rol_data\, tools/make_enhanced_data.py).
 *   Stats          an FPS / clock overlay, and the game's own clock, score,
 *                  health bars and gather rates (profile toggles).
 *   Photo mode     hides the interface (the game's own Alt+Z), F9 saves a
 *                  PNG of the frame to Pictures\Rise of Legends.
 *
 * Settings the game reads only at start say "(restart)"; the menu can
 * restart it.
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhance.h"

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

/* ------------------------------------------------------------- settings */

typedef struct { int w, h; } res_t;
static const res_t k_res[] = { { 0, 0 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 },
                               { 2560, 1440 }, { 3840, 2160 }, { 1024, 768 } };
#define NRES (int)(sizeof k_res / sizeof k_res[0])
static const char* const k_zoom_dir[] = { NULL, "far", "farthest" };

static struct {
    int fullscreen, res, vsync, aniso, msaa, shadow, sharpen, vivid, zoom, fps;
    int clock, score, health, gather;
} S;

static char g_ini[MAX_PATH], g_data[MAX_PATH], g_start_dir[MAX_PATH];
static int g_headless;
static int g_restart_needed;

static int ini_get(const char* k, int def) { return GetPrivateProfileIntA("rol", k, def, g_ini); }
static void ini_set(const char* k, int v) {
    char b[16];
    _snprintf(b, sizeof b, "%d", v);
    WritePrivateProfileStringA("rol", k, b, g_ini);
}

static void settings_load(void) {
    int tall = GetSystemMetrics(SM_CYSCREEN) > 1600;
    S.fullscreen = ini_get("fullscreen", 1);
    S.res = ini_get("render", tall ? 3 : 0);          /* 4K: 1080p, 2x */
    S.vsync = ini_get("vsync", 1);
    S.aniso = ini_get("anisotropy", 16);
    S.msaa = ini_get("msaa", 4);
    S.shadow = ini_get("shadows", 4096);
    S.sharpen = ini_get("sharpen", tall ? 2 : 1);
    S.vivid = ini_get("vivid", 0);
    S.zoom = ini_get("zoom", 1);
    S.fps = ini_get("fps", 0);
    S.clock = ini_get("game_clock", 1);
    S.score = ini_get("score", 0);
    S.health = ini_get("health_bars", 0);
    S.gather = ini_get("gather_rates", 0);
    if (S.res < 0 || S.res >= NRES) S.res = 0;
    if (S.zoom < 0 || S.zoom > 2) S.zoom = 0;
}

static void settings_save(void) {
    ini_set("fullscreen", S.fullscreen); ini_set("render", S.res); ini_set("vsync", S.vsync);
    ini_set("anisotropy", S.aniso); ini_set("msaa", S.msaa); ini_set("shadows", S.shadow);
    ini_set("sharpen", S.sharpen); ini_set("vivid", S.vivid); ini_set("zoom", S.zoom);
    ini_set("fps", S.fps); ini_set("game_clock", S.clock); ini_set("score", S.score);
    ini_set("health_bars", S.health); ini_set("gather_rates", S.gather);
}

/* The render size: the chosen one, capped to the screen; 0 = the screen. */
static res_t render_size(void) {
    res_t r = k_res[S.res], scr = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (!r.w || r.w > scr.w || r.h > scr.h) r = scr;
    return r;
}

/* --------------------------------------------- the game's own settings */

/* Documents\My Games\Rise Of Legends: personal.ini names the profile, which
 * is <nickname>\<nickname>.profile, XML with the settings as attributes. */
static int game_dir(char* out) {
    char docs[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs))) return 0;
    _snprintf(out, MAX_PATH, "%s\\My Games\\Rise Of Legends", docs);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

/* Replace the value of attribute `key` (the first ` key="`) in buf. */
static int set_attr(char** buf, const char* key, const char* val) {
    char pat[96];
    _snprintf(pat, sizeof pat, " %s=\"", key);
    char* at = strstr(*buf, pat);
    if (!at) return 0;
    at += strlen(pat);
    char* end = strchr(at, '"');
    if (!end) return 0;
    size_t head = (size_t)(at - *buf), vl = strlen(val), tail = strlen(end);
    if ((size_t)(end - at) == vl && !memcmp(at, val, vl)) return 0;
    char* nb = (char*)malloc(head + vl + tail + 1);
    memcpy(nb, *buf, head);
    memcpy(nb + head, val, vl);
    memcpy(nb + head + vl, end, tail + 1);
    free(*buf);
    *buf = nb;
    return 1;
}

static void profile_apply(void) {
    char dir[MAX_PATH], ini[MAX_PATH], name[MAX_PATH], path[MAX_PATH], v[64];
    res_t r = render_size();
    if (!game_dir(dir)) return;                    /* first run: the game makes it */
    _snprintf(ini, sizeof ini, "%s\\personal.ini", dir);
    _snprintf(v, sizeof v, "%d", r.w);  WritePrivateProfileStringA("DEFAULT", "Windowed Width", v, ini);
    _snprintf(v, sizeof v, "%d", r.h);  WritePrivateProfileStringA("DEFAULT", "Windowed Height", v, ini);
    WritePrivateProfileStringA("DEFAULT", "AllowAllResolutions", "1", ini);
    WritePrivateProfileStringA("DEFAULT", "VSync", S.vsync ? "1" : "0", ini);
    if (!GetPrivateProfileStringA("DEFAULT", "CurrentProfileFile", "", name, sizeof name, ini)) return;
    {
        char stem[MAX_PATH], *dot;
        _snprintf(stem, sizeof stem, "%s", name);
        if ((dot = strrchr(stem, '.'))) *dot = 0;
        _snprintf(path, sizeof path, "%s\\%s\\%s", dir, stem, name);
    }
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)calloc(1, n + 1);
    fread(buf, 1, n, f);
    fclose(f);
    int changed = 0;
    _snprintf(v, sizeof v, "%d x %d x 32", r.w, r.h);
    changed |= set_attr(&buf, "graphics_resolution", v);
    _snprintf(v, sizeof v, "%d", S.shadow);
    changed |= set_attr(&buf, "shadowmap_size", v);
    {   /* the hardware probe caps the shadow map at 2048 from 2006-era limits */
        char* m = strstr(buf, "name=\"ShadowMapMaxSize\"");
        if (m) {
            char* tmp = _strdup(m);
            int c = set_attr(&tmp, "value", v);
            if (c) {
                size_t head = (size_t)(m - buf);
                char* nb = (char*)malloc(head + strlen(tmp) + 1);
                memcpy(nb, buf, head);
                strcpy(nb + head, tmp);
                free(buf);
                buf = nb;
                changed = 1;
            }
            free(tmp);
        }
    }
    _snprintf(v, sizeof v, "%d", S.msaa);
    changed |= set_attr(&buf, "multisample_passes", v);
    changed |= set_attr(&buf, "show_game_clock", S.clock ? "1" : "0");
    changed |= set_attr(&buf, "show_score", S.score ? "1" : "0");
    changed |= set_attr(&buf, "health_bars", S.health ? "1" : "0");
    changed |= set_attr(&buf, "gather_rates", S.gather ? "1" : "0");
    if (changed && (f = fopen(path, "wb"))) {
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
        fprintf(stderr, "[enhance] profile %s: %dx%d, shadows %d, msaa %d\n", path, r.w, r.h, S.shadow, S.msaa);
    }
    free(buf);
}

/* Per-monitor DPI aware: otherwise, at 200% display scaling, Windows tells the
 * game a 4K screen is 1920x1080 and bitmap-stretches its window, blurring
 * everything, and the render-scale choices see the wrong screen. */
static void dpi_aware(void) {
    typedef BOOL (WINAPI *ctx_fn)(HANDLE);
    ctx_fn f = (ctx_fn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (!f || !f((HANDLE)-4)) SetProcessDPIAware();     /* -4: DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
}

void enhance_init(const char* host_dir, int headless) {
    g_headless = headless;
    dpi_aware();
    GetCurrentDirectoryA(MAX_PATH, g_start_dir);
    _snprintf(g_ini, sizeof g_ini, "%s\\rol.ini", host_dir);
    _snprintf(g_data, sizeof g_data, "%s\\rol_data", host_dir);
    settings_load();
    if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES) settings_save();   /* show what's settable */
    profile_apply();
    fprintf(stderr, "[enhance] %s: render %d, aniso %d, sharpen %d, zoom %d\n", g_ini, S.res, S.aniso, S.sharpen, S.zoom);
}

/* ----------------------------------------------------------- the zoom data */

/* An archive open the zoom setting replaces: rol_data\<level>\BIGS\... */
static int redirect(const char* path, char* out) {
    static const char* const k_bigs[] = { "BIGS\\multiplayer_data.big", "BIGS\\patch8\\multiplayer_data.big",
                                          "BIGS\\art.big", "BIGS\\patch8\\art.big" };
    char norm[MAX_PATH];
    if (!S.zoom || !path) return 0;
    _snprintf(norm, sizeof norm, "%s", path);
    for (char* c = norm; *c; c++) if (*c == '/') *c = '\\';
    size_t n = strlen(norm);
    for (int i = 0; i < 4; i++) {
        size_t k = strlen(k_bigs[i]);
        if (n >= k && !_stricmp(norm + n - k, k_bigs[i]) && (n == k || norm[n - k - 1] == '\\')) {
            _snprintf(out, MAX_PATH, "%s\\%s\\%s", g_data, k_zoom_dir[S.zoom], k_bigs[i]);
            return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
        }
    }
    return 0;
}

static void shim_CreateFileA(void) {
    char alt[MAX_PATH];
    const char* p = (const char*)(uintptr_t)ARG(0);
    if (redirect(p, alt)) { fprintf(stderr, "[enhance] %s -> %s\n", p, alt); p = alt; }
    g_eax = (uint32_t)(uintptr_t)CreateFileA(p, ARG(1), ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3),
                                             ARG(4), ARG(5), (HANDLE)(uintptr_t)ARG(6));
    g_esp += 4 + 7 * 4;
}

static void shim_CreateFileW(void) {
    char a[MAX_PATH], alt[MAX_PATH];
    wchar_t walt[MAX_PATH];
    const wchar_t* p = (const wchar_t*)(uintptr_t)ARG(0);
    if (p && WideCharToMultiByte(CP_ACP, 0, p, -1, a, sizeof a, NULL, NULL) && redirect(a, alt) &&
        MultiByteToWideChar(CP_ACP, 0, alt, -1, walt, MAX_PATH)) {
        fprintf(stderr, "[enhance] %s -> %s\n", a, alt);
        p = walt;
    }
    g_eax = (uint32_t)(uintptr_t)CreateFileW(p, ARG(1), ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3),
                                             ARG(4), ARG(5), (HANDLE)(uintptr_t)ARG(6));
    g_esp += 4 + 7 * 4;
}

/* ---------------------------------------------------- window, render scale */

static HWND g_hwnd;
static int g_bb_w, g_bb_h;                   /* the device's back buffer */

static RECT monitor_rect(HWND h) {
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcMonitor;
}

static int fullscreen_window(void) { return !g_headless && S.fullscreen && g_hwnd; }

static void cover_monitor(void) {
    RECT r = monitor_rect(g_hwnd);
    SetWindowLongA(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(g_hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

/* Client pixels -> back-buffer pixels: the present stretches the back buffer
 * over the client area, the game thinks in back-buffer pixels. */
static void to_game(LONG* x, LONG* y) {
    RECT c;
    if (g_headless) return;                  /* the script already speaks back-buffer pixels */
    if (!g_hwnd || !g_bb_w || !GetClientRect(g_hwnd, &c) || c.right <= 0 || c.bottom <= 0) return;
    if (c.right == g_bb_w && c.bottom == g_bb_h) return;
    *x = MulDiv(*x, g_bb_w, c.right);
    *y = MulDiv(*y, g_bb_h, c.bottom);
}

static void shim_ScreenToClient(void) {
    HWND h = (HWND)(uintptr_t)ARG(0);
    POINT* p = (POINT*)(uintptr_t)ARG(1);
    g_eax = ScreenToClient(h, p);
    if (g_eax && p && h == g_hwnd) to_game(&p->x, &p->y);
    g_esp += 4 + 2 * 4;
}

/* The game sizes its window to its resolution; a fullscreen window stays over
 * the whole monitor. */
static void shim_SetWindowPos(void) {
    HWND h = (HWND)(uintptr_t)ARG(0);
    int x = (int)ARG(2), y = (int)ARG(3), cx = (int)ARG(4), cy = (int)ARG(5);
    UINT fl = ARG(6);
    if (h == g_hwnd && fullscreen_window()) {
        RECT r = monitor_rect(h);
        x = r.left; y = r.top; cx = r.right - r.left; cy = r.bottom - r.top;
        fl &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    g_eax = SetWindowPos(h, (HWND)(uintptr_t)ARG(1), x, y, cx, cy, fl);
    g_esp += 4 + 7 * 4;
}

native32_shim_t g_enhance_shims[ENHANCE_NSHIMS] = {
    { "CreateFileA", shim_CreateFileA },
    { "CreateFileW", shim_CreateFileW },
    { "ScreenToClient", shim_ScreenToClient },
    { "SetWindowPos", shim_SetWindowPos },
};

void enhance_fix_params(D3DPRESENT_PARAMETERS* pp) {
    if (!g_headless)
        pp->PresentationInterval = S.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    g_bb_w = (int)pp->BackBufferWidth;
    g_bb_h = (int)pp->BackBufferHeight;
}

/* ------------------------------------------------------------- the device */

typedef HRESULT (WINAPI *sss_t)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
typedef HRESULT (WINAPI *reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static sss_t g_real_sss;
static reset_t g_real_reset;

/* Anisotropic filtering: the game asks for LINEAR minification and never
 * sets MAXANISOTROPY. POINT samplers (lookups, the shadow map) stay. */
static HRESULT WINAPI hl_SetSamplerState(IDirect3DDevice9* d, DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) {
    if (t == D3DSAMP_MINFILTER && v == D3DTEXF_LINEAR && S.aniso > 1) {
        g_real_sss(d, s, D3DSAMP_MAXANISOTROPY, (DWORD)S.aniso);
        v = D3DTEXF_ANISOTROPIC;
    }
    return g_real_sss(d, s, t, v);
}

static void post_release(void);

/* Reset fails while any D3DPOOL_DEFAULT resource is alive: ours go first. */
static HRESULT WINAPI hl_Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp) {
    post_release();
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->BackBufferFormat = D3DFMT_UNKNOWN;
    enhance_fix_params(pp);
    return g_real_reset(d, pp);
}

static WNDPROC g_game_proc;
static LRESULT CALLBACK enhance_proc(HWND h, UINT m, WPARAM w, LPARAM l);

static void patch_slot(void** vt, int i, void* fn, void** save) {
    DWORD old;
    if (save) *save = vt[i];
    VirtualProtect(&vt[i], sizeof(void*), PAGE_READWRITE, &old);
    vt[i] = fn;
    VirtualProtect(&vt[i], sizeof(void*), old, &old);
}

void enhance_device_created(IDirect3DDevice9* dev, HWND window) {
    void** vt = *(void***)dev;
    if (!g_real_sss) {
        patch_slot(vt, 69, (void*)hl_SetSamplerState, (void**)&g_real_sss);   /* SetSamplerState */
        patch_slot(vt, 16, (void*)hl_Reset, (void**)&g_real_reset);           /* Reset */
    }
    if (window && !g_hwnd) {
        g_hwnd = window;
        if (!g_headless) {
            g_game_proc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)enhance_proc);
            if (S.fullscreen) cover_monitor();
        }
    }
}

/* ------------------------------------------------------- post-processing */

static const char k_ps[] =
"sampler s0 : register(s0);\n"
"float4 p : register(c0);   /* 1/w, 1/h, sharpen 0..1, vivid 0..1 */\n"
"float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
"  float3 c = tex2D(s0, uv).rgb;\n"
"  float3 n = tex2D(s0, uv + float2(0, -p.y)).rgb;\n"
"  float3 s = tex2D(s0, uv + float2(0,  p.y)).rgb;\n"
"  float3 e = tex2D(s0, uv + float2( p.x, 0)).rgb;\n"
"  float3 w = tex2D(s0, uv + float2(-p.x, 0)).rgb;\n"
"  float3 mn = min(c, min(min(n, s), min(e, w)));\n"
"  float3 mx = max(c, max(max(n, s), max(e, w)));\n"
"  float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 0.0001)));\n"   /* AMD CAS's edge-aware weight */
"  float3 wt = -amp * lerp(0.125, 0.2, p.z) * step(0.001, p.z);\n"
"  float3 o = saturate((c + (n + s + e + w) * wt) / (1.0 + 4.0 * wt));\n"
"  float l = dot(o, float3(0.299, 0.587, 0.114));\n"
"  o = lerp(o, saturate(lerp(l, o, 1.3) * 1.04 - 0.02), p.w);\n"
"  return float4(o, 1);\n"
"}\n";

typedef HRESULT (WINAPI *compile_fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                     LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

static IDirect3DPixelShader9* g_ps;
static IDirect3DTexture9* g_copy;
static IDirect3DStateBlock9* g_sb;
static int g_copy_w, g_copy_h, g_ps_failed;

static IDirect3DSurface9* g_resolve;

static void post_release(void) {
    if (g_copy) { IDirect3DTexture9_Release(g_copy); g_copy = NULL; }
    if (g_resolve) { IDirect3DSurface9_Release(g_resolve); g_resolve = NULL; }
    if (g_sb) { IDirect3DStateBlock9_Release(g_sb); g_sb = NULL; }
}

static int post_ready(IDirect3DDevice9* d, IDirect3DSurface9* bb, D3DSURFACE_DESC* bd) {
    if (!g_ps && !g_ps_failed) {
        HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
        compile_fn compile = m ? (compile_fn)GetProcAddress(m, "D3DCompile") : NULL;
        ID3DBlob *code = NULL, *err = NULL;
        g_ps_failed = 1;
        if (compile && SUCCEEDED(compile(k_ps, sizeof k_ps - 1, "rol_post", NULL, NULL, "main", "ps_2_0",
                                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            g_ps_failed = FAILED(IDirect3DDevice9_CreatePixelShader(d, (const DWORD*)ID3D10Blob_GetBufferPointer(code), &g_ps));
            ID3D10Blob_Release(code);
        } else if (err) {
            fprintf(stderr, "[enhance] post shader: %s\n", (const char*)ID3D10Blob_GetBufferPointer(err));
        }
        if (err) ID3D10Blob_Release(err);
        if (g_ps_failed) fprintf(stderr, "[enhance] no post shader (d3dcompiler_47.dll)\n");
    }
    if (!g_ps) return 0;
    if (g_copy && ((int)bd->Width != g_copy_w || (int)bd->Height != g_copy_h)) post_release();
    if (!g_copy) {
        if (FAILED(IDirect3DDevice9_CreateTexture(d, bd->Width, bd->Height, 1, D3DUSAGE_RENDERTARGET, bd->Format,
                                                   D3DPOOL_DEFAULT, &g_copy, NULL))) return 0;
        g_copy_w = (int)bd->Width;
        g_copy_h = (int)bd->Height;
    }
    if (!g_sb && FAILED(IDirect3DDevice9_CreateStateBlock(d, D3DSBT_ALL, &g_sb))) return 0;
    (void)bb;
    return 1;
}

static void post(IDirect3DDevice9* d, IDirect3DSurface9* bb) {
    static const float k_sharp[] = { 0.0f, 0.35f, 0.65f, 1.0f };
    D3DSURFACE_DESC bd;
    IDirect3DSurface9* dst = NULL;
    if (!S.sharpen && !S.vivid) return;
    IDirect3DSurface9_GetDesc(bb, &bd);
    if (!post_ready(d, bb, &bd)) return;
    if (FAILED(IDirect3DTexture9_GetSurfaceLevel(g_copy, 0, &dst))) return;
    if (SUCCEEDED(IDirect3DDevice9_StretchRect(d, bb, NULL, dst, NULL, D3DTEXF_NONE))) {
        float w = (float)bd.Width, h = (float)bd.Height;
        struct { float x, y, z, rhw, u, v; } q[4] = {
            { -0.5f, -0.5f, 0, 1, 0, 0 }, { w - 0.5f, -0.5f, 0, 1, 1, 0 },
            { -0.5f, h - 0.5f, 0, 1, 0, 1 }, { w - 0.5f, h - 0.5f, 0, 1, 1, 1 } };
        float c[4] = { 1.0f / w, 1.0f / h, k_sharp[S.sharpen & 3], S.vivid ? 1.0f : 0.0f };
        IDirect3DStateBlock9_Capture(g_sb);
        IDirect3DDevice9_SetRenderTarget(d, 0, bb);
        IDirect3DDevice9_SetDepthStencilSurface(d, NULL);
        IDirect3DDevice9_SetVertexShader(d, NULL);
        IDirect3DDevice9_SetPixelShader(d, g_ps);
        IDirect3DDevice9_SetFVF(d, D3DFVF_XYZRHW | D3DFVF_TEX1);
        IDirect3DDevice9_SetTexture(d, 0, (IDirect3DBaseTexture9*)g_copy);
        g_real_sss(d, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        g_real_sss(d, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        g_real_sss(d, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_real_sss(d, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        g_real_sss(d, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        g_real_sss(d, 0, D3DSAMP_SRGBTEXTURE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_ZENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHATESTENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_CULLMODE, D3DCULL_NONE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_FOGENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_STENCILENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_SCISSORTESTENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_SRGBWRITEENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(d, D3DRS_COLORWRITEENABLE, 0xF);
        IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
        IDirect3DDevice9_BeginScene(d);
        IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
        IDirect3DDevice9_EndScene(d);
        IDirect3DStateBlock9_Apply(g_sb);
    }
    IDirect3DSurface9_Release(dst);
}

/* A surface GetRenderTargetData can read: the back buffer itself, or with
 * antialiasing (a multisampled back buffer, which cannot be read) a resolved
 * copy. The caller releases it. */
IDirect3DSurface9* enhance_readable(IDirect3DDevice9* d, IDirect3DSurface9* bb) {
    D3DSURFACE_DESC bd;
    IDirect3DSurface9_GetDesc(bb, &bd);
    if (bd.MultiSampleType == D3DMULTISAMPLE_NONE) { IDirect3DSurface9_AddRef(bb); return bb; }
    if (g_resolve) {
        D3DSURFACE_DESC rd;
        IDirect3DSurface9_GetDesc(g_resolve, &rd);
        if (rd.Width != bd.Width || rd.Height != bd.Height) { IDirect3DSurface9_Release(g_resolve); g_resolve = NULL; }
    }
    if (!g_resolve && FAILED(IDirect3DDevice9_CreateRenderTarget(d, bd.Width, bd.Height, bd.Format, D3DMULTISAMPLE_NONE, 0,
                                                                  FALSE, &g_resolve, NULL)))
        return NULL;
    if (FAILED(IDirect3DDevice9_StretchRect(d, bb, NULL, g_resolve, NULL, D3DTEXF_NONE))) return NULL;
    IDirect3DSurface9_AddRef(g_resolve);
    return g_resolve;
}

/* --------------------------------------------------------------- overlay */

static void overlay(IDirect3DDevice9* d, IDirect3DSurface9* bb) {
    static DWORD t0;
    static int frames, fps;
    enum { OW = 220, OH = 26 };
    static IDirect3DSurface9 *sys, *vid;
    DWORD now = GetTickCount();
    frames++;
    if (now - t0 >= 1000) { fps = frames; frames = 0; t0 = now; }
    if (!S.fps) return;
    if (!sys && FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, OW, OH, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL)))
        return;
    if (!vid && FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, OW, OH, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &vid, NULL)))
        return;
    HDC dc;
    if (FAILED(IDirect3DSurface9_GetDC(sys, &dc))) return;
    {
        char txt[64];
        SYSTEMTIME st;
        RECT r = { 0, 0, OW, OH };
        GetLocalTime(&st);
        _snprintf(txt, sizeof txt, "%d fps   %02d:%02d", fps, st.wHour, st.wMinute);
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 214, 120));
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(dc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    IDirect3DSurface9_ReleaseDC(sys, dc);
    if (SUCCEEDED(IDirect3DDevice9_UpdateSurface(d, sys, NULL, vid, NULL))) {
        D3DSURFACE_DESC bd;
        IDirect3DSurface9_GetDesc(bb, &bd);
        RECT to = { (LONG)bd.Width - OW - 8, 8, (LONG)bd.Width - 8, 8 + OH };
        IDirect3DDevice9_StretchRect(d, vid, NULL, bb, &to, D3DTEXF_NONE);
    }
}

/* ------------------------------------------------------------ screenshot */

static volatile LONG g_shot;

static void save_png(const char* path, const BYTE* px, UINT w, UINT h, UINT pitch) {
    IWICImagingFactory* f = NULL;
    IWICStream* s = NULL;
    IWICBitmapEncoder* e = NULL;
    IWICBitmapFrameEncode* fr = NULL;
    WCHAR wp[MAX_PATH];
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGR;
    MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&f)) &&
        SUCCEEDED(IWICImagingFactory_CreateStream(f, &s)) &&
        SUCCEEDED(IWICStream_InitializeFromFilename(s, wp, GENERIC_WRITE)) &&
        SUCCEEDED(IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &e)) &&
        SUCCEEDED(IWICBitmapEncoder_Initialize(e, (IStream*)s, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(e, &fr, NULL)) &&
        SUCCEEDED(IWICBitmapFrameEncode_Initialize(fr, NULL)) &&
        SUCCEEDED(IWICBitmapFrameEncode_SetSize(fr, w, h)) &&
        SUCCEEDED(IWICBitmapFrameEncode_SetPixelFormat(fr, &fmt)) &&
        SUCCEEDED(IWICBitmapFrameEncode_WritePixels(fr, h, pitch, pitch * h, (BYTE*)px)) &&
        SUCCEEDED(IWICBitmapFrameEncode_Commit(fr)) && SUCCEEDED(IWICBitmapEncoder_Commit(e)))
        fprintf(stderr, "[enhance] screenshot %s\n", path);
    if (fr) IWICBitmapFrameEncode_Release(fr);
    if (e) IWICBitmapEncoder_Release(e);
    if (s) IWICStream_Release(s);
    if (f) IWICImagingFactory_Release(f);
}

static void screenshot(IDirect3DDevice9* d, IDirect3DSurface9* bb) {
    D3DSURFACE_DESC bd;
    IDirect3DSurface9* sys = NULL;
    D3DLOCKED_RECT lr;
    char dir[MAX_PATH], path[MAX_PATH];
    SYSTEMTIME st;
    IDirect3DSurface9* src = enhance_readable(d, bb);
    if (!src) return;
    IDirect3DSurface9_GetDesc(src, &bd);
    if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, bd.Width, bd.Height, bd.Format, D3DPOOL_SYSTEMMEM, &sys, NULL))) {
        IDirect3DSurface9_Release(src);
        return;
    }
    if (SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(d, src, sys)) &&
        SUCCEEDED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
        if (FAILED(SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, 0, dir))) strcpy(dir, ".");
        strcat(dir, "\\Rise of Legends");
        CreateDirectoryA(dir, NULL);
        GetLocalTime(&st);
        _snprintf(path, sizeof path, "%s\\RoL %04d-%02d-%02d %02d%02d%02d.png", dir, st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
        save_png(path, (const BYTE*)lr.pBits, bd.Width, bd.Height, (UINT)lr.Pitch);
        IDirect3DSurface9_UnlockRect(sys);
    }
    IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(src);
}

void enhance_present(IDirect3DDevice9* d) {
    IDirect3DSurface9* bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(d, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    post(d, bb);
    if (InterlockedExchange(&g_shot, 0)) screenshot(d, bb);   /* the picture, before the overlay */
    overlay(d, bb);
    IDirect3DSurface9_Release(bb);
}

/* --------------------------------------------------------------- the menu */

enum {
    ID_FULL = 100, ID_RES = 110, ID_VSYNC = 130, ID_ANISO = 140, ID_MSAA = 150, ID_SHADOW = 160,
    ID_SHARP = 170, ID_VIVID = 180, ID_ZOOM = 190, ID_FPS = 200, ID_CLOCK, ID_SCORE, ID_HEALTH,
    ID_GATHER, ID_PHOTO = 220, ID_SHOT, ID_RESTART = 230
};

/* The game reads keys through DirectInput, so its own hotkeys are sent as
 * real input (to the foreground window, which is the game). */
static void send_keys(const WORD* vk, int n) {
    INPUT in[8];
    int k = 0;
    for (int i = 0; i < n && k < 8; i++) { ZeroMemory(&in[k], sizeof in[k]); in[k].type = INPUT_KEYBOARD; in[k++].ki.wVk = vk[i]; }
    for (int i = n - 1; i >= 0 && k < 8; i--) {
        ZeroMemory(&in[k], sizeof in[k]); in[k].type = INPUT_KEYBOARD; in[k].ki.wVk = vk[i];
        in[k++].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    SendInput(k, in, sizeof in[0]);
}

static void restart(void) {
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    SetEnvironmentVariableA("ROL_HOST_CHILD", NULL);      /* host.c's relaunch marker */
    SetCurrentDirectoryA(g_start_dir);
    if (CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, g_start_dir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        ExitProcess(0);
    }
}

#define CHK(c) ((c) ? MF_CHECKED : 0)
#define RS(s) (s " (restart)")

static void show_menu(HWND h) {
    HMENU m = CreatePopupMenu(), disp = CreatePopupMenu(), gfx = CreatePopupMenu(), pic = CreatePopupMenu(),
          cam = CreatePopupMenu(), stats = CreatePopupMenu(), photo = CreatePopupMenu();
    char label[64];
    res_t scr = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    AppendMenuA(disp, MF_STRING | CHK(S.fullscreen), ID_FULL, RS("Fullscreen (borderless)"));
    AppendMenuA(disp, MF_STRING | CHK(S.vsync), ID_VSYNC, RS("VSync"));
    AppendMenuA(disp, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < NRES; i++) {
        if (i && (k_res[i].w > scr.w || k_res[i].h > scr.h)) continue;
        if (i) _snprintf(label, sizeof label, "Render %dx%d%s (restart)", k_res[i].w, k_res[i].h,
                         k_res[i].h * 2 == scr.h ? "  - 2x interface" : "");
        else _snprintf(label, sizeof label, "Render at screen size %dx%d (restart)", scr.w, scr.h);
        AppendMenuA(disp, MF_STRING | CHK(S.res == i), ID_RES + i, label);
    }
    AppendMenuA(gfx, MF_STRING | CHK(S.aniso == 0), ID_ANISO + 0, "Texture filtering: as shipped");
    AppendMenuA(gfx, MF_STRING | CHK(S.aniso == 4), ID_ANISO + 1, "Texture filtering: 4x anisotropic");
    AppendMenuA(gfx, MF_STRING | CHK(S.aniso == 16), ID_ANISO + 2, "Texture filtering: 16x anisotropic");
    AppendMenuA(gfx, MF_SEPARATOR, 0, NULL);
    AppendMenuA(gfx, MF_STRING | CHK(S.shadow == 2048), ID_SHADOW + 0, RS("Shadows: 2048 (as shipped)"));
    AppendMenuA(gfx, MF_STRING | CHK(S.shadow == 4096), ID_SHADOW + 1, RS("Shadows: 4096"));
    AppendMenuA(gfx, MF_STRING | CHK(S.shadow == 8192), ID_SHADOW + 2, RS("Shadows: 8192"));
    AppendMenuA(gfx, MF_SEPARATOR, 0, NULL);
    AppendMenuA(gfx, MF_STRING | CHK(S.msaa == 0), ID_MSAA + 0, RS("Antialiasing: off"));
    AppendMenuA(gfx, MF_STRING | CHK(S.msaa == 2), ID_MSAA + 1, RS("Antialiasing: 2x"));
    AppendMenuA(gfx, MF_STRING | CHK(S.msaa == 4), ID_MSAA + 2, RS("Antialiasing: 4x"));
    AppendMenuA(pic, MF_STRING | CHK(S.sharpen == 0), ID_SHARP + 0, "Sharpening: off");
    AppendMenuA(pic, MF_STRING | CHK(S.sharpen == 1), ID_SHARP + 1, "Sharpening: light");
    AppendMenuA(pic, MF_STRING | CHK(S.sharpen == 2), ID_SHARP + 2, "Sharpening: medium");
    AppendMenuA(pic, MF_STRING | CHK(S.sharpen == 3), ID_SHARP + 3, "Sharpening: strong");
    AppendMenuA(pic, MF_SEPARATOR, 0, NULL);
    AppendMenuA(pic, MF_STRING | CHK(S.vivid), ID_VIVID, "Vivid colour");
    AppendMenuA(cam, MF_STRING | CHK(S.zoom == 0), ID_ZOOM + 0, RS("Zoom: as shipped"));
    AppendMenuA(cam, MF_STRING | CHK(S.zoom == 1), ID_ZOOM + 1, RS("Zoom: farther (1.5x)"));
    AppendMenuA(cam, MF_STRING | CHK(S.zoom == 2), ID_ZOOM + 2, RS("Zoom: farthest (2x)"));
    AppendMenuA(stats, MF_STRING | CHK(S.fps), ID_FPS, "Frame rate and clock");
    AppendMenuA(stats, MF_SEPARATOR, 0, NULL);
    AppendMenuA(stats, MF_STRING | CHK(S.clock), ID_CLOCK, RS("Game clock"));
    AppendMenuA(stats, MF_STRING | CHK(S.score), ID_SCORE, RS("Score"));
    AppendMenuA(stats, MF_STRING | CHK(S.health), ID_HEALTH, RS("Health bars always"));
    AppendMenuA(stats, MF_STRING | CHK(S.gather), ID_GATHER, RS("Gather rates"));
    AppendMenuA(photo, MF_STRING, ID_PHOTO, "Hide / show the interface\tAlt+Z");
    AppendMenuA(photo, MF_STRING, ID_SHOT, "Save a screenshot\tF9");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)disp, "Display");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)gfx, "Graphics");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)pic, "Picture");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)cam, "Camera");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)stats, "Stats");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)photo, "Photo mode");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING | (g_restart_needed ? 0 : MF_GRAYED), ID_RESTART,
                g_restart_needed ? "Restart the game to apply" : "Nothing waiting for a restart");

    RECT r;
    GetClientRect(h, &r);
    POINT at = { r.right / 2, r.bottom / 3 };
    ClientToScreen(h, &at);
    ClipCursor(NULL);
    while (ShowCursor(TRUE) < 0) {}
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_CENTERALIGN, at.x, at.y, 0, h, NULL);
    ShowCursor(FALSE);
    DestroyMenu(m);

    int restart_needed = 0;
    if (cmd == ID_FULL) { S.fullscreen ^= 1; restart_needed = 1; }
    else if (cmd >= ID_RES && cmd < ID_RES + NRES) { S.res = cmd - ID_RES; restart_needed = 1; }
    else if (cmd == ID_VSYNC) { S.vsync ^= 1; restart_needed = 1; }
    else if (cmd >= ID_ANISO && cmd < ID_ANISO + 3) S.aniso = (int[]){ 0, 4, 16 }[cmd - ID_ANISO];
    else if (cmd >= ID_SHADOW && cmd < ID_SHADOW + 3) { S.shadow = 2048 << (cmd - ID_SHADOW); restart_needed = 1; }
    else if (cmd >= ID_MSAA && cmd < ID_MSAA + 3) { S.msaa = (int[]){ 0, 2, 4 }[cmd - ID_MSAA]; restart_needed = 1; }
    else if (cmd >= ID_SHARP && cmd < ID_SHARP + 4) S.sharpen = cmd - ID_SHARP;
    else if (cmd == ID_VIVID) S.vivid ^= 1;
    else if (cmd >= ID_ZOOM && cmd < ID_ZOOM + 3) { S.zoom = cmd - ID_ZOOM; restart_needed = 1; }
    else if (cmd == ID_FPS) S.fps ^= 1;
    else if (cmd == ID_CLOCK) { S.clock ^= 1; restart_needed = 1; }
    else if (cmd == ID_SCORE) { S.score ^= 1; restart_needed = 1; }
    else if (cmd == ID_HEALTH) { S.health ^= 1; restart_needed = 1; }
    else if (cmd == ID_GATHER) { S.gather ^= 1; restart_needed = 1; }
    else if (cmd == ID_PHOTO) { static const WORD k[] = { VK_MENU, 'Z' }; send_keys(k, 2); }
    else if (cmd == ID_SHOT) InterlockedExchange(&g_shot, 1);
    else if (cmd == ID_RESTART) { settings_save(); profile_apply(); restart(); }
    if (cmd && cmd != ID_RESTART) settings_save();
    if (restart_needed) g_restart_needed = 1;
    if (restart_needed && MessageBoxA(h, "This setting applies when the game starts.\n\nRestart the game now? "
                                         "Unsaved progress is lost.", "Rise of Legends",
                                      MB_YESNO | MB_ICONQUESTION) == IDYES) {
        profile_apply();
        restart();
    }
}

static LRESULT CALLBACK enhance_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && w == VK_F10) { show_menu(h); return 0; }
    if ((m == WM_KEYUP || m == WM_SYSKEYUP) && w == VK_F10) return 0;   /* F10 is Windows' menu key */
    if (m == WM_KEYDOWN && w == VK_F9) { InterlockedExchange(&g_shot, 1); return 0; }
    if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL) {
        LONG x = (SHORT)LOWORD(l), y = (SHORT)HIWORD(l);
        to_game(&x, &y);
        l = MAKELPARAM(x, y);
    }
    return CallWindowProcW(g_game_proc, h, m, w, l);
}
