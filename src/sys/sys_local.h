#pragma once

#include "../qcommon/qcommon.h"

void 		IN_Init(void *windowData);
void 		IN_Frame(void);
void 		IN_Shutdown(void);
void 		IN_Restart(void);

void		Sys_PlatformInit(int argc, char *argv[]);
void		Sys_PlatformExit(void);
char		*Sys_ConsoleInput(void);
void 		Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr);
void		Sys_SigHandler(int signal);
void		Sys_SetTaskbarState(void *win_handle, tbstate_t state, uint64_t current, uint64_t total);
#ifndef _WIN32
void		Sys_AnsiColorPrint(const char *msg, bool extendedColors);
#endif
