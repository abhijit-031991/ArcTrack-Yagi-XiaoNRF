#define ARDUINOJSON_USE_LONG_LONG 1
#define ARCH_STM32

const char* tagID = "ArcTrack-Yagi";

#define BLE_TX PA2
#define BLE_RX PA3
#define LED_PIN PA10
// #define WB05_NRST_PIN PB4   // WB05 NRST on PB4 (active low). DISABLED: when this
//                             // is defined, the #ifdef block in test.cpp setup()
//                             // drives NRST and (with only a weak pull-up release)
//                             // leaves the WB05 held in reset -> no HCI response.
//                             // Re-enable only with a firm active-high release and
//                             // a verified PB4->NRST connection.

#define PING_SIZE 14
#define DATA_SIZE 16
#define SETTING_SIZE 27
#define REQ_SIZE 3

struct data{
    uint32_t datetime;
    uint16_t locktime;
    float lat;
    float lng;
    float hdop;
    float x;
    float y;
    float z;
    unsigned int count;
    uint16_t id;
}__attribute__((__packed__));

struct settings{
    uint16_t tag;
    int gpsFrq;
    int gpsTout;
    int hdop;
    int radioFrq;
    int startHour;
    int endHour;
    bool scheduled;
}__attribute__((__packed__));

struct reqPing{
    uint16_t tag;
    byte request;
  }__attribute__((__packed__));;

struct resPing{
    uint16_t tag;
    byte resp;
  }__attribute__((__packed__));;

  struct longPing{
    uint16_t ta;    
    uint16_t cnt;
    float la;
    float ln;
    uint8_t devtyp;
    bool mortality;
  }__attribute__((__packed__));
    
  struct calibrationData{
      float lat;
      float lng;
      float hdop;
      float bat;
      float signal;
      uint16_t tag;
      bool mqtt;
      bool gprs;
      bool network;
  }__attribute__((__packed__)); // Calibration Struct - Store GPS data during calibration.