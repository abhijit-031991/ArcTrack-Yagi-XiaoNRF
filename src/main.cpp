#include <Arduino.h>
#include <bluefruit.h>
#include <RadioLib.h>
#include <ArduinoJson.h>

// Include your existing files from the src directory
#include <definitions.h>
#include <codes.h>

// --- Hardware Pin Mappings for XIAO nRF52840 + Wio-SX1262 ---
#define LORA_NSS   D1
#define LORA_DIO1  D2
#define LORA_NRST  D0
#define LORA_BUSY  D3

// --- RadioLib SX1262 Instance ---
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// Explicit 128-bit standard expansion for 0x189A:
BLEService        dataService("00000000-0000-0000-0000-00000000189a");

// Characteristics matching your STM32 definitions exactly:
BLECharacteristic pingCharacteristic("c4850de5-2ca0-464b-8e4a-ae45ad4460b7");
BLECharacteristic dataCharacteristic("9e150970-35ad-400c-b46d-08ed71f07709");
BLECharacteristic metaData("a0fa056d-716d-42cd-bfd6-f48c72e2cbe6");

volatile bool packetReceived = false;

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

void setupBLE() {
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(tagID); // tagID ("ArcTrack-Yagi") from definitions.h

  // 1. MUST INITIALIZE SERVICE FIRST
  dataService.begin();

  // 2. CONFIGURE AND BEGIN CHARACTERISTICS IMMEDIATELY AFTER
  // Ping Characteristic
  pingCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  pingCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  pingCharacteristic.setMaxLen(128);
  pingCharacteristic.setWriteCallback(writeCallback);
  pingCharacteristic.begin(); // <--- Crucial!

  // Data Characteristic
  dataCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  dataCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  dataCharacteristic.setMaxLen(512);
  dataCharacteristic.begin(); // <--- Crucial!

  // Metadata Characteristic
  metaData.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  metaData.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  metaData.setMaxLen(256);
  metaData.begin(); // <--- Crucial!

  // 3. CONFIGURE ADVERTISING AFTER ALL SERVICES & CHARACTERISTICS ARE BEGUN
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(dataService);

  Bluefruit.ScanResponse.addName();
  Bluefruit.ScanResponse.addTxPower();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);

  Serial.println("BLE Services & Characteristics Initialized Successfully!");
}

void setup() {
  // LED_PIN is redefined on nRF52; using hardware LED_BUILTIN to avoid GPIO mismatch
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);

  // Initialize Radio (867.0 MHz matching your LoRa network)
  Serial.print(F("Initializing SX1262... "));
  int state = radio.begin(867.0, 125.0, 12, 7, 0x12, 10, 8);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("SX1262 Initialized Successfully!"));
  } else {
    Serial.print(F("SX1262 Init Failed, code: "));
    Serial.println(state);
  }

  // Set up DIO1 interrupt for incoming LoRa packets
  radio.setDio1Action(setRxFlag);
  radio.startReceive();

  // Start BLE
  setupBLE();
}

void processLoRaPacket() {
  size_t len = radio.getPacketLength();
  float rssi = radio.getRSSI();

  // 1. Handle Response Packet (resPing)
  if (len == sizeof(resPing)) {
    resPing r;
    radio.readData((uint8_t*)&r, len);

    StaticJsonDocument<128> doc;
    doc[F("ID")] = r.tag;
    doc[F("Msg")] = r.resp;
    doc[F("RSSI")] = rssi;

    char dat[128];
    serializeJson(doc, dat);
    pingCharacteristic.notify(dat);
    Serial.println(dat);
  }
  // 2. Handle Ping Packet (longPing)
  else if (len == sizeof(longPing)) {
    longPing p;
    radio.readData((uint8_t*)&p, len);

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
  }

  // Resume listening mode
  radio.startReceive();
}

void loop() {
  if (packetReceived) {
    packetReceived = false;
    processLoRaPacket();
  }

  // Sleep nRF52 CPU until an event occurs
  waitForEvent();
}