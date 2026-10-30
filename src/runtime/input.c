/*
 * Scripted input for headless runs: --click x,y@s, --rclick x,y@s (orders),
 * --drag x1,y1,x2,y2@s (box select, left button) and --key vk@s.
 *
 * The game's window is hidden (host.c, --headless), so nothing real can
 * reach it, and over RDP the real cursor must not: the phone's pointer would
 * drive the game. So the cursor is virtual. GetCursorPos/SetCursorPos read
 * and write a scripted position, and GetAsyncKeyState reports only scripted
 * keys and buttons. A thread plays the script as window messages posted to
 * the game's window, the way Windows would deliver them.
 *
 * Times are positions in the --record video, which is 30 frames a second of
 * presented frames: @95 means frame 2850. Not wall-clock time: the game
 * presents faster than 30 fps here, and loading time varies, so a script
 * written by reading timestamps off a recording would otherwise click on
 * whatever happened to be up at that moment.
 *
 * Coordinates are client pixels of the game's 1024x768 window, the same as a
 * frame of --record.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "input.h"

HWND g_game_hwnd;
extern volatile LONG g_frames;                 /* host.c: frames presented */
#define SCRIPT_FPS 30                          /* host.c records at 30 fps */

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
#define MAX_EVENTS 512

typedef struct { int key; int x, y, x2, y2; double at; } event_t;
#define EV_CLICK 0
#define EV_MOVE (-1)
#define EV_RCLICK (-2)
#define EV_DRAG (-3)
#define EV_WHEEL (-4)                          /* x = delta, 120 a notch, + away from you */
static event_t g_events[MAX_EVENTS];
static int g_nevents;
/* Where the script wants the cursor, in client pixels. Windows messages and
 * GetCursorPos report it as is; the game's own cursor is steered to it with
 * DirectInput motion (see mouse_step). */
static POINT g_oscursor = { 512, 384 };
static volatile LONG g_wheel;          /* wheel motion not yet read by the game */
static volatile LONG g_steer;          /* a move or click has happened */
static volatile LONG g_held[256];              /* scripted keys/buttons down */

int input_arg(int argc, char** argv, int i) {
    const char* at;
    if (i + 1 >= argc || g_nevents == MAX_EVENTS) return 0;
    if (!strcmp(argv[i], "--click") || !strcmp(argv[i], "--move") || !strcmp(argv[i], "--rclick")) {
        event_t* e = &g_events[g_nevents];
        if (sscanf(argv[i + 1], "%d,%d", &e->x, &e->y) != 2) return 0;
        at = strchr(argv[i + 1], '@');
        e->at = at ? atof(at + 1) : 0;
        e->key = argv[i][2] == 'm' ? EV_MOVE : argv[i][2] == 'r' ? EV_RCLICK : EV_CLICK;
        g_nevents++;
        return 2;
    }
    if (!strcmp(argv[i], "--wheel")) {
        event_t* e = &g_events[g_nevents];
        e->x = atoi(argv[i + 1]);
        at = strchr(argv[i + 1], '@');
        e->at = at ? atof(at + 1) : 0;
        e->key = EV_WHEEL;
        g_nevents++;
        return 2;
    }
    if (!strcmp(argv[i], "--drag")) {
        event_t* e = &g_events[g_nevents];
        if (sscanf(argv[i + 1], "%d,%d,%d,%d", &e->x, &e->y, &e->x2, &e->y2) != 4) return 0;
        at = strchr(argv[i + 1], '@');
        e->at = at ? atof(at + 1) : 0;
        e->key = EV_DRAG;
        g_nevents++;
        return 2;
    }
    if (!strcmp(argv[i], "--key")) {
        event_t* e = &g_events[g_nevents];
        e->key = (int)strtol(argv[i + 1], NULL, 0);
        at = strchr(argv[i + 1], '@');
        e->at = at ? atof(at + 1) : 0;
        g_nevents++;
        return 2;
    }
    return 0;
}

