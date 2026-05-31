#pragma once

#include <stdint.h>

static const uint8_t PLAYER2_PACKET_MAGIC_0 = 'P';
static const uint8_t PLAYER2_PACKET_MAGIC_1 = '2';
static const uint8_t PLAYER2_PACKET_VERSION = 1;
static const uint8_t PLAYER2_PACKET_VERSION_V2 = 2;

enum Player2PacketType : uint8_t {
  PLAYER2_PACKET_TYPE_BUTTON_EVENT = 0x01,
  PLAYER2_PACKET_TYPE_LED_STATE = 0x02,
  PLAYER2_PACKET_TYPE_BEEP_CMD = 0x03,
};

enum Player2BeepCommand : uint8_t {
  PLAYER2_BEEP_NONE = 0,
  PLAYER2_BEEP_READY = 1,
  PLAYER2_BEEP_HIT = 2,
  PLAYER2_BEEP_MISS = 3,
  PLAYER2_BEEP_WIN = 4,
  PLAYER2_BEEP_LOSE = 5,
};

enum Player2LedStateFlags : uint8_t {
  PLAYER2_LED_FLAG_NONE = 0x00,
  PLAYER2_LED_FLAG_PRESS_TURNS_OFF = 0x01,
  PLAYER2_LED_FLAG_PRESS_TURNS_ON = 0x02,
  PLAYER2_LED_FLAG_LOCAL_TOGGLE = 0x04,
};

struct __attribute__((packed)) Player2EspNowPacket {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t version;
  uint8_t sequence;
  uint8_t buttonCode;
};

struct __attribute__((packed)) Player2EspNowPacketV2 {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t version;
  uint8_t packetType;
  uint8_t sequence;
  // For PLAYER2_PACKET_TYPE_LED_STATE: arg0 is RGBW base mask, bit=1 means LED visibly ON.
  uint8_t arg0;
  // For PLAYER2_PACKET_TYPE_LED_STATE: arg1 marks RGBW buttons where local press overlay is enabled.
  uint8_t arg1;
  // For PLAYER2_PACKET_TYPE_LED_STATE: flags use Player2LedStateFlags.
  uint8_t flags;
};

inline bool isPlayer2EspNowPacket(const uint8_t* data, int len) {
  if (data == nullptr || len != (int)sizeof(Player2EspNowPacket)) {
    return false;
  }

  return data[0] == PLAYER2_PACKET_MAGIC_0 &&
         data[1] == PLAYER2_PACKET_MAGIC_1 &&
         data[2] == PLAYER2_PACKET_VERSION;
}

inline bool isPlayer2EspNowPacketV2(const uint8_t* data, int len) {
  if (data == nullptr || len != (int)sizeof(Player2EspNowPacketV2)) {
    return false;
  }

  return data[0] == PLAYER2_PACKET_MAGIC_0 &&
         data[1] == PLAYER2_PACKET_MAGIC_1 &&
         data[2] == PLAYER2_PACKET_VERSION_V2;
}
