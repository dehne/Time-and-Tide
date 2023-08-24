/***
 * 
 * Tides and Currents API example v0.5.0 configuration header file
 * 
 * See main.ccp for more information
 * 
 *
 ****
 *
 * Copyright 2023 by D.L. Ehnebuske
 * License: GNU Lesser General Public License v2.1
 * 
 ***/

// The default WiFi credentials to use (Don't modify this; use the interactive commands to set actual values.)
#define TAT_SSID                "Need ssid"
#define TAT_PASSWORD            "Need password"

// The default NOAA station ID for the station we're doing tides for (Port Townsend, WA)
#define TAT_STATION_ID          "9444900"

// The default minimum and maximum water levels (feet above MLLW) to be displayed
#define TAT_STATION_MIN_LEVEL   (-4.3)
#define TAT_STATION_MAX_LEVEL   (12.1)

// The tc_scale_t type of clock face the device has; linear or nonlinear
#define TAT_FACE_TYPE           (tcNonlinear)

// The ESP32 non-volatile name space we use to store our config data
#define TAT_NVS_NAMESPACE       "Tide and Time"
// The ESP32 NVS key name for our signature
#define TAT_NVS_SIG_NAME        "signature"
// The ESP32 NVS key name for our data blob
#define TAT_NVS_DATA_NAME       "datablob"
// The value of the signature on the data stored in nvs
#define TAT_NVS_SIG             (0x2623)

// How long (ms) to wait for the WiFi to connect before declaring failure
#define TAT_WIFI_WAIT_MILLIS    (15000)

// The NTP server to use to set the time
#define TAT_NTP_SERVER          "pool.ntp.org"

// How long to wait for the NTP server to respond before declaring failure
#define TAT_NTP_WAIT_MILLIS     (20000)
#define TAT_NTP_CHECK_MILLIS    (500)

// The definition of "local" time in Posix TZ format. This must match the timezone asked for in 
// the requests to NOAA. Probably best not to change this.
// See https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html for format definition.
// See https://en.wikipedia.org/wiki/List_of_tz_database_time_zones for list of timezones. 
// NB: The UTC offset in the wikipedia list has the opposite sign from the Posix TZ format.
#define TAT_POSIX_TZ            "GMT+0"

// How often to update the water level display (sec)
#define TAT_LEVEL_CHECK_SECS    (360)

// The NOAA server that serves up tides and currents information in response to HTTPS GET requests 
#define TAT_SERVER_URL          "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter"

// The pem for DigiCert Global Root CA, the CA that signed the server certifcate for 
// api.tidesandcurrents.noaa.gov. It's valid until 29 Mar 2031.
// 
// We use this to verify that the site we connect to is really the one we want. Doing it this way 
// is a little fragile since DigiCert is wont to update the cert before it expires, thus 
// invalidating what we have here. But doing full cert management is a lot of work. 
//
// When Digicert changes the cert, getting data from NOAA will fail with "connection 
// refused". To get the new one go to https://www.digicert.com/kb/digicert-root-certificates.htm
// and search for "DigiCert Global G2 TLS RSA SHA256 2020 CA1". There they have a link to 
// download the current PEM.
#define TAT_SERVER_ROOT_CA_PEM  "-----BEGIN CERTIFICATE-----\n"\
                                "MIIEyDCCA7CgAwIBAgIQDPW9BitWAvR6uFAsI8zwZjANBgkqhkiG9w0BAQsFADBh\n"\
                                "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"\
                                "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"\
                                "MjAeFw0yMTAzMzAwMDAwMDBaFw0zMTAzMjkyMzU5NTlaMFkxCzAJBgNVBAYTAlVT\n"\
                                "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxMzAxBgNVBAMTKkRpZ2lDZXJ0IEdsb2Jh\n"\
                                "bCBHMiBUTFMgUlNBIFNIQTI1NiAyMDIwIENBMTCCASIwDQYJKoZIhvcNAQEBBQAD\n"\
                                "ggEPADCCAQoCggEBAMz3EGJPprtjb+2QUlbFbSd7ehJWivH0+dbn4Y+9lavyYEEV\n"\
                                "cNsSAPonCrVXOFt9slGTcZUOakGUWzUb+nv6u8W+JDD+Vu/E832X4xT1FE3LpxDy\n"\
                                "FuqrIvAxIhFhaZAmunjZlx/jfWardUSVc8is/+9dCopZQ+GssjoP80j812s3wWPc\n"\
                                "3kbW20X+fSP9kOhRBx5Ro1/tSUZUfyyIxfQTnJcVPAPooTncaQwywa8WV0yUR0J8\n"\
                                "osicfebUTVSvQpmowQTCd5zWSOTOEeAqgJnwQ3DPP3Zr0UxJqyRewg2C/Uaoq2yT\n"\
                                "zGJSQnWS+Jr6Xl6ysGHlHx+5fwmY6D36g39HaaECAwEAAaOCAYIwggF+MBIGA1Ud\n"\
                                "EwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFHSFgMBmx9833s+9KTeqAx2+7c0XMB8G\n"\
                                "A1UdIwQYMBaAFE4iVCAYlebjbuYP+vq5Eu0GF485MA4GA1UdDwEB/wQEAwIBhjAd\n"\
                                "BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwdgYIKwYBBQUHAQEEajBoMCQG\n"\
                                "CCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQAYIKwYBBQUHMAKG\n"\
                                "NGh0dHA6Ly9jYWNlcnRzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RH\n"\
                                "Mi5jcnQwQgYDVR0fBDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQuY29t\n"\
                                "L0RpZ2lDZXJ0R2xvYmFsUm9vdEcyLmNybDA9BgNVHSAENjA0MAsGCWCGSAGG/WwC\n"\
                                "ATAHBgVngQwBATAIBgZngQwBAgEwCAYGZ4EMAQICMAgGBmeBDAECAzANBgkqhkiG\n"\
                                "9w0BAQsFAAOCAQEAkPFwyyiXaZd8dP3A+iZ7U6utzWX9upwGnIrXWkOH7U1MVl+t\n"\
                                "wcW1BSAuWdH/SvWgKtiwla3JLko716f2b4gp/DA/JIS7w7d7kwcsr4drdjPtAFVS\n"\
                                "slme5LnQ89/nD/7d+MS5EHKBCQRfz5eeLjJ1js+aWNJXMX43AYGyZm0pGrFmCW3R\n"\
                                "bpD0ufovARTFXFZkAdl9h6g4U5+LXUZtXMYnhIHUfoyMo5tS58aI7Dd8KvvwVVo4\n"\
                                "chDYABPPTHPbqjc1qCmBaZx2vN4Ye5DUys/vZwP9BFohFrH/6j/f3IL16/RZkiMN\n"\
                                "JCqVJUzKoZHm1Lesh3Sz8W2jmdv51b2EQJ8HmA==\n"\
                                "-----END CERTIFICATE-----\n"
