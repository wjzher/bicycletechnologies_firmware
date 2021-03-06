#include <cmath>
#include "trace.h"
#include "tm_stm32_gps.h"
#include "tm_stm32_delay.h"

#define M_PI (3.141592653589793)

static TM_GPS_Data_t GPS_Data;
static TM_GPS_Float_t GPS_Float_Lat;
static TM_GPS_Float_t GPS_Float_Lon;
static TM_GPS_Float_t GPS_Float_Alt;
static TM_GPS_Distance_t GPS_Distance;
bool isRTCSet;

void retrieveGPS();

int long2tilex(double lon, int zoomlevel, int *tileOffset) 
{ 
	double tmp = (lon + 180.0) / 360.0 * pow(2.0, zoomlevel);
	int ret = (int)(floor(tmp));
	*tileOffset = (int)((tmp - ret)*256);
	return ret; 
}

int lat2tiley(double lat, int zoomlevel, int *tileOffset)
{ 
	double tmp = (1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, zoomlevel);
	int ret = (int)(floor(tmp));
	*tileOffset = (int)((tmp - ret)*256);
	return ret; 
}

double tilex2long(int x, int z) 
{
	return x / pow(2.0, z) * 360.0 - 180;
}

double tiley2lat(int y, int z) 
{
	double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
	return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

void runGPS(){	
		/* Delay init */
	TM_DELAY_Init();
	
	/* Initialize GPS on 9600 baudrate */
	TM_GPS_Init(&GPS_Data, 9600);
	
	isRTCSet = false;
		
	while(1) {
		
	}
}

void retrieveGPS(){
	TM_GPS_Result_t result, current;
	char buffer[40];
	float temp;
	
	TM_DELAY_SetTime(0);
	
	/* Update GPR data */
	/* Call this as faster as possible */
	result = TM_GPS_Update(&GPS_Data);
	/* If we didn't receive any useful data in the start */
	if (result == TM_GPS_Result_FirstDataWaiting && TM_DELAY_Time() > 3000) {
		/* If we didn't receive nothing within 3 seconds */
		TM_DELAY_SetTime(0);
		TRACE("GPS:,nothing received after 3 seconds\n");
	}
	/* If we have any unread data */
	if (result == TM_GPS_Result_NewData) {
		/* We received new packet of useful data from GPS */
		current = TM_GPS_Result_NewData;
		
		saveGPS(&GPS_Data);
		
		/* Is GPS signal valid? */
		if (GPS_Data.Validity) {
			/* If you want to make a GPS tracker, now is the time to save your data on SD card */
			if(!isRTCSet){
				TM_RTC_t rtcd;
				getRTC(&rtcd, TM_RTC_Format_BIN);
				uint32_t rtcTime = TM_RTC_GetUnixTimeStamp(&rtcd);
				rtcd.Year = GPS_Data.Date.Year;
				rtcd.Month = GPS_Data.Date.Month;
				rtcd.Day = GPS_Data.Date.Date;
				rtcd.Hours = GPS_Data.Time.Hours;
				rtcd.Minutes = GPS_Data.Time.Minutes;
				rtcd.Seconds = GPS_Data.Time.Seconds;
				rtcd.Subseconds = GPS_Data.Time.Hundredths;
				uint32_t gpsTime = TM_RTC_GetUnixTimeStamp(&rtcd);
				
				if ( ((rtcTime >= gpsTime) && ((rtcTime-gpsTime)>5)) || ((gpsTime >= rtcTime) && ((gpsTime-rtcTime)>5))){
					if(updateRTC(&rtcd, TM_RTC_Format_BIN) != TM_RTC_Result_Ok){
						TRACE("GPS:,ERROR: Saving RTC Based on GPS Failed\n");
					};
					closeTraceFile();
					openTraceFile();
					TRACE("GPS:,Updated RTC based on GPS\n");
				}
				isRTCSet = true;
			}
			
			/* We have valid GPS signal */
			TRACE("GPS:,New GPS Data Received\n");
#ifndef GPS_DISABLE_GPGGA
			/* Latitude */
			/* Convert float to integer and decimal part, with 6 decimal places */
			TM_GPS_ConvertFloat(GPS_Data.Latitude, &GPS_Float_Lat, 6);
			
			/* Longitude */
			/* Convert float to integer and decimal part, with 6 decimal places */
			TM_GPS_ConvertFloat(GPS_Data.Longitude, &GPS_Float_Lon, 6);
			
			/* Altitude */
			/* Convert float to integer and decimal part, with 6 decimal places */
			TM_GPS_ConvertFloat(GPS_Data.Altitude, &GPS_Float_Alt, 6);

			TRACE("GPS:,Latitude=%d.%d,Longitude=%d.%d,SatsInUse=%02d,Date=% 4d/%02d/%02d,UTCTime=%02d.%02d.%02d:%02d,Fix=%d\n", GPS_Float_Lat.Integer, GPS_Float_Lat.Decimal, 
			GPS_Float_Lon.Integer, GPS_Float_Lon.Decimal, GPS_Data.Satellites, GPS_Data.Date.Year, GPS_Data.Date.Month, GPS_Data.Date.Date,
			GPS_Data.Time.Hours, GPS_Data.Time.Minutes, GPS_Data.Time.Seconds, GPS_Data.Time.Hundredths, 
			GPS_Data.Fix, GPS_Float_Alt.Integer, GPS_Float_Alt.Decimal); 
#endif
		} else {
			/* GPS signal is not valid */
			TRACE("GPS:,Data received is not valid\n");
		}
	} else if (result == TM_GPS_Result_FirstDataWaiting && current != TM_GPS_Result_FirstDataWaiting) {
		current = TM_GPS_Result_FirstDataWaiting;
		//TM_USART_Puts(USART3, "Waiting first data from GPS!\n");
	} else if (result == TM_GPS_Result_OldData && current != TM_GPS_Result_OldData) {
		current = TM_GPS_Result_OldData;
		/* We already read data, nothing new was received from GPS */
	}
}