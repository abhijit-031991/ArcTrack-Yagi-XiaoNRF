#define INIT_BEGIN (byte)74
#define INIT_END (byte)105
#define INIT_END_WIPE (byte)106

////// Data Download Codes(50-55)  //////
#define DATA_DOWNLOAD_NEW (byte)50      // Sent from App to device to request new data since last download
#define DATA_DOWNLOAD_ALL (byte)51      // Sent from App to device to request all stored data
#define DATA_DOWNLOAD_END (byte)52      // Sent from device to App to indicate end of data transmission
#define DATA_DOWNLOAD_ERROR (byte)53    // Sent from device to App if data transmission failed
#define DATA_DOWNLOAD_BEGIN (byte)54    // Sent from device to App to indicate start of data transmission

#define WIPE_MEMORY (byte)128

#define SETTINGS_UPDATED (byte)83

#define SIMPLE_PING (byte)99
#define SIMPLE_PING_ACK (byte)101