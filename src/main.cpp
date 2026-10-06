#include <Arduino.h>
#include <bluefruit.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <SPI.h>

// Include your existing files from the src directory
#include <definitions.h>
#include <codes.h>

// --- Verified Official Seeed Wio-SX1262 Pin Mapping ---
#define LORA_NSS   D4    // Chip Select
#define LORA_DIO1  D1    // IRQ Interrupt
#define LORA_NRST  D2    // Reset
#define LORA_BUSY  D3    // Busy
#define LORA_RXEN  D5    // RF Switch RX Enable

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// Explicit 128-bit standard expansion for 0x189A:
BLEService        dataService("00000000-0000-0000-0000-00000000189a");

// Characteristics matching your STM32 definitions exactly:
BLECharacteristic pingCharacteristic("c4850de5-2ca0-464b-8e4a-ae45ad4460b7");
BLECharacteristic dataCharacteristic("9e150970-35ad-400c-b46d-08ed71f07709");
BLECharacteristic metaData("a0fa056d-716d-42cd-bfd6-f48c72e2cbe6");

volatile bool packetReceived = false;

// Non-blocking timer for 1-minute battery updates
unsigned long lastBatteryCheckTime = 0;
const unsigned long BATTERY_INTERVAL_MS = 60000; // 60 seconds

// Interrupt handler when SX1262 receives a packet
void setRxFlag(void) {
  packetReceived = true;
}

// Write Callback: Triggered when mobile app writes to pingCharacteristic
void writeCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data_ptr, uint16_t len) {
  char setIn[128] = {0};
  uint16_t copyLen = len < sizeof(setIn) - 1 ? len : sizeof(setIn) - 1;
  memcpy(setIn, data_ptr, copyLen);

  Serial.print("BLE Write Received: ");
  Serial.println(setIn);

  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, setIn);

  if (!error) {
    // Handle Ping Request
    if (doc.containsKey("ID") && doc.containsKey("Msg")) {
      reqPing r;
      r.tag = doc["ID"];
      r.request = (byte)doc["Msg"];

      Serial.print(F("Transmitting LoRa Ping for Tag ID: "));
      Serial.println(r.tag);

      radio.standby();
      radio.transmit((uint8_t*)&r, sizeof(r));
      radio.startReceive();
    }

    // Handle Settings Update
    if (doc.containsKey("ID") && doc.containsKey("gfrq")) {
      settings s;
      s.tag = doc["ID"];
      s.gpsFrq = doc["gfrq"];
      s.gpsTout = doc["gtout"];
      s.hdop = doc["hdop"];
      s.radioFrq = doc["rfrq"];
      s.startHour = doc["starth"];
      s.endHour = doc["endh"];
      s.scheduled = doc["sched"];

      Serial.println(F("Transmitting LoRa Settings Payload"));

      radio.standby();
      radio.transmit((uint8_t*)&s, sizeof(s));
      radio.startReceive();
    }
  }
}

// Function to read and return battery voltage in Volts (e.g., 4.12 V)
float readBatteryVoltage() {
  // 1. Enable the battery divider circuit (Active LOW on XIAO nRF52840)
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);

  // 2. Configure ADC parameters for accurate reading
  analogReference(AR_INTERNAL); // 3.6V internal reference (0.6V reference * 6 gain)
  analogReadResolution(12);     // 12-bit resolution (0 - 4095)

  // Allow voltage to stabilize across the divider
  delayMicroseconds(50);

  // 3. Take an averaged reading
  uint32_t adcRaw = 0;
  const int numSamples = 8;
  for (int i = 0; i < numSamples; i++) {
    adcRaw += analogRead(PIN_VBAT);
    delay(2);
  }
  adcRaw /= numSamples;

  // 4. Disable the divider to prevent battery drain
  digitalWrite(VBAT_ENABLE, HIGH);
  pinMode(VBAT_ENABLE, INPUT);

  // 5. Convert ADC value to actual battery voltage:
  // Formula: (ADC / 4095) * 3.6V * (Divider Ratio: (1M + 510k) / 510k ≈ 2.96078)
  float measuredVoltage = ((float)adcRaw / 4095.0f) * 3.6f * 2.96078f;

  return measuredVoltage;
}

