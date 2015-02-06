#ifndef _DEBUG_H
#define _DEBUG_H

#include <Windows.h>

VOID OutputDebug(LPWSTR strFormat, ...);

VOID HexDump(WCHAR *desc, VOID *addr, UINT len);

#endif // _DEBUG_H