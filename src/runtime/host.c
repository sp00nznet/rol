/*
 * Rise of Legends - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header). What is here is only what is specific to
 * this game: where the image goes, the command line, and the fault report.
 * docs/host.md has the reasoning.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so the guest image at 0x00400000 is
 * free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3d9.h>

#include "native32.h"
#include "recomp_trace.h"
#include "input.h"

extern const uint32_t rol_entry_va;     /* recomp_dispatch.c */

#define ROL_IMAGE_BASE 0x00400000u

static DWORD g_watchdog_s;
static int   g_headless;

/* --headless: nothing reaches the screen. Over RDP a window lands on whatever
 * device is connected (REPO_RULES section 13), so a message box prints, and
 * the game's window is real but never shown: D3D9 renders into a hidden
 * window as happily as a visible one. */
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
static const char* gstr(uint32_t va) { return va ? (const char*)(uintptr_t)va : "(null)"; }

/* The answer a headless run gives: No where the box has a No button, else OK.
 * The game's "not closed properly last time" prompt is YES/NO/CANCEL, where
 * Yes opens the Readme (a browser, on an RDP phone) and Cancel drops the game
 * to its lowest graphics setting. IDOK is none of the three. */
static uint32_t mb_answer(uint32_t type) {
    uint32_t buttons = type & MB_TYPEMASK;
    return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDNO : IDOK;
}

static void shim_MessageBoxA(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %s: %s\n", ARG(3), g_eax, gstr(ARG(2)), gstr(ARG(1)));
    g_esp += 4 + 4 * 4;
}

static void shim_MessageBoxW(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %ls: %ls\n", ARG(3), g_eax,
            (const wchar_t*)(uintptr_t)ARG(2), (const wchar_t*)(uintptr_t)ARG(1));
    g_esp += 4 + 4 * 4;
}

/* The real CreateWindowExA minus WS_VISIBLE. Windows calls the guest's window
 * procedure from inside it (WM_CREATE...); native32's lock nests, and the
 * callback runs below these arguments on the guest stack. */