static LPARAM client_lparam(void) { return MAKELPARAM(g_oscursor.x, g_oscursor.y); }

/* Move the cursor, and give the game's per-frame DirectInput steering time to
 * get it there before anything is pressed. */
static void move_to(int x, int y, DWORD settle) {
    g_oscursor.x = x;
    g_oscursor.y = y;
    g_steer = 1;
    Sleep(settle);
    PostMessageA(g_game_hwnd, WM_MOUSEMOVE, 0, client_lparam());
}

static void button(int vk, int down) {
    UINT msg = vk == VK_LBUTTON ? (down ? WM_LBUTTONDOWN : WM_LBUTTONUP)
                                : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
    g_held[vk] = down;
    PostMessageA(g_game_hwnd, msg, down ? (vk == VK_LBUTTON ? MK_LBUTTON : MK_RBUTTON) : 0, client_lparam());
}

static DWORD WINAPI script(LPVOID unused) {
    (void)unused;
    /* No WM_ACTIVATE/WM_ACTIVATEAPP: the game believes it is fullscreen, and
     * an activation sends it into lost-device recovery (reset, then sleep
     * until the device is back) on a device that was never lost. It stopped
     * at frame 1. GetForegroundWindow (below) is enough. */
    while (!g_frames || !g_game_hwnd) Sleep(10);
    for (int i = 0; i < g_nevents; i++) {
        event_t* e = &g_events[i];
        while (g_frames < (LONG)(e->at * SCRIPT_FPS)) Sleep(2);
        if (e->key == EV_MOVE) {
            move_to(e->x, e->y, 0);
            fprintf(stderr, "[input] %.1fs move %d,%d\n", e->at, e->x, e->y);
        } else if (e->key == EV_CLICK || e->key == EV_RCLICK) {
            int vk = e->key == EV_CLICK ? VK_LBUTTON : VK_RBUTTON;
            move_to(e->x, e->y, 250);          /* let the motion land first */
            Sleep(60);
            button(vk, 1);
            Sleep(90);
            button(vk, 0);
            fprintf(stderr, "[input] %.1fs %s %d,%d\n", e->at, vk == VK_LBUTTON ? "click" : "rclick", e->x, e->y);
        } else if (e->key == EV_WHEEL) {
            POINT sp = g_oscursor;
            InterlockedExchangeAdd(&g_wheel, e->x);
            ClientToScreen(g_game_hwnd, &sp);
            PostMessageA(g_game_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (SHORT)e->x), MAKELPARAM(sp.x, sp.y));
            fprintf(stderr, "[input] %.1fs wheel %d\n", e->at, e->x);
        } else if (e->key == EV_DRAG) {
            move_to(e->x, e->y, 250);
            Sleep(60);
            button(VK_LBUTTON, 1);
            for (int k = 1; k <= 8; k++)       /* a few frames of motion, so it reads as a drag */
                move_to(e->x + (e->x2 - e->x) * k / 8, e->y + (e->y2 - e->y) * k / 8, 60);
            Sleep(120);
            button(VK_LBUTTON, 0);
            fprintf(stderr, "[input] %.1fs drag %d,%d -> %d,%d\n", e->at, e->x, e->y, e->x2, e->y2);
        } else {
            UINT sx = MapVirtualKeyA(e->key, MAPVK_VK_TO_VSC_EX);
            UINT sc = (sx & 0xFF) | ((sx >> 8) == 0xE0 ? 0x100 : 0);   /* bit 24: extended (Page Up, not keypad 9) */
            g_held[e->key & 0xFF] = 1;
            PostMessageA(g_game_hwnd, WM_KEYDOWN, e->key, 1 | (sc << 16));
            Sleep(90);
            g_held[e->key & 0xFF] = 0;
            PostMessageA(g_game_hwnd, WM_KEYUP, e->key, 1 | (sc << 16) | 0xC0000000u);
            fprintf(stderr, "[input] %.1fs key 0x%02X\n", e->at, e->key);
        }
    }
    return 0;
}

