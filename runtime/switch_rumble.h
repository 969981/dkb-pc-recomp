// Switch HD rumble packet encoding (see switch_rumble.c for the SDL origin of the tables).
#pragma once
#include <stdint.h>

// One side's VibrationValue {amp_low, freq_low, amp_high, freq_high} (amplitude 0..1, frequency in Hz) as the four
// rumble bytes of a Switch controller. Both amplitudes zero gives SDL's neutral value 00 01 40 40.
void switch_rumble_encode(const float v[4], uint8_t out[4]);
// The 10-byte SwitchCommonOutputPacket_t SDL_SendGamepadEffect takes: type 0x10, a counter byte SDL overwrites, then
// the left and right sides' four bytes each.
void switch_rumble_packet(const float v[2][4], uint8_t out[10]);
