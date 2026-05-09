// Truly minimal ESP-NOW broadcast test.
// No channel lock, no TX power, no sleep settings — bare minimum only.
// Flash identical firmware to both boards, open serial monitors on both.
// Each board sends a 4-byte counter every 3 s and prints everything it receives.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t txSeq = 0;

void OnSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    Serial.printf("[TX] cb: %s\n",
        status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

void OnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    Serial.printf("[RX] %d bytes from %02X:%02X:%02X:%02X:%02X:%02X  rx_ch=%u\n",
        len,
        info->src_addr[0], info->src_addr[1], info->src_addr[2],
        info->src_addr[3], info->src_addr[4], info->src_addr[5],
        (unsigned)info->rx_ctrl->channel);
    if (len == 4) {
        uint32_t seq;
        memcpy(&seq, data, 4);
        Serial.printf("       seq=%u\n", seq);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== minimal esp-now test ===");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("esp_now_init FAILED");
        return;
    }

    esp_now_register_send_cb(OnSent);
    esp_now_register_recv_cb(OnRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("add_peer FAILED");
        return;
    }

    Serial.println("Ready — TX every 3 s");
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last >= 3000) {
        last = millis();
        uint8_t ch = 0; wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&ch, &sec);
        Serial.printf("[TX] seq=%u  ch=%u\n", txSeq, (unsigned)ch);
        esp_now_send(BCAST, (const uint8_t*)&txSeq, sizeof(txSeq));
        txSeq++;
    }
}
