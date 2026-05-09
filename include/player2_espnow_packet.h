#pragma once

#include <stdint.h>

static const uint8_t PLAYER2_PACKET_MAGIC_0 = 'P';
static const uint8_t PLAYER2_PACKET_MAGIC_1 = '2';
static const uint8_t PLAYER2_PACKET_VERSION = 1;

struct __attribute__((packed)) Player2EspNowPacket {
  uint8_t magic0;
  uint8_t magic1;
  uint8_t version;
  uint8_t sequence;
  uint8_t buttonCode;
};

inline bool isPlayer2EspNowPacket(const uint8_t* data, int len) {
  if (data == nullptr || len != (int)sizeof(Player2EspNowPacket)) {
    return false;
  }

  return data[0] == PLAYER2_PACKET_MAGIC_0 &&
         data[1] == PLAYER2_PACKET_MAGIC_1 &&
         data[2] == PLAYER2_PACKET_VERSION;
}
