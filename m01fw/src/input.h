#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_COUNT };
enum { ENC_MENU, ENC_VALUE, ENC_COUNT };

void input_init(void);
void input_sample(void);          /* called at 4 kHz from SysTick */
uint8_t keys_state(void);         /* bit i = key i held (debounced) */
uint8_t keys_pressed(void);       /* new presses since last call */
int32_t enc_count(int enc);       /* absolute detent count */
uint8_t enc_raw(int enc);         /* bit1 = A, bit0 = B (1 = high) */
