/*
  To upload through terminal you can use: curl -F "image=@firmware.bin" esp8266-webupdate.local/update
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include "FS.h"
#include "protocol/protocol.h"

#define THING_VERSION  "03.01:01:00"
#define UDP_PACKET_SIZE  1024

const char* host = "lightlivingroom";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";
const char* ssid = "*****";
const char* password = "*****";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiUDP Udp;
unsigned int localUdpPort = 3000;
char incomingPacket[UDP_PACKET_SIZE];
char outgoingPacket[UDP_PACKET_SIZE];

struct ParaboxConfig {
  uint8_t   input1;
  uint8_t   input2;
  uint8_t   input3;
  uint8_t   input4;
  uint32_t  crc;
} __attribute__((packed));

struct ParaboxConfig paraboxConfig;

unsigned long timeout = 0;

void setup(void) {

  pinMode(15, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(15, LOW);

  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(16, OUTPUT);
  

  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting Sketch...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);

  while(WiFi.waitForConnectResult() != WL_CONNECTED){
    WiFi.begin(ssid, password);
    Serial.println("WiFi failed, retrying");
  }

  Serial.println("Mounting FS...");
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  bool configRet = loadConfig();
  if(!configRet) {
    Serial.println("Failed to load config, create blank");
    paraboxConfig.input1 = 0;
    paraboxConfig.input2 = 0;
    paraboxConfig.input3 = 0;
    paraboxConfig.input4 = 0;
    
    configRet = saveConfig();
    if(!configRet) {
      Serial.println("Failed to save blank config");
    }
  }

  digitalWrite(16, paraboxConfig.input1);
  digitalWrite(13, paraboxConfig.input2);
  digitalWrite(12, paraboxConfig.input3);
  digitalWrite(14, paraboxConfig.input4);
  
  MDNS.begin(host);

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);

  Udp.begin(localUdpPort);
  Serial.printf("Now listening at IP %s, UDP port %d\n", WiFi.localIP().toString().c_str(), localUdpPort);
}

uint32_t calculateCRC32(uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

bool saveConfig() {
  File configFile = SPIFFS.open("/config.bin", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  uint32_t crc = calculateCRC32((uint8_t *)&paraboxConfig, sizeof(paraboxConfig) - 4);
  paraboxConfig.crc = crc;

  configFile.write((uint8_t *)&paraboxConfig, sizeof(paraboxConfig));
  return true;
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.bin", "r");
  if (!configFile) {
    Serial.println("Failed to open config file for reading");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }
  if (size < sizeof(paraboxConfig)) {
    Serial.println("Config file size is too small");
    return false;
  }

  configFile.read((uint8_t *)&paraboxConfig, sizeof(paraboxConfig));
  Serial.printf("Loaded config inputs %d %d %d %d crc %lu\n", paraboxConfig.input1, paraboxConfig.input2, paraboxConfig.input3, paraboxConfig.input4, paraboxConfig.crc);

  uint32_t crc = calculateCRC32((uint8_t *)&paraboxConfig, sizeof(paraboxConfig) - 4);
  Serial.printf("Saved crc %lu calc %lu\n", paraboxConfig.crc, crc);
  
  if(paraboxConfig.crc != crc) {
    Serial.println("Wrong CRC");
    return false;
  }
  
  return true;
}

int processCommand(int packetSize) {
  struct ParaboxHeader messageHeader;
  Serial.printf("sizeof(messageHeader): %d packetSize: %d\n", sizeof(messageHeader), packetSize);
  if (packetSize >= sizeof(messageHeader)) {
    memcpy(&messageHeader, incomingPacket, sizeof(messageHeader));

    Serial.printf("messageHeader.action: %d messageHeader.parameter: %d messageHeader.dataType: %d messageHeader.dataLen: %d\n", messageHeader.action, messageHeader.parameter, messageHeader.dataType, messageHeader.dataLen);
    
    switch(messageHeader.parameter) {
    case PARAM_VERSION:
      if (messageHeader.action == ACTION_READ) {
        messageHeader.action = ACTION_READ_ACK;
        messageHeader.dataType = DATATYPE_STRING;
        messageHeader.dataLen = sizeof(THING_VERSION);

        memcpy(outgoingPacket, &messageHeader, sizeof(messageHeader));
        uint32_t i = 0;
        for(i = 0; i < sizeof(THING_VERSION); ++i) {
          outgoingPacket[sizeof(messageHeader) + i] = THING_VERSION[i];
        }
        return sizeof(messageHeader) + messageHeader.dataLen;
      }
      break;
    case PARAM_OUTPUT:
      if (messageHeader.action == ACTION_WRITE) {
        uint8_t value = incomingPacket[sizeof(messageHeader)];
        if (value == 0) {
          Serial.printf("Turn off: %d\n", messageHeader.index);
          if(messageHeader.index == 0) {
            digitalWrite(16, LOW);
            paraboxConfig.input1 = 0;
          } else if(messageHeader.index == 1) {
            digitalWrite(13, LOW);
            paraboxConfig.input2 = 0;
          } else if(messageHeader.index == 2) {
            digitalWrite(12, LOW);
            paraboxConfig.input3 = 0;
          } else if(messageHeader.index == 3) {
            paraboxConfig.input4 = 0;
            digitalWrite(14, LOW);
          }
        } else {
          Serial.printf("Turn on: %d\n", messageHeader.index);
          if(messageHeader.index == 0) {
            digitalWrite(16, HIGH);
            paraboxConfig.input1 = 1;
          } else if(messageHeader.index == 1) {
            digitalWrite(13, HIGH);
            paraboxConfig.input2 = 1;
          } else if(messageHeader.index == 2) {
            digitalWrite(12, HIGH);
            paraboxConfig.input3 = 1;
          } else if(messageHeader.index == 3) {
            digitalWrite(14, HIGH);
            paraboxConfig.input4 = 1;
          }
        }
        messageHeader.action = ACTION_WRITE_ACK;
        messageHeader.dataType = DATATYPE_VOID;
        messageHeader.dataLen = 0;

        bool configRet = saveConfig();
        if(!configRet) {
          Serial.println("Failed to save config");
        }

        memcpy(outgoingPacket, &messageHeader, sizeof(messageHeader));
        return sizeof(messageHeader) + messageHeader.dataLen;
      }
    }
  }
  return 0;
}

void loop(void) {
  // handle OTA update
  httpServer.handleClient();

  // handle UDP server
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    int len = Udp.read(incomingPacket, UDP_PACKET_SIZE);
    if (len > 0) {
      incomingPacket[len] = 0;
    }

    int retLen = processCommand(packetSize);
    if(retLen) {
      // send back a reply, to the IP address and port we got the packet from
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      Serial.printf("Send %d bytes to %s, port %d\n", retLen, Udp.remoteIP().toString().c_str(), Udp.remotePort());
      Udp.write(outgoingPacket, retLen);
      Udp.endPacket();
    }
  }

  // keep alive LED
  if(millis() > timeout) {
    timeout = millis() + 500;
    digitalWrite(15, !digitalRead(15));
  }
  
}