void input_start(void) {
    if (g_nevents) CloseHandle(CreateThread(NULL, 0, script, NULL, 0, NULL));
}

/* ------------------------------------------------------------ DirectInput */

/* The game reads the mouse and keyboard through DirectInput 8 and moves its
 * own cursor by the mouse's relative motion; GetCursorPos only seeds it. So
 * the script has to arrive as DirectInput data. The device vtable is patched
 * (all devices share it; each call is sorted by instance):
 *
 *   SetCooperativeLevel, Acquire   always succeed: the window is hidden and
 *                                  never foreground, so the real ones fail
 *   GetDeviceState, GetDeviceData  the script, never the hardware: mouse
 *                                  motion and buttons, keyboard keys
 *
 * Motion is reported closed-loop, see mouse_step. */
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

static void* g_mouse_dev;
static void* g_kbd_dev;
static int g_told_btn, g_told_rbtn;

/* A virtual key as DirectInput names it. Extended keys (Page Up, the arrows,
 * Home, End, Insert, Delete) share their scan code with the numeric keypad and
 * differ only by the E0 prefix; DirectInput adds 0x80 for it. Without that,
 * Page Up reached the game as keypad 9. */
static unsigned dik(int vk) {
    unsigned sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC_EX);
    return (sc & 0xFF) | ((sc >> 8) == 0xE0 ? 0x80 : 0);
}
static unsigned char g_told_keys[256];
static DWORD g_seq;
static volatile LONG g_n_state[2], g_n_data[2];   /* [mouse, keyboard] reads */

void input_report(void) {
    fprintf(stderr, "[input] DirectInput reads: mouse %ld state + %ld data, keyboard %ld state + %ld data\n",
            g_n_state[0], g_n_data[0], g_n_state[1], g_n_data[1]);
}

