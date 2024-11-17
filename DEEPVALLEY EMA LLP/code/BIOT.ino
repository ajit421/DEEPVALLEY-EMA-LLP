#include <WiFi.h>
#include <az_iot.h>
#include <az_core.h>
#include <azure_ca.h>
#include <az_iot_hub_client.h>
#include <az_iot_hub_client_properties.h>
#include <az_result.h>
#include <az_iot_common.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <time.h>
#include <jbdbms.h>
#include <Arduino.h>
#include <Stream.h>

// Wi-Fi credentials
const char* ssid = "Year2050";
const char* password = "iequaebeiQuohG7u";

// Azure IoT Hub configuration
const char* hub_hostname = "SmartBattery.azure-devices.net";
az_span hub_hostname_span = az_span_create((uint8_t*)hub_hostname, strlen(hub_hostname));
const char* device_id = "EB-test";
az_span device_id_span = az_span_create((uint8_t*)device_id, strlen(device_id));
static const char* device_key = "PHc/tytO5Z+wo8wo5NuDWVc4cW/4tWq1FAIoTNtoxVw=";

// JBD BMS UART configuration
HardwareSerial bmsSerial(1); // UART2 on ESP32
const int bmsRxPin = 18; // Replace with your RX pin number
const int bmsTxPin = 17; // Replace with your TX pin number
const int bmsSerialBaudRate = 9600; // Replace with the correct baud rate for your BMS

// JSON document for sending telemetry data
StaticJsonDocument<512> jsonDoc;

// Azure IoT Hub client
az_iot_hub_client client;
az_iot_hub_client_options options;

// JBD BMS object
uint32_t lastAccessTime;
JbdBms bmsBattery(bmsSerial, &lastAccessTime);

struct BmsData {
  uint16_t totalVoltage;
  int16_t current;
  uint16_t remainingCapacity;
  uint16_t nominalCapacity;
  uint16_t cycles;
  uint16_t productionDate;
  uint16_t balanceStatus;
  uint16_t balanceStatusHigh;
  uint16_t protectionStatus;
  uint8_t softwareVersion;
  uint8_t rsoc;
  uint8_t fetControl;
  uint8_t numBatteryStrings;
  uint8_t numNtc;
  uint16_t cellVoltages[4]; // Adjust the size as per your requirements
  int16_t ntcTemperatures[2]; // Adjust the size as per your requirements
};
BmsData bmsData;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32...");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Initialize JBD BMS UART
  bmsSerial.begin(bmsSerialBaudRate, SERIAL_8N1, bmsRxPin, bmsTxPin);
  Serial.println("JBD BMS UART initialized");

  // Initialize JBD BMS communication
  bmsBattery.begin();
  Serial.println("JBD BMS communication initialized");

  // Initialize Azure IoT Hub client
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  az_result result = az_iot_hub_client_init(&client, hub_hostname_span, device_id_span, &options);
    if (az_result_failed(result)) {
        Serial.println("Failed to initialize Azure IoT Hub client");
        return;
    }
    Serial.println("Azure IoT Hub client initialized");
}

void loop() {
  // Read basic information from JBD BMS
  JbdBms::Status_t statusData;
  if (bmsBattery.getStatus(statusData)) {
    bmsData.totalVoltage = statusData.voltage;
    bmsData.current = statusData.current;
    bmsData.remainingCapacity = statusData.remainingCapacity;
    bmsData.nominalCapacity = statusData.nominalCapacity;
    bmsData.cycles = statusData.cycles;
    bmsData.productionDate = statusData.productionDate;
    bmsData.balanceStatus = statusData.balanceLow;
    bmsData.balanceStatusHigh = statusData.balanceHigh;
    bmsData.protectionStatus = statusData.fault;
    bmsData.softwareVersion = statusData.version;
    bmsData.rsoc = statusData.currentCapacity;
    bmsData.fetControl = statusData.mosfetStatus;
    bmsData.numBatteryStrings = statusData.cells;
    bmsData.numNtc = statusData.ntcs;
    for (uint8_t i = 0; i < bmsData.numNtc; i++) {
      bmsData.ntcTemperatures[i] = JbdBms::deciCelsius(statusData.temperatures[i]);
    }
  }

  // Read cell voltages from JBD BMS
  JbdBms::Cells_t cellsData;
  if (bmsBattery.getCells(cellsData)) {
    for (uint8_t i = 0; i < bmsData.numBatteryStrings; i++) {
      bmsData.cellVoltages[i] = cellsData.voltages[i];
    }
  }

  // Create JSON document with telemetry data
  jsonDoc.clear();
  JsonObject root = jsonDoc.to<JsonObject>();
  root["totalVoltage"] = bmsData.totalVoltage;
  root["current"] = bmsData.current;
  root["remainingCapacity"] = bmsData.remainingCapacity;
  root["nominalCapacity"] = bmsData.nominalCapacity;
  root["cycles"] = bmsData.cycles;
  root["productionDate"] = bmsData.productionDate;
  root["balanceStatus"] = bmsData.balanceStatus;
  root["balanceStatusHigh"] = bmsData.balanceStatusHigh;
  root["protectionStatus"] = bmsData.protectionStatus;
  root["softwareVersion"] = bmsData.softwareVersion;
  root["rsoc"] = bmsData.rsoc;
  root["fetControl"] = bmsData.fetControl;
  root["numBatteryStrings"] = bmsData.numBatteryStrings;
  root["numNtc"] = bmsData.numNtc;

  // Add cell voltages to the JSON document
  JsonObject cellVoltages = root.createNestedObject("cellVoltages");
  for (uint8_t i = 0; i < bmsData.numBatteryStrings; i++) {
    cellVoltages["cell" + String(i + 1)] = bmsData.cellVoltages[i];
  }

  // Add NTC temperatures to the JSON document
  JsonArray ntcTemperatures = root.createNestedArray("ntcTemperatures");
  for (uint8_t i = 0; i < bmsData.numNtc; i++) {
    ntcTemperatures.add(bmsData.ntcTemperatures[i]);
  }

  // Send telemetry data to Azure IoT Hub
  char payload[512];
  serializeJson(root, payload, sizeof(payload));
  az_iot_message_properties props;
  char properties_buffer[128];
  az_iot_message_properties_init(&props, AZ_SPAN_FROM_BUFFER(properties_buffer), 0);
  az_iot_message_properties_append(&props, AZ_SPAN_FROM_STR("key"), AZ_SPAN_FROM_STR("value"));
  az_result result = az_iot_hub_client_properties_parse_received_topic(&client, az_span_create_from_str(payload), NULL);
  if (az_result_failed(result)) {
    Serial.println("Failed to send telemetry data to Azure IoT Hub");
  } else {
    Serial.println("Telemetry data sent to Azure IoT Hub");
  }

  delay(5000); // Delay for 5 seconds before sending next telemetry data
}

