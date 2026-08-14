#include <Arduino.h>
#include <SPI.h>
#include <STM32duinoBLE.h>
#include <permaDefs.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// Remove manual HCI initialization - let the library handle it
// Use the default BLE object provided by the library

BLEService dataService("0X189A"); // Fixed: removed "0X" prefix

// BLE Characteristics
BLECharacteristic pingCharacteristic("c4850de5-2ca0-464b-8e4a-ae45ad4460b7", BLERead | BLEWrite | BLENotify, 128, false);
BLECharacteristic dataCharacteristic("9e150970-35ad-400c-b46d-08ed71f07709", BLERead | BLEWrite | BLENotify, 128, false);
BLECharacteristic metaData("a0fa056d-716d-42cd-bfd6-f48c72e2cbe6", BLERead | BLEWrite | BLENotify, 128, false);

void blinkLed(int bw, int pause) {
  digitalToggle(PA10);
  delay(bw);
  digitalToggle(PA10);
  delay(pause);
}

void setup() {
  delay(5000);
  SerialUSB.begin(115200);
  SerialUSB.println("ArcTrack-Mobile-transmitter");
  
  pinMode(PA10, OUTPUT);
  digitalWrite(PA10, HIGH);
  
  // Initialize SPI and LoRa
  SPI.begin();
  LoRa.setPins(SS, RST, DI0);
  if (!LoRa.begin(867E6)) {
    SerialUSB.println("Starting LoRa failed!");
  } else {
    LoRa.setSpreadingFactor(12);
    SerialUSB.println("Started LoRa!");
  }
  
  // Initialize BLE
  SerialUSB.println("Initializing BLE...");
  if (!BLE.begin()) {
    SerialUSB.println("Starting BLE failed!");
    // Blink rapidly to indicate error
    while (1) {
      blinkLed(100, 100);
    }
  } else {
    SerialUSB.println("BLE started!");
    
    // Get and print BLE address for verification
    SerialUSB.print("BLE Address: ");
    SerialUSB.println(BLE.address());
    
    // Configure BLE advertising
    BLE.setAdvertisingInterval(50);
    BLE.setLocalName(tagID);
    BLE.setAdvertisedService(dataService);
    
    // Add characteristics to service
    dataService.addCharacteristic(pingCharacteristic);
    dataService.addCharacteristic(dataCharacteristic);
    dataService.addCharacteristic(metaData);
    
    // Add service to BLE
    BLE.addService(dataService);
    
    // Start advertising
    if (!BLE.advertise()) {
      SerialUSB.println("Failed to start advertising!");
    } else {
      SerialUSB.println("BLE advertising started!");
      SerialUSB.println("Device should be visible in BLE scans");
    }
    
    // Debug: print sizes
    calibrationData calData;
    settings s;
    SerialUSB.print("Calibration Data Size: ");
    SerialUSB.println(sizeof(calData));
    SerialUSB.print("Settings Data Size: ");
    SerialUSB.println(sizeof(s));
  }
}