typedef HRESULT (WINAPI *di_create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT (WINAPI *create_dev_t)(void*, REFGUID, void**, LPUNKNOWN);
typedef HRESULT (WINAPI *plain_t)(void*);
typedef HRESULT (WINAPI *coop_t)(void*, HWND, DWORD);
typedef HRESULT (WINAPI *state_t)(void*, DWORD, LPVOID);
typedef HRESULT (WINAPI *data_t)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static di_create_t g_real_di_create;
static create_dev_t g_real_create_dev;

static void patch(void** slot, void* fn, void** save) {
    DWORD old;
    if (save && !*save) *save = *slot;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = fn;
    VirtualProtect(slot, 4, old, &old);
}

static HRESULT WINAPI hl_coop(void* self, HWND h, DWORD f) { (void)self; (void)h; (void)f; return DI_OK; }

typedef HRESULT (WINAPI *setprop_t)(void*, REFGUID, LPCDIPROPHEADER);
static setprop_t g_real_setprop;
static int g_mouse_abs;                /* the game put the mouse in DIPROPAXISMODE_ABS */

static HRESULT WINAPI hl_setprop(void* self, REFGUID prop, LPCDIPROPHEADER h) {
    uintptr_t id = (uintptr_t)prop;    /* DIPROP_* are MAKEDIPROP(n): small integers */
    if (id < 0x10000 && h->dwSize >= sizeof(DIPROPDWORD)) {
        DWORD v = ((const DIPROPDWORD*)h)->dwData;
        fprintf(stderr, "[input] SetProperty %s prop %u = %lu\n",
                self == g_mouse_dev ? "mouse" : self == g_kbd_dev ? "keyboard" : "?", (unsigned)id, v);
        if (self == g_mouse_dev && prop == DIPROP_AXISMODE) g_mouse_abs = v == DIPROPAXISMODE_ABS;
    }
    return g_real_setprop(self, prop, h);
}
static HRESULT WINAPI hl_acquire(void* self) { (void)self; return DI_OK; }

/* The game moves its cursor by DirectInput motion alone: 0x00554490 adds
 * each delta times a sensitivity (float 0x00E52CA8, 1.5) to an accumulator
 * (floats 0x0104CCF0/F4) and draws the cursor at its floor. GetCursorPos
 * syncs it only once per focus gain (0x005550B0), which a hidden window
 * never has. The accumulator starts at 32.0, not where the cursor is drawn,
 * so open-loop deltas from the centre snapped it to the edge. Instead each
 * frame reports the rest of the way from the game's own accumulator to the
 * target. Once per frame: the game drains the buffer in a loop, and a second
 * read in the same frame would see the accumulator not yet moved. */
static void mouse_step(LONG* dx, LONG* dy) {
    static LONG last_frame = -1;
    float sens = *(float*)&MEM32(0x00E52CA8), ax = *(float*)&MEM32(0x0104CCF0), ay = *(float*)&MEM32(0x0104CCF4);
    *dx = *dy = 0;
    if (!g_steer || g_frames == last_frame || !(sens > 0)) return;
    last_frame = g_frames;
    *dx = (LONG)((g_oscursor.x + 0.5f - ax) / sens);
    *dy = (LONG)((g_oscursor.y + 0.5f - ay) / sens);
}

static HRESULT WINAPI hl_state(void* self, DWORD cb, LPVOID out) {
    memset(out, 0, cb);
    InterlockedIncrement(&g_n_state[self == g_mouse_dev ? 0 : 1]);
    if (self == g_mouse_dev && cb >= sizeof(DIMOUSESTATE)) {
        DIMOUSESTATE* m = (DIMOUSESTATE*)out;
        mouse_step(&m->lX, &m->lY);
        m->lZ = InterlockedExchange(&g_wheel, 0);
        m->rgbButtons[0] = g_held[VK_LBUTTON] ? 0x80 : 0;
        m->rgbButtons[1] = g_held[VK_RBUTTON] ? 0x80 : 0;
    } else if (self == g_kbd_dev && cb >= 256) {
        for (int vk = 1; vk < 256; vk++)
            if (g_held[vk]) ((BYTE*)out)[dik(vk)] = 0x80;
    }
    return DI_OK;
}

/* Buffered reads: synthesise the events between what the game was last told
 * and the scripted state now. */
static HRESULT WINAPI hl_data(void* self, DWORD cb, LPDIDEVICEOBJECTDATA out, LPDWORD n, DWORD flags) {
    DWORD cap = *n, k = 0;
    (void)flags;
    InterlockedIncrement(&g_n_data[self == g_mouse_dev ? 0 : 1]);
#define EMIT(ofs, val) do { if (out && k < cap) { \
        LPDIDEVICEOBJECTDATA d = (LPDIDEVICEOBJECTDATA)((BYTE*)out + k * cb); \
        memset(d, 0, cb); d->dwOfs = (ofs); d->dwData = (DWORD)(val); \
        d->dwTimeStamp = GetTickCount(); d->dwSequence = ++g_seq; } k++; } while (0)
    if (self == g_mouse_dev) {
        int btn = g_held[VK_LBUTTON] != 0;
        LONG dx, dy;
        mouse_step(&dx, &dy);
        if (dx) EMIT(DIMOFS_X, dx);
        if (dy) EMIT(DIMOFS_Y, dy);
        int rbtn = g_held[VK_RBUTTON] != 0;
        if (btn != g_told_btn) { EMIT(DIMOFS_BUTTON0, btn ? 0x80 : 0); g_told_btn = btn; }
        if (rbtn != g_told_rbtn) { EMIT(DIMOFS_BUTTON1, rbtn ? 0x80 : 0); g_told_rbtn = rbtn; }
        { LONG z = InterlockedExchange(&g_wheel, 0); if (z) EMIT(DIMOFS_Z, z); }
    } else if (self == g_kbd_dev) {
        for (int vk = 1; vk < 256; vk++) {
            unsigned sc = dik(vk);
            int down = g_held[vk] != 0;
            if (sc && down != g_told_keys[sc]) { EMIT(sc, down ? 0x80 : 0); g_told_keys[sc] = (unsigned char)down; }
        }
    }
