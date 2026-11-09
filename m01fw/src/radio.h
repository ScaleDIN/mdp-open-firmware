#pragma once
#include <stdint.h>
#include <stdbool.h>

#define RADIO_PAYLOAD 32

/* Si24R1 (nRF24L01+ clone) with the stock M01 settings:
 * 2 Mbps, max power, CRC16, auto-ack on pipe 0, 10 retries x 500 us,
 * fixed 32-byte payloads, 5-byte address. */
bool radio_init(uint8_t channel);                 /* true if SPI read-back works */
void radio_set_channel(uint8_t channel);
void radio_set_address(const uint8_t addr[5]);    /* register order (LSB first) */
bool radio_tx(const uint8_t p[RADIO_PAYLOAD]);    /* true = hardware ACK received */
bool radio_rx(uint8_t p[RADIO_PAYLOAD], uint32_t timeout_ms);
void radio_idle(void);
uint8_t radio_read_reg(uint8_t reg);
