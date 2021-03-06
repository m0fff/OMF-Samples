/*

©2009-2018 OSIsoft, LLC. All Rights Reserved.

No Warranty or Liability.  The OSIsoft Samples contained herein are being supplied to Licensee
"AS IS" without any warranty of any kind.  OSIsoft DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES,
INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE and NONINFRINGEMENT. In no event will OSIsoft be liable to Licensee or to any third party
for damages of any kind arising from Licensee's use of the OSIsoft Samples OR OTHERWISE, including
but not limited to direct, indirect, special, incidental and consequential damages, and Licensee
expressly assumes the risk of all such damages.  FURTHER, THE OSIsoft SAMPLES ARE NOT ELIGIBLE FOR
SUPPORT UNDER EITHER OSISOFT'S STANDARD OR ENTERPRISE LEVEL SUPPORT AGREEMENTS

*/

/* main.c */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "baseBuild.h"
#include "buildTypes.h"
#include "buildStreams.h"
#include "buildObjects.h"
#include "buildData.h"
#include "getValuesAndStreams.h"

#include "bufferData.h"
#include "sendToServerC.h"
#include "testTxMessages.h"
#include "main.h"

/* Revision history
   Version   Date           Who             Comments
   1.0.0.0   04-May-2016    ETam            initial version
   1.0.1.0   13-May-2016    ETam            handle 503 (Service Unavailable) return from Relay 
   1.0.2.0   16-May-2016    Etam            remove strcpy() and sprintf() from json.c
   1.0.3.0   17-May-2016    Etam            always use https
   1.0.3.1   18-May-2016    Etam            improve variable names and use enum in EngineType
   1.1.0.0   26-Aug-2016    ETam            send certificate to Relay
   1.1.1.0   29-Aug-2016    ETam            verify identity of Relay
   1.2.0.0	 30-Nov-2016	KShu			OMF v0.11rc1 first commit
   1.3.0.0   20-Jun-2018    mlagro          OMF 1.0
   1.4.0.0   21-Apr-2018    mof             OMF 1.0/Relay 2.2.16.1212
*/
#define g_VERSION  "1.4.0.0  21-Apr-2018"

#define RELAY_PAGE  "/ingress/messages"
#define HDR_VERSION  "1.0"
#define MSGFORMAT_JSON  "JSON"
#define MSGTYPE_TYPE  "type"
#define MSGTYPE_CONTAINER "container"
#define MSGTYPE_DATA	"data"
#define ACTION_CREATE  "create"
#define ACTION_UPDATE  "update"

#define RELAY_CERT_FILE  "server.pem"

extern struct Vehicle g_theVehicles[];
extern struct Engine g_theEngines[];
extern struct Transmission g_theTransmissions[];

extern char g_validVehStatus[NUM_VALID_VEHSTATUS][STRING_LENGTH_MED];
extern double g_vehicleFloat[NUM_VEHICLE_FLOAT];
extern int g_vehicleInt[NUM_VEHICLE_INT];

/* declartion of functions implemented in other files */
void SendCertToRelay(char* szRelay, char* szPort, char* szConnectorName, char* szOneTimePassword);
char* GetFullPath();
/* end declartion of functions implemented in other files */

/*
This function returns the fully qualified path to the certificate file that the Relay uses.
"Relay uses" means the file that has the thumbprint as specified in certhash:
   netsh http> netsh http add sslcert ipport=0.0.0.0:5460 
      certhash=b68adab6332d57678cc2ef6b56f9affc4d9c412c 
	  appid={ABCD272C-433A-492B-99A7-B307AECD0423}

This certificate file must be in PEM format.

The developer is responsible for putting this certificate file onto the connector computer.
*/
char* GetRelayCertFile()
{
	static char szFile[PATH_LENGTH];
	snprintf(szFile, PATH_LENGTH, "%s%s", GetFullPath(), RELAY_CERT_FILE);

	return szFile;
}

/*
This function logs information. Right now, we simply write to the console. 
Of course, we can add code that writes to a file on disk or to the system log.
*/
void logMsg(const char* szMsg)
{
	time_t tNow = time(NULL);
#ifdef _WIN32
	char str[26];
	struct tm local;
	localtime_s(&local, &tNow);
	asctime_s(str, sizeof str, &local);
	printf("%s", str);
#elif linux
	struct tm* local = localtime(&tNow);
	printf("%s", asctime(local));
#else
	need to implement
#endif		

	printf("%s", szMsg);
	printf("\n\n");
}

