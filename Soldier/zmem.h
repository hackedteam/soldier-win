#ifndef _ZALLOC_H
#define _ZALLOC_H

#include <Windows.h>

LPVOID zalloc(__in DWORD dwSize);
VOID zfree(__in LPVOID lpMem);
void znfree(__in LPVOID* pMem);
void zndelete(__in LPVOID* pMem);
LPVOID zalloc_s(__in size_t dwSize );
VOID zfree_s(__in LPVOID pMem);

#endif // endif