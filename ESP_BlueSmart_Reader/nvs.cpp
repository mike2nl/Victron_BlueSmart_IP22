/***************************************************************************
  Copyright (c) 2023 WASN.eu

  Licensed under the MIT License. You may not use this file except in
  compliance with the License. You may obtain a copy of the License at

  https://opensource.org/licenses/MIT

***************************************************************************/

#include "config.h"
#include "nvs.h"
#include "victron.h"
#include "mqtt.h"

EEPROM_Rotate EEP;
settings_t settings;
settings_t defaultSettings = {
    60,
    10,
#ifdef MQTT_ENABLE
    true,
#else
    false,
#endif
    MQTT_BROKER_HOSTNAME,
    MQTT_BROKER_PORT,
    MQTT_BASE_TOPIC,
    MQTT_PUBLISH_INTERVAL_SEC,
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    true,
    MQTT_USERNAME,
    MQTT_PASSWORD,
#else
    false,
    "none",
    "none",
#endif
#ifdef MQTT_PUBLISH_JSON
    true,
#else
    false,
#endif
#ifdef MQTT_HA_AUTO_DISCOVERY
    true,
#else
    false,
#endif
#ifdef MQTT_USE_TLS
    true,
#else
    false,
#endif
#ifdef POWER_SAVING_MODE
    true,
#else
    false,
#endif
#ifdef INFLUXDB_ENABLE
    true,
#else
    false,
#endif
#ifdef SYSTEM_ID
    SYSTEM_ID,
#else
    "",
#endif
    0x77
};

static void readNVS() {
    settings_t settingsNVS;
    EEP.get(EEPROM_ADDR, settingsNVS);
    if (settingsNVS.magic == 0x77) {
        memcpy(&settings, &settingsNVS, sizeof(settings_t));
        Serial.printf("Restored system settings from NVS (%d bytes)\n", sizeof(settings)*8);
    } else {
        memcpy(&settings, &defaultSettings, sizeof(settings_t));
        Serial.printf("Initialized NVS (%d bytes) with default setttings\n", sizeof(settings)*8);
        saveNVS(true);
    }
}


void initNVS() {
    EEP.size(3);
    EEP.begin(2048); // must be larger than size of settings_t in nvs.h
    readNVS();
}


// restore default settings
void resetNVS() {
    Serial.println(F("Reset settings to default values"));
    memcpy(&settings, &defaultSettings, sizeof(settings_t));
    saveNVS(false);
}


// save current reading to pseudo eeprom
void saveNVS(bool rotate) {
    Serial.println(F("Save settings to NVS"));
    EEP.put(EEPROM_ADDR, settings);
    EEP.rotate(rotate);
    EEP.commit();
}


// export system settings as JSON string
const char* nvs2json() {
    DynamicJsonDocument JSON(768);
    static char buf[896];
    JSON["readingsIntervalMs"] = settings.readingsIntervalMs;
    JSON["enableMQTT"] = settings.enableMQTT;
    JSON["mqttBroker"] = settings.mqttBroker;
    JSON["mqttBrokerPort"] = settings.mqttBrokerPort;
    JSON["mqttBaseTopic"] = settings.mqttBaseTopic;
    JSON["mqttIntervalSecs"] = settings.mqttIntervalSecs;
    JSON["mqttEnableAuth"] = settings.mqttEnableAuth;
    if (settings.mqttEnableAuth) {
        JSON["mqttUsername"] = settings.mqttUsername;
        JSON["mqttPassword"] = settings.mqttPassword;
    }
    JSON["mqttJSON"] = settings.mqttJSON;
    JSON["mqttSecure"] = settings.mqttSecure;
    JSON["enableHADiscovery"] = settings.enableHADiscovery;
    JSON["enablePowerSavingMode"] = settings.enablePowerSavingMode;
    JSON["enableInflux"] = settings.enableInflux;
    JSON["systemID"] = settings.systemID;
    JSON["version"] = FIRMWARE_VERSION;

    memset(buf, 0, sizeof(buf));
    serializeJsonPretty(JSON, buf, sizeof(buf)-1);
    if (JSON.overflowed())
        return NULL;
    return buf;
}


// restore system settings from uploaded JSON file
bool json2nvs(const char* buf, size_t size) {
    DynamicJsonDocument JSON(1024);
    uint16_t mqttIntervalMinSecs;

    DeserializationError error = deserializeJson(JSON, buf, size);
    if (error) {
        Serial.printf("Failed to import settings, deserialize error %s!\n", error.c_str());
        return false;
    } else if (JSON["version"] > FIRMWARE_VERSION) {
        Serial.printf("Failed to import settings, invalid configuration v%d!\n", int(JSON["version"]));
        return false;
    }

    if (JSON["readingsIntervalMs"] >= READINGS_INTERVAL_MS_MIN && JSON["readingsIntervalMs"] <= READINGS_INTERVAL_MS_MAX)
        settings.readingsIntervalMs = JSON["readingsIntervalMs"];
        
    settings.enableMQTT = JSON["enableMQTT"];
    if (strlen(JSON["mqttBroker"]) >= MQTT_BROKER_LEN_MIN)
        strlcpy(settings.mqttBroker, JSON["mqttBroker"], sizeof(settings.mqttBroker));
    if (JSON["mqttBrokerPort"] > 1023)
        settings.mqttBrokerPort = JSON["mqttBrokerPort"];
    if (strlen(JSON["mqttBaseTopic"]) >= MQTT_BASETOPIC_LEN_MIN)
        strlcpy(settings.mqttBaseTopic, JSON["mqttBaseTopic"], sizeof(settings.mqttBaseTopic));

    settings.enablePowerSavingMode = JSON["enablePowerSavingMode"];
    if (settings.enablePowerSavingMode)
        mqttIntervalMinSecs = MQTT_INTERVAL_MIN_POWERSAVING;
    else
        mqttIntervalMinSecs = MQTT_INTERVAL_MIN;
    if (JSON["mqttIntervalSecs"] >= mqttIntervalMinSecs && JSON["mqttIntervalSecs"] <= MQTT_INTERVAL_MAX)
        settings.mqttIntervalSecs = JSON["mqttIntervalSecs"];

    settings.mqttEnableAuth = JSON["mqttEnableAuth"];
    if (settings.mqttEnableAuth) {
        if (strlen(JSON["mqttUsername"]) >= MQTT_USER_LEN_MIN)
            strlcpy(settings.mqttUsername, JSON["mqttUsername"], sizeof(settings.mqttUsername));
        if (strlen(JSON["mqttPassword"]) >= MQTT_PASS_LEN_MIN)
            strlcpy(settings.mqttPassword, JSON["mqttPassword"], sizeof(settings.mqttPassword));
    }
    settings.mqttJSON = JSON["mqttJSON"];
    settings.enableHADiscovery = JSON["enableHADiscovery"];
    settings.mqttSecure = JSON["mqttSecure"];

    settings.enableInflux = JSON["enableInflux"];
    if (strlen(JSON["systemID"]) >= METER_ID_LEN_MIN)
        strlcpy(settings.systemID, JSON["systemID"], sizeof(settings.systemID));

    Serial.println(F("Successfully imported settings"));
    saveNVS(true);

    return true;
}
