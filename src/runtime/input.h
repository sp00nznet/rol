/* Scripted input for headless runs: see input.c. */
#ifndef TM_INPUT_H
#define TM_INPUT_H
#include <windows.h>
#include "native32.h"

extern HWND g_game_hwnd;

/* Consume --click x,y@s or --key vk@s at argv[i]; returns entries taken. */
int  input_arg(int argc, char** argv, int i);
/* Start playing the script (no-op without events). */
void input_start(void);

/* host.c's GetProcAddress shim hands DirectInput8Create through this. */
FARPROC input_wrap_proc(const char* name, FARPROC real);
void input_report(void);

#define INPUT_NSHIMS 4
extern native32_shim_t g_input_shims[INPUT_NSHIMS];

#endif