#undef EMIT
    if (k > cap) k = cap;
    *n = k;
    return DI_OK;
}

static HRESULT WINAPI hl_create_dev(void* self, REFGUID g, void** dev, LPUNKNOWN outer) {
    HRESULT hr = g_real_create_dev(self, g, dev, outer);
    if (hr == DI_OK) {
        void** vt = *(void***)*dev;
        if (IsEqualGUID(g, &GUID_SysMouse)) g_mouse_dev = *dev;
        if (IsEqualGUID(g, &GUID_SysKeyboard)) g_kbd_dev = *dev;
        patch(&vt[6], hl_setprop, (void**)&g_real_setprop);
        patch(&vt[7], hl_acquire, NULL);
        patch(&vt[9], hl_state, NULL);
        patch(&vt[10], hl_data, NULL);
        patch(&vt[13], hl_coop, NULL);
        fprintf(stderr, "[input] DirectInput device %s -> scripted\n",
                IsEqualGUID(g, &GUID_SysMouse) ? "mouse" :
                IsEqualGUID(g, &GUID_SysKeyboard) ? "keyboard" : "other");
    }
    return hr;
}

static HRESULT WINAPI hl_di_create(HINSTANCE inst, DWORD ver, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    /* The game passes its hInstance, which is the guest image (0x00400000,
     * host.c's GetModuleHandleA). DirectInput checks it against the loader's
     * module list, where the guest image is not, and answered E_INVALIDARG:
     * the recompiled game had no mouse or keyboard at all. Any real module
     * will do; use the host's. */
    if ((uintptr_t)inst == 0x00400000u) inst = GetModuleHandleA(NULL);
    HRESULT hr = g_real_di_create(inst, ver, iid, out, outer);
    fprintf(stderr, "[input] DirectInput8Create -> 0x%08lX\n", hr);
    if (hr == DI_OK && !g_real_create_dev)
        patch(&(*(void***)*out)[3], hl_create_dev, (void**)&g_real_create_dev);  /* CreateDevice */
    return hr;
}

/* Called by host.c's GetProcAddress shim. */
FARPROC input_wrap_proc(const char* name, FARPROC real) {
    if (!strcmp(name, "DirectInput8Create")) {
        g_real_di_create = (di_create_t)real;
        return (FARPROC)hl_di_create;
    }
    return real;
}

/* ------------------------------------------------------------ guest shims */

static void shim_GetCursorPos(void) {
    POINT p = g_oscursor;
    if (g_game_hwnd) ClientToScreen(g_game_hwnd, &p);
    if (ARG(0)) *(POINT*)(uintptr_t)ARG(0) = p;
    g_eax = 1;
    g_esp += 4 + 1 * 4;
}

/* The game re-centres the Windows cursor every frame it has moved (mouse
 * capture). The script's target is not the Windows cursor, so it stays put:
 * taking the centre as the new target walked the game's cursor back to it. */
static void shim_SetCursorPos(void) {
    g_eax = 1;
    g_esp += 4 + 2 * 4;
}

static void shim_GetAsyncKeyState(void) {
    g_eax = g_held[ARG(0) & 0xFF] ? 0x8000u : 0;
    g_esp += 4 + 1 * 4;
}

/* The game window is always foreground. */
static void shim_GetForegroundWindow(void) {
    g_eax = (uint32_t)(uintptr_t)g_game_hwnd;
    g_esp += 4;
}

native32_shim_t g_input_shims[INPUT_NSHIMS] = {
    { "GetForegroundWindow", shim_GetForegroundWindow },
    { "GetCursorPos", shim_GetCursorPos },
    { "SetCursorPos", shim_SetCursorPos },
    { "GetAsyncKeyState", shim_GetAsyncKeyState },
};
