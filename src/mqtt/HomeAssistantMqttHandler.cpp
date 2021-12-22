#include "HomeAssistantMqttHandler.h"
#include "hexutils.h"
#include "Uptime.h"
#include "web/root/ha1_json.h"
#include "web/root/ha2_json.h"
#include "web/root/ha3_json.h"
#include "web/root/jsonsys_json.h"
#include "web/root/jsonprices_json.h"
#include "web/root/hadiscover1_json.h"
#include "web/root/hadiscover2_json.h"

bool HomeAssistantMqttHandler::publish(AmsData* data, AmsData* previousState) {
	if(topic.isEmpty() || !mqtt->connected())
		return false;

    listType = data->getListType(); // for discovery stuff in publishSystem()
    if(data->getListType() == 3) { // publish energy counts
       if(data->getActiveExportCounter() > 0){
            char json[256];
            snprintf_P(json, sizeof(json), HA2_JSON,
                data->getActiveImportCounter(),
                data->getActiveExportCounter(),
                data->getReactiveImportCounter(),
                data->getReactiveExportCounter(),
                data->getMeterTimestamp()
            );
            mqtt->publish(topic + "/energy", json);
        }
    }
    if(data->getListType() == 1) { // publish power counts
        char json[192];
        snprintf_P(json, sizeof(json), HA1_JSON,
            data->getActiveImportPower()
        );
        return mqtt->publish(topic + "/power", json);
    } else if(data->getListType() == 2 || data->getListType() == 3) {
        char json[768];
        snprintf_P(json, sizeof(json), HA3_JSON,
            data->getListId().c_str(),
            data->getMeterId().c_str(),
            data->getMeterModel().c_str(),
            data->getActiveImportPower(),
            data->getReactiveImportPower(),
            data->getActiveExportPower(),
            data->getReactiveExportPower(),
            data->getL1Current(),
            data->getL2Current(),
            data->getL3Current(),
            data->getL1Voltage(),
            data->getL2Voltage(),
            data->getL3Voltage(),
            data->getPowerFactor() == 0 ? 1 : data->getPowerFactor(),
            data->getPowerFactor() == 0 ? 1 : data->getL1PowerFactor(),
            data->getPowerFactor() == 0 ? 1 : data->getL2PowerFactor(),
            data->getPowerFactor() == 0 ? 1 : data->getL3PowerFactor()
        );
        return mqtt->publish(topic + "/power", json);
    }
    return false;
}

bool HomeAssistantMqttHandler::publishTemperatures(AmsConfiguration* config, HwTools* hw) {
	int count = hw->getTempSensorCount();
    if(count == 0) return false;

	int size = 32 + (count * 26);

	char buf[size];
	snprintf(buf, 24, "{\"temperatures\":{");

	for(int i = 0; i < count; i++) {
		TempSensorData* data = hw->getTempSensorData(i);
        if(data != NULL) {
            char* pos = buf+strlen(buf);
            snprintf(pos, 26, "\"%s\":%.2f,", 
                toHex(data->address, 8).c_str(),
                data->lastRead
            );
            data->changed = false;
            delay(1);
        }
	}
	char* pos = buf+strlen(buf);
	snprintf(count == 0 ? pos : pos-1, 8, "}}");
    return mqtt->publish(topic + "/temperatures", buf);
}