void loop() {
  // CRITICAL: Always call BLE.poll() even when not connected
  BLE.poll();
  
  // Blink LED to show device is running
  blinkLed(200, 300);
  
  // Check for BLE central connection
  BLEDevice central = BLE.central();
  
  if (central) {
    digitalWrite(PA10, HIGH);
    SerialUSB.print("Connected to central: ");
    SerialUSB.println(central.address());
    
    // While connected to central
    while (central.connected()) {
      // CRITICAL: Must call BLE.poll() to process BLE events
      BLE.poll();
      
      // Check for LoRa packets
      int x = LoRa.parsePacket();
      
      if (x != 0) {
        SerialUSB.print("LoRa packet size: ");
        SerialUSB.println(x);
      }
      
      // Handle Request/Response (3 bytes)
      if (x == 3) {
        struct resp {
          uint16_t tag;
          byte res;
        } r;
        
        while (LoRa.available()) {
          LoRa.readBytes((uint8_t*)&r, x);
        }
        
        StaticJsonDocument<128> doc;
        doc[F("ID")] = r.tag;
        doc[F("Msg")] = r.res;
        doc[F("RSSI")] = LoRa.packetRssi();
        
        char dat[128];
        serializeJson(doc, dat);
        pingCharacteristic.writeValue(dat);
        SerialUSB.println(dat);
      }
      
      // Handle Ping (14 bytes)
      if (x == 16) {
        SerialUSB.println(F("Received Ping"));
        
        struct ping {
          uint16_t ta;
          uint16_t cnt;
          uint16_t pid;
          float la;
          float ln;
          uint8_t devtyp;
          bool mortality;
        } __attribute__((__packed__)) p;
        
        while (LoRa.available()) {
          LoRa.readBytes((uint8_t*)&p, x);
        }
        
        StaticJsonDocument<256> doc;
        doc[F("ID")] = p.ta;
        doc[F("Lat")] = String(p.la, 6);
        doc[F("Lng")] = String(p.ln, 6);
        doc[F("DTyp")] = p.devtyp;
        doc[F("cnt")] = p.cnt;
        doc[F("RSSI")] = LoRa.packetRssi();
        doc[F("Mort")] = p.mortality;
        doc[F("Pid")] = p.pid;
        
        char dat[128];
        serializeJson(doc, dat);
        pingCharacteristic.writeValue(dat);
        SerialUSB.println(dat);
      }
      
      // Handle Data (36 bytes)
      if (x == 36) {
        struct data d;
        
        while (LoRa.available()) {
          LoRa.readBytes((uint8_t*)&d, sizeof(d));
        }
        
        StaticJsonDocument<512> doc;
        doc[F("Date")] = d.datetime;
        doc[F("Lat")] = d.lat;
        doc[F("Lng")] = d.lng;
        doc[F("LckTm")] = d.locktime;
        doc[F("hdop")] = d.hdop;
        doc[F("ID")] = d.id;
        doc[F("x")] = d.x;
        doc[F("y")] = d.y;
        doc[F("z")] = d.z; // Fixed: was "X" should be lowercase
        doc[F("cnt")] = d.count;
        
        char dat[512];
        serializeJson(doc, dat);
        dataCharacteristic.writeValue(dat);
        SerialUSB.println(dat);
      }
      
      // Handle MetaData (23 or 26 bytes)
      if (x == 25 || x == 28) {
        calibrationData calData;
        
        while (LoRa.available()) {
          LoRa.readBytes((uint8_t*)&calData, sizeof(calData));
        }
        
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
        pingCharacteristic.writeValue(dat);
        SerialUSB.println(dat);
      }
      
      // Handle characteristic writes
      if (pingCharacteristic.written()) {
        SerialUSB.println("Ping Characteristic written");
        
        char setIn[128];
        pingCharacteristic.readValue(setIn, pingCharacteristic.valueLength());
        SerialUSB.println((char*)setIn);
        
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, setIn);
        
        if (error) {
          SerialUSB.print("JSON parsing failed: ");
          SerialUSB.println(error.c_str());
        } else {
          // Handle ping request
          if (doc.containsKey("ID") && doc.containsKey("Msg")) {
            uint16_t id = doc["ID"];
            int msg = doc["Msg"];
            
            SerialUSB.print(F("Received Ping ID: "));
            SerialUSB.print(id);
            SerialUSB.print(F(", Message: "));
            SerialUSB.println(msg);
            
            reqPing r;
            r.tag = id;
            r.request = (byte)msg;
            
            LoRa.idle();
            LoRa.beginPacket();
            LoRa.write((uint8_t*)&r, sizeof(r));
            LoRa.endPacket();
            LoRa.sleep();
          }
          
          // Handle settings update
          if (doc.containsKey("ID") && doc.containsKey("gfrq")) {
            settings s;
            s.gpsFrq = doc["gfrq"];
            s.gpsTout = doc["gtout"];
            s.hdop = doc["hdop"];
            s.radioFrq = doc["rfrq"];
            s.startHour = doc["starth"];
            s.endHour = doc["endh"];
            s.scheduled = doc["sched"];
            s.tag = doc["ID"];
            
            SerialUSB.println(F("Received Settings, sending via LoRa"));
            
            LoRa.idle();
            LoRa.beginPacket();
            LoRa.write((uint8_t*)&s, sizeof(s));
            LoRa.endPacket();
            LoRa.sleep();
          }
        }
      }
      
      // Small delay to prevent tight loop
      delay(10);
    }
    
    // Disconnected
    SerialUSB.print(F("Disconnected from central: "));
    SerialUSB.println(central.address());
    digitalWrite(PA10, LOW);
  }
  
  // Small delay in main loop
  delay(10);
}