void setupBLE() {
  // 1. MUST set max MTU before Bluefruit.begin() (Max is 247 bytes for BLE 4.2/5.0)
  Bluefruit.configPrphConn(247, BLE_GAP_EVENT_LENGTH_DEFAULT, 4, 4); // MTU, Event Length, HVN Queue Size, Write Command Queue Size
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(tagID);

  dataService.begin();

  // Configure characteristics
  pingCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  pingCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  pingCharacteristic.setMaxLen(244); // 247 MTU - 3 byte ATT overhead = 244 byte max payload
  pingCharacteristic.setWriteCallback(writeCallback);
  pingCharacteristic.begin();

  dataCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  dataCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  dataCharacteristic.setMaxLen(244);
  dataCharacteristic.begin();

  metaData.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  metaData.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  metaData.setMaxLen(244);
  metaData.begin();

  // Advertising setup
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(dataService);
  Bluefruit.ScanResponse.addName();
  Bluefruit.ScanResponse.addTxPower();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);

  Serial.println("BLE Configured with 247 MTU Capacity");
}

void sendBatteryUpdate() {
  float vbat = readBatteryVoltage();

  StaticJsonDocument<64> doc;
  doc[F("BAT")] = serialized(String(vbat, 2)); // Formats as 4.12 or float value

  char dat[64];
  size_t jsonLen = serializeJson(doc, dat);

  // Only notify if BLE client is connected and notifications are enabled
  if (Bluefruit.connected()) {
    metaData.notify((uint8_t*)dat, jsonLen);
    Serial.print(F("Battery update sent via BLE: "));
  } else {
    Serial.print(F("Battery reading (No BLE connection): "));
  }
  Serial.println(dat);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Configure RXEN pin for the RF switch
  pinMode(LORA_RXEN, OUTPUT);
  digitalWrite(LORA_RXEN, HIGH);

  Serial.begin(115200);
  uint32_t startTime = millis();
  while (!Serial && (millis() - startTime < 3000));

  Serial.print(F("Initializing SX1262... "));

  // Parameters: Freq (867.0), BW (125.0), SF (12), CR (7), SyncWord (0x12), Power (10), Preamble (8), TCXO Voltage (1.8V), useLDO (false)
  int state = radio.begin(867.0, 125.0, 12, 7, 0x12, 10, 8, 1.8, false);

  if (state == RADIOLIB_ERR_NONE) {
    // Enable internal DIO2 RF switch control
    radio.setDio2AsRfSwitch(true);
    
    // Set up DIO1 interrupt
    radio.setDio1Action(setRxFlag);
    radio.startReceive();
    
    Serial.println(F("SX1262 Initialized Successfully (Code 0)!"));
  } else {
    Serial.print(F("SX1262 Init Failed, code: "));
    Serial.println(state);
  }

  setupBLE();
}