bool HomeAssistantMqttHandler::publishPrices(EntsoeApi* eapi) {
	if(topic.isEmpty() || !mqtt->connected())
		return false;
	if(strlen(eapi->getToken()) == 0)
		return false;

	time_t now = time(nullptr);

	float min1hr, min3hr, min6hr;
	int8_t min1hrIdx = -1, min3hrIdx = -1, min6hrIdx = -1;
	float min = INT16_MAX, max = INT16_MIN;
	float values[24] = {0};
	for(uint8_t i = 0; i < 24; i++) {
		float val = eapi->getValueForHour(now, i);
		values[i] = val;

		if(val == ENTSOE_NO_VALUE) break;
		
		if(val < min) min = val;
		if(val > max) max = val;

		if(min1hrIdx == -1 || min1hr > val) {
			min1hr = val;
			min1hrIdx = i;
		}

		if(i >= 2) {
			i -= 2;
			float val1 = values[i++];
			float val2 = values[i++];
			float val3 = val;
			if(val1 == ENTSOE_NO_VALUE || val2 == ENTSOE_NO_VALUE || val3 == ENTSOE_NO_VALUE) continue;
			float val3hr = val1+val2+val3;
			if(min3hrIdx == -1 || min3hr > val3hr) {
				min3hr = val3hr;
				min3hrIdx = i-2;
			}
		}

		if(i >= 5) {
			i -= 5;
			float val1 = values[i++];
			float val2 = values[i++];
			float val3 = values[i++];
			float val4 = values[i++];
			float val5 = values[i++];
			float val6 = val;
			if(val1 == ENTSOE_NO_VALUE || val2 == ENTSOE_NO_VALUE || val3 == ENTSOE_NO_VALUE || val4 == ENTSOE_NO_VALUE || val5 == ENTSOE_NO_VALUE || val6 == ENTSOE_NO_VALUE) continue;
			float val6hr = val1+val2+val3+val4+val5+val6;
			if(min6hrIdx == -1 || min6hr > val6hr) {
				min6hr = val6hr;
				min6hrIdx = i-5;
			}
		}

	}

	char ts1hr[21];
	if(min1hrIdx > -1) {
        time_t ts = now + (SECS_PER_HOUR * min1hrIdx);
        //Serial.printf("1hr: %d %lu\n", min1hrIdx, ts);
		tmElements_t tm;
        breakTime(ts, tm);
		sprintf(ts1hr, "%04d-%02d-%02dT%02d:00:00Z", tm.Year+1970, tm.Month, tm.Day, tm.Hour);
	}
	char ts3hr[21];
	if(min3hrIdx > -1) {
        time_t ts = now + (SECS_PER_HOUR * min3hrIdx);
        //Serial.printf("3hr: %d %lu\n", min3hrIdx, ts);
		tmElements_t tm;
        breakTime(ts, tm);
		sprintf(ts3hr, "%04d-%02d-%02dT%02d:00:00Z", tm.Year+1970, tm.Month, tm.Day, tm.Hour);
	}
	char ts6hr[21];
	if(min6hrIdx > -1) {
        time_t ts = now + (SECS_PER_HOUR * min6hrIdx);
        //Serial.printf("6hr: %d %lu\n", min6hrIdx, ts);
		tmElements_t tm;
        breakTime(ts, tm);
		sprintf(ts6hr, "%04d-%02d-%02dT%02d:00:00Z", tm.Year+1970, tm.Month, tm.Day, tm.Hour);
	}

    char json[384];
    snprintf_P(json, sizeof(json), JSONPRICES_JSON,
        WiFi.macAddress().c_str(),
        values[0],
        values[1],
        values[2],
        values[3],
        values[4],
        values[5],
        values[6],
        values[7],
        values[8],
        values[9],
        values[10],
        values[11],
        min == INT16_MAX ? 0.0 : min,
        max == INT16_MIN ? 0.0 : max,
        ts1hr,
        ts3hr,
        ts6hr
    );
    return mqtt->publish(topic + "/prices", json);
}

bool HomeAssistantMqttHandler::publishSystem(HwTools* hw) {
	if(topic.isEmpty() || !mqtt->connected()){
        sequence = 0;
		return false;
    }

    if(sequence % 3 == 0){
        char json[192];
        snprintf_P(json, sizeof(json), JSONSYS_JSON,
            WiFi.macAddress().c_str(),
            clientId.c_str(),
            (uint32_t) (millis64()/1000),
            hw->getVcc(),
            hw->getWifiRssi(),
            hw->getTemperature()
        );
        mqtt->publish(topic + "/state", json);
    }
    if(sequence % 60 == 1 && listType > 0){ // every 60 ams message, publish mqtt discovery
        char json[512];
        String haTopic = "homeassistant/sensor/";
        String haUID = "ams-3a08";

        snprintf_P(json, sizeof(json), HADISCOVER1_JSON,
            "AMS reader status",
            (topic + "/state").c_str(),
            (topic + "/state").c_str(),
            (haUID + "_status").c_str(),
            (haUID + "_status").c_str(),
            "dB",
            "rssi",
            haUID.c_str(),
            "AMS reader",
            "ESP32",
            "2.0.0",
            "AmsToMqttBridge"
        );
        mqtt->publish(haTopic + haUID + "_status/config", json);

        snprintf_P(json, sizeof(json), HADISCOVER2_JSON,
            "AMS active import",
            (topic + "/power").c_str(),
            (haUID + "_activeI").c_str(),
            (haUID + "_activeI").c_str(),
            "W",
            "P",
            "power",
            "measurement",
            haUID.c_str()
        );
        mqtt->publish(haTopic + haUID + "_activeI/config", json);

        snprintf_P(json, sizeof(json), HADISCOVER2_JSON,
            "AMS accumulated active energy",
            (topic + "/energy").c_str(),
            (haUID + "_accumI").c_str(),
            (haUID + "_accumI").c_str(),
            "kWh",
            "tPI",
            "energy",
            "total_increasing",
            haUID.c_str()
        );
        mqtt->publish(haTopic + haUID + "_accumI/config", json);

    }
    if(listType>0) sequence++;
    return true;
}