/***
 * Dealing with the API's product=one_minute_water_level request asking for the latest data
 * 
 * The expected result looks like:
 * 
 *      {
 *          "metadata": {
 *          {
 *              "id": "9444900",
 *              "name": "Port Townsend",
 *              "lat": "48.1129",
 *              "lon": "-122.7595"
 *          },
 *          "data": [
 *              {
 *              "t": "2022-01-11 12:59",
 *              "v": "2.426"
 *              }
 *          ]
 *      }
 * 
 ***/

// The request asking for the water level in feet relative to MLLW at a specified station 
// averaged over the last minute to be returned in json format. NOAA asks that individual 
// (non-NOAA) users identify themselves for debugging purposes by the including the 
// "application=<first>_<last>" parameter. The api works without it, but just to be nice...
// Post-pend the desired NOAA station ID in the form "ddddddd" to form the complete request.
#define TAT_GET_WL              "application=David_Ehnebuske&"\
                                "units=english&time_zone=gmt&datum=MLLW&format=json&"\
                                "product=one_minute_water_level&date=latest&station="

// Capacity required for the JsonDocument we'll use to deserialize the result of the above request
// Calculated using https://arduinojson.org/v6/assistant/ (and then rounded up some).
#define TAT_JSON_CAPACITY_WL    (256)

/***
 * 
 * Dealing with the API's product=predictions&interval=hilo asking for the high and low tide data.
 * 
 * We ask for 36 hours of data because it appears that the service ignores the time portion of
 * the begin_date parameter and always starts at midnight of the specified date. It does appear to
 * pay attention to the range parameter specifying the number of hours of data is wanted. We could 
 * probably get away with a bit less range since we only need to be sure we have the next tide 
 * after midnight.
 * 
 * The expected result is shaped like this:
 * 
 *   {
 *       "predictions" : [
 *           {
 *               "t":"2023-01-31 02:50", 
 *               "v":"8.081", 
 *               "type":"H"
 *           },
 *           {
 *               "t":"2023-01-31 06:06", 
 *               "v":"7.435", 
 *               "type":"L"
 *           },
 *           {
 *               "t":"2023-01-31 10:38", 
 *               "v":"8.260", 
 *               "type":"H"
 *           },
 *           {
 *               "t":"2023-01-31 18:50", 
 *               "v":"-0.108", 
 *               "type":"L"
 *           }
 *       ]
 *   }
 * 
 * where there are five or six entries, one for each high/low tide.
 * 
 ***/

// The request asking for the hi/lo tides for the day. Post-pend desired date in the form "yyyymmdd" and
// the desired station in the form "&station=ddddddd" where "ddddddd" is the 7-digit NOAA station ID
#define TAT_GET_PRED_TIDES      "application=David_Ehnebuske&units=english&"\
                                "time_zone=gmt&datum=MLLW&format=json&"\
                                "product=predictions&interval=hilo&range=48&begin_date="

// Capacity required for the JsonDocument we'll use to deserialize the result of the above request
#define TAT_JSON_CAPACITY_TIDES (768)

/***
 *
 * Dealing with the API's product=predictions asking for the current day's predicted water 
 * level data at the default six-minute interval.
 * 
 * The expected result looks like:
 * 
 *    {
 *      "predictions": [
 *          {
 *          "t": "2022-01-09 00:00",
 *          "v": "-1.403"
 *          },
 *          {
 *          "t": "2022-01-09 00:06",
 *          "v": "-1.490"
 *          },
 *          ...
 *      ]
 *    }
 * 
 * where the array has 241 elements, one for every six-minute interval including one at the 
 * beginning and one at the end.
 * 
 ***/

// The request asking for the six-minute water level predictions for 24 hours. Post-pend 
// the desired date in the form "yyyymmdd" and the desired station in the form 
// "&station=ddddddd". So post pend something like "20230224&station=9444900"
#define TAT_GET_PRED_WL         "application=David_Ehnebuske&"\
                                "units=english&time_zone=gmt&datum=MLLW&format=json&"\
                                "range=24&product=predictions&begin_date="

// Capacity for jsonDocument we'll use to deserialize the result of the above request
#define TAT_JSON_CAPACITY_PRED  (24576)

// Number of predictions expected from the above query
#define TAT_N_PRED_WL           (241)
