#include <WiFi.h>
#include <az_iot_hub_client.h>
#include <az_iot_hub_client_properties.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <time.h>
#include "jbdbms.h"

// Wi-Fi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Azure IoT Hub configuration
static const char* hub_hostname = "YOUR_IOT_HUB_HOSTNAME";
static const char* device_id = "YOUR_DEVICE_ID";
static const char* device_key = "YOUR_DEVICE_KEY";

// JBD BMS UART configuration
HardwareSerial bmsSerial(2); // UART2 on ESP32
const int bmsRxPin = 16; // Replace with your RX pin number
const int bmsTxPin = 17; // Replace with your TX pin number
const int bmsSerialBaudRate = 9600; // Replace with the correct baud rate for your BMS

// Placeholder for AZ_IOT_HUB_CLIENT_MODEL_ID
#define AZ_IOT_HUB_CLIENT_MODEL_ID "YOUR_MODEL_ID"

// JSON document for sending telemetry data
StaticJsonDocument<512> jsonDoc;

// Azure IoT Hub client
az_iot_hub_client client;

// JBD BMS object
uint32_t lastAccessTime;
JbdBms bmsBattery(bmsSerial, &lastAccessTime);

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
  options.model_id = AZ_IOT_HUB_CLIENT_MODEL_ID;
  az_result_t result = az_iot_hub_client_init(&client, hub_hostname, device_id, device_key, &options);
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
  az_iot_message_properties_init(&props, NULL);
  az_iot_message_properties_append(&props, AZ_IOT_HUB_CLIENT_PROPERTY_CONTENT_TYPE, "application/json");
  az_iot_message_properties_append(&props, AZ_IOT_HUB_CLIENT_PROPERTY_KEEP_ALIVE_INTERVAL_IN_SECS, "120");
  az_result_t result = az_iot_hub_client_telemetry_send_message(&client, payload, strlen(payload), &props);
  if (az_result_failed(result)) {
    Serial.println("Failed to send telemetry data to Azure IoT Hub");
  } else {
    Serial.println("Telemetry data sent to Azure IoT Hub");
  }

  delay(5000); // Delay for 5 seconds before sending next telemetry data
}

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
  uint16_t cellVoltages[32]; // Adjust the size as per your requirements
  int16_t ntcTemperatures[8]; // Adjust the size as per your requirements
};
BmsData bmsData;


// In this updated code:

// 1. The `jbdbms.h` library is included using `#include "jbdbms.h"`.
// 2. An instance of the `JbdBms` class is created, `bmsBattery`, passing the `bmsSerial` object and a pointer to the `lastAccessTime` variable.
// 3. In the `setup()` function, the `bmsBattery.begin()` method is called to initialize the communication with the BMS.
// 4. The `readBmsData()` function has been replaced with the corresponding methods from the `JbdBms` library:
   - `bmsBattery.getStatus()` is used to read the battery pack status information.
   - `bmsBattery.getCells()` is used to read the individual cell voltages.
// 5. The received data from the BMS is processed and stored in the `BmsData` struct.
// 6. The JSON document creation and sending telemetry data to Azure IoT Hub remain the same, but now use the data from the `BmsData` struct.
// 7. The helper function `JbdBms::deciCelsius()` is used to convert the NTC temperature data to Celsius.

// Note:
- The size of the `cellVoltages` array in the `BmsData` struct is set to 32, which is the maximum number of battery strings supported by the library. Adjust this value based on your actual number of battery strings.
- The size of the `ntcTemperatures` array in the `BmsData` struct is set to 8, which is the maximum number of NTC sensors supported by the library. Adjust this value based on your actual number of NTC sensors.

// This updated code should provide a more efficient and streamlined way to communicate with the Jiabaida BMS and integrate with the Azure IoT Hub.