/*
This function handles the logging of information regarding attempts to send messages
to the Relay.
*/
int handlePostReturn(int ret, int http_response_code, const char* szMsg)
{
	char szMsgLog[STRING_LENGTH_LARGE];
	int retval = 0;

	if (ret) {
		if (INFO_WROTE_MSG_TO_BUFFER == ret) {
			snprintf(szMsgLog, STRING_LENGTH_LARGE, 
				"Info> Buffer file has data; message [%s] appended to buffer file", szMsg);
		}
		else {
			snprintf(szMsgLog, STRING_LENGTH_LARGE, "%s error> %s", COMM_LIB_NAME, getCommLibErr());
			retval = -1;
		}
		logMsg(szMsgLog);
	}
	else {
		if (202 == http_response_code || 200 == http_response_code) {
			snprintf(szMsgLog, STRING_LENGTH_LARGE, "%s--good http response! (%d)", szMsg, http_response_code);
			logMsg(szMsgLog);
		}
		else {
			snprintf(szMsgLog, STRING_LENGTH_LARGE, "%s--bad http response (%d)", szMsg, http_response_code);
			logMsg(szMsgLog);
		}
	}

	return retval;
}

void usage()
{
	printf("Usage: program.exe RelayComputer PortNum [tests]|[ConnectorName OneTimePassword]\n");
#ifdef _WIN32
	printf("For example:\n");
	printf("C:> program.exe RelayComputer 8118\n"); 
	printf("or\n");
	printf("C:> program.exe RelayComputer 8118 tests\n");
	printf("or\n");
	printf("C:> program.exe RelayComputer 8118 MyOmfConnector 123456\n");
#elif linux
	printf("For example:\n");
	printf("$ ./program.exe RelayComputer 8118\n");
	printf("or\n");
	printf("$ ./program.exe RelayComputer 8118 tests\n");
	printf("or\n");
	printf("$ ./program.exe RelayComputer 8118 MyOmfConnector 123456\n");
#else
	need to implement
#endif
}

/* Function to send Types, Streams, and non-time series Values 
*/
int sendCoreValues()
{
	char* sTX;  /* JSON array of values that we send to the Relay */

	int ret;  /* return value for calls to communications library (e.g., libCurl) */
	int http_response_code;

	/* send messagetype:Type to Relay */
	sTX = defineTypes();
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_TYPE, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Type message");
	if (ret) return ret;

	/* send messagetype:Container (Engine RPM) to Relay */
	sTX = getStreams_RTValues(RTVAL_ENGINE_RPM);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Engine RPM) message");
	if (ret) return ret;

	/* send messagetype:Container (Engine CoolantTemp) to Relay */
	sTX = getStreams_RTValues(RTVAL_ENGINE_COOLANTTEMP);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Engine CoolantTemperature) message");
	if (ret) return ret;

	/* send messagetype:Container (Vehicle Meas) to Relay */
	sTX = getStreams_RTValues(RTVAL_VEHICLE_MEAS);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Vehicle Measurements) message");
	if (ret) return ret;

	/* send messagetype:Container (values streams, Vehicle Status) to Relay */
	sTX = getStreams_RTValues(RTVAL_VEHICLE_STATUS);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Vehicle Status) message");
	if (ret) return ret;

	/* send messagetype:Container (values streams, Vehicle Float) to Relay */
	sTX = getStreams_RTValues(RTVAL_VEHICLE_FLOAT);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Vehicle Float) message");
	if (ret) return ret;

	/* send messagetype:Container (values streams, Vehicle Int) to Relay */
	sTX = getStreams_RTValues(RTVAL_VEHICLE_INT);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_CONTAINER, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Stream (Vehicle Int) message");
	if (ret) return ret;

	/* send messagetype:Data (Vehicle assets) to Relay */
	sTX = defineVehicleAssets(g_theVehicles, NUM_VEHICLES);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (vehicle assets) message");
	if (ret) return ret;

	/* send messagetype:Data (Engine assets) to Relay */
	sTX = defineEngineAssets(g_theEngines, NUM_ENGINES);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (engine assets) message");
	if (ret) return ret;

	/* send messagetype:Data (Transmission assets) to Relay */
	sTX = defineTransmissionAssets(g_theTransmissions, NUM_TRANSMISSIONS);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (transmission assets) message");
	if (ret) return ret;

	/* send messagetype:Data (Link between Vehicle-Engine) */
	sTX = getAssets_Asset_links(ASSET_LINK_VEHICLE_ENGINE);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (links between a vehicle and engine) message");
	if (ret) return ret;

	/* send messagetype:Data (Link between Vehicle-Transmission) */
	sTX = getAssets_Asset_links(ASSET_LINK_VEHICLE_TRANSMISSION);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (links between a vehicle and transmission) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Engine RPM) */
	sTX = getAssets_Value_Links(RTVAL_ENGINE_RPM);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Engine RPM) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Engine CoolantTemp) */
	sTX = getAssets_Value_Links(RTVAL_ENGINE_COOLANTTEMP);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Engine CoolantTemp) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Vehicle Meas) */
	sTX = getAssets_Value_Links(RTVAL_VEHICLE_MEAS);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Vehicle Meas) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Vehicle Status) */
	sTX = getAssets_Value_Links(RTVAL_VEHICLE_STATUS);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Vehicle Status) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Vehicle Float) */
	sTX = getAssets_Value_Links(RTVAL_VEHICLE_FLOAT);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Vehicle Float) message");
	if (ret) return ret;

	/* send messagetype:Data (real-time value link, Vehicle Int) */
	sTX = getAssets_Value_Links(RTVAL_VEHICLE_INT);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (real-time value link, Vehicle Int) message");
	if (ret) return ret;

	/* send messagetype:Data (Link between _ROOT and Vehicle(s)) */
	sTX = getAssets_Asset_links(ASSET_LINK_VEHICLE_VEHICLE);
	ret = sendMsgToRelay(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	handlePostReturn(ret, http_response_code, "Object (links between _ROOT and each vehicle) message");
	if (ret) return ret;

	return 0;
}