static void shim_CreateWindowExA(void) {
    HWND h = CreateWindowExA(ARG(0), (LPCSTR)(uintptr_t)ARG(1), (LPCSTR)(uintptr_t)ARG(2),
                             ARG(3) & ~WS_VISIBLE, (int)ARG(4), (int)ARG(5), (int)ARG(6),
                             (int)ARG(7), (HWND)(uintptr_t)ARG(8), (HMENU)(uintptr_t)ARG(9),
                             (HINSTANCE)(uintptr_t)ARG(10), (LPVOID)(uintptr_t)ARG(11));
    fprintf(stderr, "[headless] CreateWindowExA(\"%s\", %dx%d) from sub_%08X -> hidden hwnd %p\n",
            gstr(ARG(2)), (int)ARG(6), (int)ARG(7), g_cur_func, (void*)h);
    g_game_hwnd = h;
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

static void shim_ShowWindow(void) {
    g_eax = 0;                         /* "was hidden", which is true */
    g_esp += 4 + 2 * 4;
}

/* Force a windowed D3D9 device. An exclusive-fullscreen device changes the
 * display mode even for a hidden window, which is exactly what must not happen
 * to an RDP session, and the game has a Fullscreen setting of its own. The
 * game reaches D3D9 only through GetProcAddress("Direct3DCreate9"), so that is
 * where it is wrapped; CreateDevice is patched in the IDirect3D9 vtable. */
typedef HRESULT (WINAPI *create_device_t)(void* self, UINT adapter, DWORD type, HWND focus,
                                          DWORD flags, D3DPRESENT_PARAMETERS* pp, void** out);
typedef void* (WINAPI *d3dcreate9_t)(UINT sdk);
static create_device_t g_real_create_device;
static d3dcreate9_t g_real_d3dcreate9;

/* IDirect3DDevice9::Present, counted. The frame count is the boot's last
 * milestone (tools/conformance.py) and the hook is where --record captures. */
typedef HRESULT (WINAPI *present_t)(void* self, const RECT* src, const RECT* dst, HWND wnd,
                                    const RGNDATA* dirty);
static present_t g_real_present;
volatile LONG g_frames;          /* input.c times its script from the first */

/* --record out.mp4: every presented frame, read back and piped to ffmpeg as
 * raw BGRX. Nothing is shown anywhere, so it works over RDP (REPO_RULES 10/13).
 * --frames N stops after N recorded frames and closes the file properly; a
 * process killed mid-recording leaves an mp4 with no index. */
static const char* g_record;
static long g_record_frames;
static FILE* g_ffmpeg;
static IDirect3DSurface9* g_readback;
static long g_recorded;

static void record_close(void) {
    if (g_ffmpeg) {
        _pclose(g_ffmpeg);
        g_ffmpeg = NULL;
        fprintf(stderr, "[record] %ld frames -> %s\n", g_recorded, g_record);
    }
}

static void record_frame(IDirect3DDevice9* dev) {
    IDirect3DSurface9* bb = NULL;
    D3DSURFACE_DESC d;
    D3DLOCKED_RECT lr;
    if (FAILED(dev->lpVtbl->GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    bb->lpVtbl->GetDesc(bb, &d);
    if (!g_readback) {
        char cmd[MAX_PATH * 2];
        if (FAILED(dev->lpVtbl->CreateOffscreenPlainSurface(dev, d.Width, d.Height, d.Format,
                                                            D3DPOOL_SYSTEMMEM, &g_readback, NULL))) {
            bb->lpVtbl->Release(bb);
            return;
        }
        _snprintf(cmd, sizeof cmd - 1, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgr0 "
                  "-s %ux%u -r 30 -i - -c:v libx264 -pix_fmt yuv420p \"%s\"",
                  d.Width, d.Height, g_record);
        g_ffmpeg = _popen(cmd, "wb");
        fprintf(stderr, "[record] %ux%u format %d -> %s\n", d.Width, d.Height, d.Format, g_record);
    }
    if (g_ffmpeg && SUCCEEDED(dev->lpVtbl->GetRenderTargetData(dev, bb, g_readback)) &&
        SUCCEEDED(g_readback->lpVtbl->LockRect(g_readback, &lr, NULL, D3DLOCK_READONLY))) {
        for (UINT y = 0; y < d.Height; y++)
            fwrite((const char*)lr.pBits + y * lr.Pitch, 4, d.Width, g_ffmpeg);
        g_readback->lpVtbl->UnlockRect(g_readback);
        g_recorded++;
    }
    bb->lpVtbl->Release(bb);
    if (g_record_frames && g_recorded >= g_record_frames) {
        record_close();
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

static HRESULT WINAPI hl_Present(void* self, const RECT* src, const RECT* dst, HWND wnd,
                                 const RGNDATA* dirty) {
    LONG n = InterlockedIncrement(&g_frames);
    if (n == 1 || n == 10 || n == 100 || n % 1000 == 0)
        fprintf(stderr, "[headless] frame %ld presented\n", n);
    if (g_record) record_frame((IDirect3DDevice9*)self);
    return g_real_present(self, src, dst, wnd, dirty);
}

static HRESULT WINAPI hl_CreateDevice(void* self, UINT adapter, DWORD type, HWND focus,
                                      DWORD flags, D3DPRESENT_PARAMETERS* pp, void** out) {
    fprintf(stderr, "[headless] CreateDevice adapter %u type %lu flags 0x%lX: %ux%u fmt %d x%u "
                    "ms %d swap %d depth %d/%d interval 0x%X %s -> windowed\n",
            adapter, type, flags, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat,
            pp->BackBufferCount, pp->MultiSampleType, pp->SwapEffect,
            pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->PresentationInterval,
            pp->Windowed ? "windowed" : "FULLSCREEN");
    /* Windowed, the back buffer must match the desktop: the game asks for a
     * 16-bit A1R5G5B5 mode, which is D3DERR_NOTAVAILABLE on a 32-bit desktop. */
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->BackBufferFormat = D3DFMT_UNKNOWN;
    HRESULT hr = g_real_create_device(self, adapter, type, focus, flags, pp, out);
    fprintf(stderr, "[headless] CreateDevice -> 0x%08lX\n", hr);
    if (hr == D3D_OK && !g_real_present) {
        void** vt = *(void***)*out;
        DWORD old;
        g_real_present = (present_t)vt[17];                 /* IDirect3DDevice9::Present */
        VirtualProtect(&vt[17], 4, PAGE_READWRITE, &old);
        vt[17] = (void*)hl_Present;
        VirtualProtect(&vt[17], 4, old, &old);
    }
    return hr;
}

static void* WINAPI hl_Direct3DCreate9(UINT sdk) {
    void** d3d = (void**)g_real_d3dcreate9(sdk);
    if (d3d && !g_real_create_device) {
        void** vt = *(void***)d3d;
        DWORD old;
        g_real_create_device = (create_device_t)vt[16];     /* IDirect3D9::CreateDevice */
        VirtualProtect(&vt[16], 4, PAGE_READWRITE, &old);
        vt[16] = (void*)hl_CreateDevice;
        VirtualProtect(&vt[16], 4, old, &old);
    }
    fprintf(stderr, "[headless] Direct3DCreate9(%u) -> %p\n", sdk, (void*)d3d);
    if (d3d) {
        IDirect3D9* i = (IDirect3D9*)d3d;
        D3DADAPTER_IDENTIFIER9 id;
        D3DDISPLAYMODE dm;
        D3DCAPS9 caps;
        UINT n = i->lpVtbl->GetAdapterCount(i);
        i->lpVtbl->GetAdapterIdentifier(i, 0, 0, &id);
        i->lpVtbl->GetAdapterDisplayMode(i, 0, &dm);
        HRESULT hc = i->lpVtbl->GetDeviceCaps(i, 0, D3DDEVTYPE_HAL, &caps);
        fprintf(stderr, "[headless] %u adapter(s); 0: %s, desktop %ux%u fmt %d, HAL caps 0x%08lX\n",
                n, id.Description, dm.Width, dm.Height, dm.Format, hc);
    }
    return d3d;
}

static void shim_GetProcAddress(void) {
    HMODULE m = (HMODULE)(uintptr_t)ARG(0);
    const char* name = (const char*)(uintptr_t)ARG(1);
    FARPROC p = GetProcAddress(m, name);
    if (p && ARG(1) >> 16 && !strcmp(name, "Direct3DCreate9")) {
        g_real_d3dcreate9 = (d3dcreate9_t)p;
        p = (FARPROC)hl_Direct3DCreate9;
    }
    else if (p && ARG(1) >> 16) p = input_wrap_proc(name, p);
    g_eax = (uint32_t)(uintptr_t)p;
    g_esp += 4 + 2 * 4;
}

/* The guest is legends.exe in the game folder, not this host. It finds its
 * data from GetModuleFileNameA -- with the host's path it looked for data\
 * under build\, found nothing, and a file-resolver fallback recursed until
 * the stack ran out -- and its hInstance (resources, window classes) comes
 * from GetModuleHandleA(NULL), which has to be the guest image. */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[MAX_PATH + 3];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0))
                   : ROL_IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0), size = ARG(2);
    char* out = (char*)(uintptr_t)ARG(1);
    if (h == 0 || h == ROL_IMAGE_BASE) {
        uint32_t n = (uint32_t)strlen(g_guest_exe);
        if (size) {
            uint32_t k = n < size ? n : size - 1;
            memcpy(out, g_guest_exe, k);
            out[k] = 0;
            n = k;
        }
        g_eax = n;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) {
    g_eax = (uint32_t)(uintptr_t)g_guest_cmdline;
    g_esp += 4;
}

/* SIMD the guest may use. D3DX-style code picks SSE/3DNow! paths by CPUID
 * and IsProcessorFeaturePresent, and lift32 implements few of those
 * instructions (16,977 unimplemented sites in this lift, nearly all SIMD), so
 * by default the guest is told there is none and takes its x87 paths. --simd
 * shows the host's real features. MMX stays: lift32 does implement it. */
static int g_simd;

static void shim_IsProcessorFeaturePresent(void) {
    uint32_t f = ARG(0);
    int simd = f == PF_XMMI_INSTRUCTIONS_AVAILABLE || f == PF_3DNOW_INSTRUCTIONS_AVAILABLE ||
               f == PF_XMMI64_INSTRUCTIONS_AVAILABLE || f == PF_SSE3_INSTRUCTIONS_AVAILABLE ||
               f >= 36;                /* SSSE3, SSE4.x, AVX and later */
    g_eax = (simd && !g_simd) ? 0 : IsProcessorFeaturePresent(f);
    g_esp += 4 + 1 * 4;
}

static void hide_simd(void) {
    if (g_simd) return;
    g_cpuid_edx1 &= ~((1u << 25) | (1u << 26));          /* SSE, SSE2 */
    g_cpuid_ecx1 = 0;                                     /* SSE3 .. AVX, and the rest */
    g_cpuid_edx_ext &= ~((1u << 31) | (1u << 30) | (1u << 22));   /* 3DNow!, 3DNow!+, MMX+ */
}

#define GUEST_SHIMS \
    { "IsProcessorFeaturePresent", shim_IsProcessorFeaturePresent }, \
    { "GetModuleHandleA", shim_GetModuleHandleA }, \
    { "GetModuleFileNameA", shim_GetModuleFileNameA }, \
    { "GetCommandLineA", shim_GetCommandLineA }

static native32_shim_t g_shims[] = { GUEST_SHIMS };

static native32_shim_t g_headless_shims[] = {
    GUEST_SHIMS,
    { "GetProcAddress", shim_GetProcAddress },
    { "MessageBoxA", shim_MessageBoxA },
    { "MessageBoxW", shim_MessageBoxW },
    { "CreateWindowExA", shim_CreateWindowExA },
    { "ShowWindow", shim_ShowWindow },
};

/* --probe VA (repeatable): report indirect calls to VA -- a virtual method or
 * callback -- with `this` and the first arguments. RECOMP_ICALL asks this
 * hook before the dispatch table, so it costs nothing when unset. */
#define MAX_PROBES 8
static uint32_t g_probe[MAX_PROBES];
static int g_nprobe;

static volatile LONG g_probe_hits[MAX_PROBES];

recomp_func_t recomp_lookup_manual(uint32_t va) {
    for (int i = 0; i < g_nprobe; i++)
        if (g_probe[i] == va && InterlockedIncrement(&g_probe_hits[i]) <= 5)
            fprintf(stderr, "[probe] sub_%08X from sub_%08X  ecx=%08X  args %08X %08X %08X\n",
                    va, g_cur_func, g_ecx, MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8));
    return NULL;
}

/* --watch32 ADDR: sample one guest dword every 10 ms and log each change
 * (the first 100), from a host thread. Unlike recomp_trace's --poison it needs
 * no RECOMP_TRACE build; the price is that a change between samples is lost. */
static uint32_t g_watch32, g_watch32_ptr, g_watch32_off;   /* *PTR+OFF form */

static DWORD WINAPI watch32(LPVOID unused) {
    uint32_t last = 0, n = 0;
    DWORD t0 = GetTickCount();
    (void)unused;
    while (n < 100) {
        MEMORY_BASIC_INFORMATION mbi;
        if (g_watch32_ptr) g_watch32 = MEM32(g_watch32_ptr) ? MEM32(g_watch32_ptr) + g_watch32_off : 0;
        if (g_watch32 && VirtualQuery((void*)(uintptr_t)g_watch32, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
            uint32_t v = MEM32(g_watch32);
            if (v != last) {
                fprintf(stderr, "[watch32] %6.2fs frame %ld [%08X] %08X -> %08X (%d)\n",
                        (GetTickCount() - t0) / 1000.0, g_frames, g_watch32, last, v, (int32_t)v);
                last = v;
                n++;
            }
        }
        Sleep(10);
    }
    return 0;
}

static void probe_report(void) {
    for (int i = 0; i < g_nprobe; i++)
        fprintf(stderr, "[probe] sub_%08X: %ld calls\n", g_probe[i], g_probe_hits[i]);
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr,
        "\n[not-lifted] sub_%08X  (called from 0x%08X)\n"
        "  Widen the closure:  py -3 run_lift.py --roots 0x%08X  (or --max N, or --all)\n",
        va, g_cur_func, va);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* Added after native32's own handler, so callbacks are resolved first and
 * only real faults get here. */
/* The report goes out through WriteFile from a static buffer, not stdio. A
 * fault on a worker thread while another thread holds the CRT's stderr lock
 * (the bridge traces every native call through it), or a stack overflow with
 * no stack left for fprintf, otherwise ended the process with exit code 3 and
 * no report at all. */
static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    if (r->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        static const char msg[] = "\n=== fault 0xC00000FD: host stack overflow in lifted code ===\n";
        DWORD w;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof msg - 1, &w, NULL);
    }
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    probe_report();
    input_report();
    record_close();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

int main(int argc, char** argv) {
    const char* exe = "_work\\game\\legends.exe";
    const char* game = "_work\\game";
    char exe_full[MAX_PATH];
    int run = 0;
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (!n) n = input_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--simd")) g_simd = 1;
        else if (!strcmp(argv[i], "--watch32") && i + 1 < argc) {
            const char* w = argv[++i];          /* ADDR, or *PTR+OFF to follow a pointer */
            if (*w == '*') {
                char* e;
                g_watch32_ptr = strtoul(w + 1, &e, 0);
                g_watch32_off = *e == '+' ? strtoul(e + 1, NULL, 0) : 0;
            } else g_watch32 = strtoul(w, NULL, 0);
        }
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc && g_nprobe < MAX_PROBES)
            g_probe[g_nprobe++] = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) g_record = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_record_frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else {
            printf("usage: rol [--run] [--headless] [--record out.mp4] [--frames N] [--move x,y@s] [--click x,y@s] [--key vk@s] [--simd] [--exe _work\\game\\legends.exe] [--game _work\\game]\n"
                   "                 [--watchdog S] [--native-trace] [--callbacks]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(exe, MAX_PATH, exe_full, NULL);
    if (g_record) {                    /* the run chdirs into game\ */
        static char rec_full[MAX_PATH];
        GetFullPathNameA(g_record, MAX_PATH, rec_full, NULL);
        g_record = rec_full;
    }
    {
        char gd[MAX_PATH];
        GetFullPathNameA(game, MAX_PATH, gd, NULL);
        _snprintf(g_guest_exe, sizeof g_guest_exe - 1, "%s\\legends.exe", gd);
        _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\"", g_guest_exe);
    }

    /* binkw32.dll and the PhysX DLLs ship in the game folder, not System32,
     * and native32_bind loads imports with a plain LoadLibraryA. */
    {
        char gd[MAX_PATH];
        GetFullPathNameA(game, MAX_PATH, gd, NULL);
        SetDllDirectoryA(gd);
    }
    native32_init();
    hide_simd();
    AddVectoredExceptionHandler(0, crash);
    printf("Rise of Legends recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    uint32_t span = native32_map(exe_full, ROL_IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, ROL_IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", exe, ROL_IMAGE_BASE, ROL_IMAGE_BASE + span);
    if (g_headless) {                  /* headless also takes the scripted-input shims */
        int nh = (int)(sizeof g_headless_shims / sizeof g_headless_shims[0]);
        native32_shim_t* all = (native32_shim_t*)calloc(nh + INPUT_NSHIMS, sizeof *all);
        memcpy(all, g_headless_shims, sizeof g_headless_shims);
        memcpy(all + nh, g_input_shims, sizeof g_input_shims);
        if (native32_bind(ROL_IMAGE_BASE, all, nh + INPUT_NSHIMS)) return 1;
    } else if (native32_bind(ROL_IMAGE_BASE, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]))) {
        return 1;
    }
    printf("  guest exe %s\n", g_guest_exe);

    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", rol_entry_va);
        return 0;
    }
    /* The game opens Data\ relative to its working directory, like the original. */
    if (!SetCurrentDirectoryA(game)) { fprintf(stderr, "cannot enter %s\n", game); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    input_start();
    if (g_watch32 || g_watch32_ptr) CloseHandle(CreateThread(NULL, 0, watch32, NULL, 0, NULL));
    printf("  entering 0x%08X\n\n", rol_entry_va);
    fflush(stdout);
    native32_call_guest(rol_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}