void processLoRaPacket() {
  size_t len = radio.getPacketLength();
  float rssi = radio.getRSSI();
  Serial.println(len); 
  // 1. Handle Response Packet (resPing)
  if (len == sizeof(resPing)) {
    resPing r;
    radio.readData((uint8_t*)&r, len);
    Serial.write((uint8_t*)&r, len);
    StaticJsonDocument<128> doc;
    doc[F("ID")] = r.tag;  
    doc[F("Msg")] = r.resp;
    doc[F("RSSI")] = rssi;
    char dat[128];
    serializeJson(doc, dat);
    if (r.resp == DATA_DOWNLOAD_BEGIN || r.resp == DATA_DOWNLOAD_END || r.resp == DATA_DOWNLOAD_ERROR)
    {
      dataCharacteristic.notify(dat);
    }else{
      pingCharacteristic.notify(dat);
    }
    
    Serial.println(dat);
  }
  // 2. Handle Ping Packet (longPing)
  else if (len == sizeof(longPing)) {
    longPing p;
    radio.readData((uint8_t*)&p, len);
    Serial.write((uint8_t*)&p, len);
    StaticJsonDocument<256> doc;
    doc[F("ID")] = p.ta;
    doc[F("Lat")] = String(p.la, 6);
    doc[F("Lng")] = String(p.ln, 6);
    doc[F("DTyp")] = p.devtyp;
    doc[F("cnt")] = p.cnt;
    doc[F("RSSI")] = rssi;
    doc[F("Mort")] = p.mortality;

    char dat[128];
    serializeJson(doc, dat);
    pingCharacteristic.notify(dat);
    Serial.println(dat);
  }

  // 5. Handle Ping Packet with Ping ID (lngPngid)
  else if (len == sizeof(lngPngid)) {
    lngPngid p;
    radio.readData((uint8_t*)&p, len);
    Serial.write((uint8_t*)&p, len);
    StaticJsonDocument<256> doc;
    doc[F("ID")] = p.ta;
    doc[F("Lat")] = String(p.la, 6);
    doc[F("Lng")] = String(p.ln, 6);
    doc[F("DTyp")] = p.devtyp;
    doc[F("cnt")] = p.cnt;
    doc[F("RSSI")] = rssi;
    doc[F("Mort")] = p.mortality;

    char dat[128];
    serializeJson(doc, dat);
    pingCharacteristic.notify(dat);
    Serial.println(dat);
  }

  // 3. Handle Data Packet (data)
  else if (len == sizeof(data)) {
    data d;
    radio.readData((uint8_t*)&d, len);

    StaticJsonDocument<512> doc;
    doc[F("Date")] = d.datetime;
    doc[F("Lat")] = d.lat;
    doc[F("Lng")] = d.lng;
    doc[F("LckTm")] = d.locktime;
    doc[F("hdop")] = d.hdop;
    doc[F("ID")] = d.id;
    doc[F("x")] = d.x;
    doc[F("y")] = d.y;
    doc[F("z")] = d.z;
    doc[F("cnt")] = d.count;

    char dat[512];
    serializeJson(doc, dat);
    dataCharacteristic.notify(dat);
    Serial.println(dat);
  }
  // 4. Handle Calibration Metadata (calibrationData)
  else if (len == sizeof(calibrationData) || len == 25 || len == 28) {
    calibrationData calData;
    radio.readData((uint8_t*)&calData, len);

    StaticJsonDocument<256> doc;
    doc[F("Lat")] = String(calData.lat, 6);
    doc[F("Lng")] = String(calData.lng, 6);
    doc[F("Hdop")] = String(calData.hdop, 1);
    doc[F("Bat")] = String(calData.bat, 2);
    doc[F("Sig")] = String(calData.signal, 2);
    doc[F("Mqtt")] = calData.mqtt;
    doc[F("Gprs")] = calData.gprs;
    doc[F("Net")] = calData.network;
    doc[F("ID")] = calData.tag;

    char dat[256];
    serializeJson(doc, dat);
    pingCharacteristic.notify(dat);
    Serial.println(dat);
  }else {
  Serial.print(F("Unknown packet length: "));
  Serial.println(len);

  // Allocate buffer for 16 bytes
  uint8_t buffer[16] = {0};

  // Read exactly 16 bytes (or min(len, 16) to avoid reading past packet boundary)
  size_t bytesToRead = (len >= 16) ? 16 : len;
  int state = radio.readData(buffer, bytesToRead);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.print(F("Raw 16-byte payload (HEX): "));
    for (size_t i = 0; i < bytesToRead; i++) {
      if (buffer[i] < 0x10) Serial.print('0'); // Leading zero padding
      Serial.print(buffer[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  } else {
    Serial.print(F("Failed to read payload, code: "));
    Serial.println(state);
  }
}

  // Resume listening mode
  radio.startReceive();
}

void loop() {
  if (packetReceived) {
    packetReceived = false;
    Serial.println("PKT");
    Serial.flush();
    processLoRaPacket();
  }
  // 2. Non-blocking 1-minute battery voltage update
  unsigned long currentMillis = millis();
  if (currentMillis - lastBatteryCheckTime >= BATTERY_INTERVAL_MS) {
    lastBatteryCheckTime = currentMillis;
    sendBatteryUpdate();
  }
  // Sleep nRF52 CPU until an event occurs
  waitForEvent();
}