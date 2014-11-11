#include "zmem.h"
#include "debug.h"

LPVOID zalloc(__in DWORD dwSize)
{
	LPBYTE pMem = (LPBYTE) malloc(dwSize);
	RtlSecureZeroMemory(pMem, dwSize);
#ifdef _DEBUG
	//OutputDebug(L"[*] Alloc => %08x\n", pMem);
#endif
	return(pMem);
}

VOID zfree(__in LPVOID pMem)
{ 
#ifdef _DEBUG
	//OutputDebug(L"[*] Free => %08x\n", pMem);
#endif

	if (pMem) 
		free(pMem); 
}

void znfree(__in LPVOID* pMem)
{ 
	if(pMem == NULL)
		return;

	if(*pMem == NULL) 
		return;

	//free memory and set to null
	free(*pMem); 
	*pMem = NULL;
}


void zndelete(__in LPVOID* pMem)
{ 
	if(pMem == NULL)
		return;

	if(*pMem == NULL) 
		return;

	//free memory and set to null
	delete(*pMem); 
	*pMem = NULL;

}