/*
This function handles the processing of the buffer file.
*/
void HandleBuffering(int ret, time_t* tSendCoreValues)
{
	if (INFO_WROTE_MSG_TO_BUFFER == ret) {
		time_t tNow = time(NULL);
		/* try at most every 30 seconds */
		if (tNow >= *tSendCoreValues + 30) {
			*tSendCoreValues = tNow;
			if (0 == sendCoreValues()) {
				ProcessBufferFile();
			}
		}
	}
}

int main(int argc, char** argv)
{
	char* sTX;  /* JSON array of values that we send to the Relay */

	int ret;  /* return value for calls to communications library (e.g., libCurl) */
	int http_response_code;

	char hostInPost[STRING_LENGTH_MED];  /* text such as etam7470:8118 in the POST message */
	int nPort;  /* port number that Relay is listening on*/

	int testingOnly = 0;  /* test various functions */

/* Updated for 201803beta */
#ifdef _WIN32
	char* szProducerToken = "uid=ad9fc9c4-856f-436a-8b88-69f8fce6a03a&sig=hNWA0/dQXtl3LK6fuVP/xz5NvkkH8CL+acbuAuw8raY=";
#elif linux
	char* szProducerToken = "uid=ad9fc9c4-856f-436a-8b88-69f8fce6a03a&sig=hNWA0/dQXtl3LK6fuVP/xz5NvkkH8CL+acbuAuw8raY=";
#else
	need to support
#endif

	char szMsg[STRING_LENGTH_LARGE];
	snprintf(szMsg, STRING_LENGTH_LARGE, "Starting OMFDemo client application v%s", g_VERSION);
	logMsg(szMsg);	
	
	if (argc != 3 && argc != 4 && argc != 5) {
		usage();
		return -1;
	}

	nPort = atoi(argv[2]);
	if (nPort < 1024) {
		logMsg("port number must be at least 1024");
		return -1;
	}

	if (5 == argc) {
		SendCertToRelay(argv[1], argv[2], argv[3], argv[4]);
		return 0;
	}

	/* argv[3] can be "tests" */
	if (4 == argc) {
		if (argv[3][0] == 't' || argv[3][0] == 'T') {
			testingOnly = 1;
		}
	}

	/* simulate data for Vehicles, Engines, Transmissions */
	StartSimulator();

	if (1 == testingOnly) {
		logMsg("Running tests");
		runTests();
		logMsg("Finished running tests");

		return 0;
	}

	if (0 != initCommLib()) {
		logMsg("Cannot start communications library");
		return -1;
	}

	if (0 != setUpBuffering()) {
		logMsg("Cannot set up data buffering");
		return -1;
	}
	
	/* argv[1] is the Relay computer name */
	/* argv[2] is the port number that Relay listens on */
	snprintf(hostInPost, STRING_LENGTH_MED, "%s:%s", argv[1], argv[2]);

	/* set values for parameters that remain constant across messages */
	setServerParams(hostInPost, RELAY_PAGE, szProducerToken, HDR_VERSION, MSGFORMAT_JSON);

	for (;;) {
		if (0 == sendCoreValues()) {
			break;
		}
		else {
			logMsg("Waiting 30 seconds before retrying ...");
#ifdef _WIN32
			Sleep(30000);
#elif linux
			usleep(30000000);
#else
			need to implement
#endif
		}
	}

	time_t tSentCoreValues = time(NULL);

	/* send a messagetype:Data (one RPM value) */
	sTX = getOneEngineValue(RTVAL_ENGINE_RPM, g_theEngines[0].szName);
	ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	HandleBuffering(ret, &tSentCoreValues);

	/* send a messagetype:Data (one CoolantTemp value) */
	sTX = getOneEngineValue(RTVAL_ENGINE_COOLANTTEMP, g_theEngines[1].szName);
	ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
	free(sTX);
	HandleBuffering(ret, &tSentCoreValues);

	{
		char szStreamId[STREAM_ID_LEN];

		/* send a messagetype:Data (one VehicleStatus value) */
		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleStatus, g_theVehicles[0].szName);
		sTX = sendOneValStr(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleStatus, g_validVehStatus[0]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleStatus, g_theVehicles[1].szName);
		sTX = sendOneValStr(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleStatus, g_validVehStatus[1]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleStatus, g_theVehicles[2].szName);
		sTX = sendOneValStr(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleStatus, g_validVehStatus[2]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		/* send a messagetype:Data (one Vehicle Float value) */
		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleFloat, g_theVehicles[0].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleFloat, g_vehicleFloat[3]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleFloat, g_theVehicles[1].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleFloat, g_vehicleFloat[2]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleFloat, g_theVehicles[2].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleFloat, g_vehicleFloat[1]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		/* send a messagetype:Data (one Vehicle Int value) */
		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleInt, g_theVehicles[0].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleInt, g_vehicleInt[0]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleInt, g_theVehicles[1].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleInt, g_vehicleInt[1]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);

		getRTStreamId(szStreamId, STREAM_ID_LEN, STREAM_VStr_vVehicleInt, g_theVehicles[2].szName);
		sTX = sendOneVal(szStreamId, KEY_IndexedDateTime, get_currtime(), KEY_vVehicleInt, g_vehicleInt[2]);
		ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
		free(sTX);
		HandleBuffering(ret, &tSentCoreValues);
	}

	/* send messagetype:Data (real-time data, multiple values in a stream) */
	{		
		char szMsg[STRING_LENGTH_MED];
		int runDurationMinutes = 20;
		int runDuration = 60 * runDurationMinutes; 
		int collectData = 0;
		int needToCollect = 1;
		time_t tStart = time(NULL);
		snprintf(szMsg, STRING_LENGTH_MED, "Collecting data for %d minutes ...", runDuration/60);
		logMsg(szMsg);
		
		for (;;) {
			time_t tNow = time(NULL);
			if (tNow > tStart + runDuration) {
				break;
			}

			/* get values at every 10-second mark */
			if (0 == (tNow % 10)) {
				if (needToCollect) {
					collectData = 1;
				}
			}
			else {
				needToCollect = 1;
			}

			if (1 == collectData) {
				logMsg("Time to collect data");
				collectData = 0;
				needToCollect = 0;

				sTX = getValues_EngineRPMData();
				ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
				free(sTX);
				HandleBuffering(ret, &tSentCoreValues);

				sTX = getValues_EngineCoolantTempData();
				ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
				free(sTX);
				HandleBuffering(ret, &tSentCoreValues);

				sTX = getValues_VehicleData();
				ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_CREATE, sTX);
				free(sTX);
				HandleBuffering(ret, &tSentCoreValues);
			}

#ifdef _WIN32
			Sleep(1000);
#elif linux
			usleep(1000000);
#else
			need to implement
#endif
		}
		logMsg("Finished collecting data");
	}

	/* update attributes for the second Transmission */		
	snprintf(g_theTransmissions[1].szSerialNumber, STRING_LENGTH_MED, "%s", "T_updated_SN");
	sTX = defineTransmissionAssets(&(g_theTransmissions[1]), 1);
	ret = processPostMsg(&http_response_code, MSGTYPE_DATA, ACTION_UPDATE, sTX);
	free(sTX);

	endCommLib();

	logMsg("Ending OMFDemo client application");

	return 0;
}
