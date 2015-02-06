#ifndef _PHOTO_H
#define _PHOTO_H

#include <Windows.h>

typedef struct _PHOTO_ADDITIONAL_HEADER
{
#define LOG_PHOTO_VERSION 2015012601
	UINT	uVersion;
	CHAR	strJsonLog[0];
} PHOTO_ADDITIONAL_HEADER, *LPPHOTO_ADDITIONAL_HEADER;

typedef struct _PHOTO_LOGS
{
	DWORD	dwSize;
	LPBYTE	lpBuffer;
} PHOTO_LOGS, *LPPHOTO_LOGS;


#define MAX_PHOTO_QUEUE 1000
extern PHOTO_LOGS lpPhotoLogs[MAX_PHOTO_QUEUE];

VOID PhotoMain();
BOOL QueuePhotoLog(__in LPBYTE lpEvBuff, __in DWORD dwEvSize);

#endif