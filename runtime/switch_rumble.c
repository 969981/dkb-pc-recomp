/*
 * Switch HD rumble packet encoding for the Nintendo Switch Pro Controller and Joy-Con.
 *
 * The amplitude tables and the neutral value come from SDL 3.4 (src/joystick/hidapi/SDL_hidapi_switch.c,
 * EncodeRumbleHighAmplitude, EncodeRumbleLowAmplitude, SetNeutralRumble and EncodeRumble), which is
 * Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org> and contributed by Valve Corporation, under the zlib licence:
 *
 *   This software is provided 'as-is', without any express or implied warranty.  In no event will the authors be held
 *   liable for any damages arising from the use of this software.
 *
 *   Permission is granted to anyone to use this software for any purpose, including commercial applications, and to
 *   alter it and redistribute it freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
 *      If you use this software in a product, an acknowledgment in the product documentation would be appreciated but
 *      is not required.
 *   2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original
 *      software.
 *   3. This notice may not be removed or altered from any source distribution.
 *
 * Altered: the tables are lookups on their own, and the frequency bytes are computed from Hz (SDL uses fixed values).
 * Frequency codes follow the dekuNukem Nintendo_Switch_Reverse_Engineering rumble_data_table.md.
 */
#include "switch_rumble.h"

#include <math.h>
#include <string.h>

static const uint16_t hfa[101][2] = {
    { 0, 0x0 }, { 514, 0x2 }, { 775, 0x4 }, { 921, 0x6 }, { 1096, 0x8 }, { 1303, 0x0a },
    { 1550, 0x0c }, { 1843, 0x0e }, { 2192, 0x10 }, { 2606, 0x12 }, { 3100, 0x14 }, { 3686, 0x16 },
    { 4383, 0x18 }, { 5213, 0x1a }, { 6199, 0x1c }, { 7372, 0x1e }, { 7698, 0x20 }, { 8039, 0x22 },
    { 8395, 0x24 }, { 8767, 0x26 }, { 9155, 0x28 }, { 9560, 0x2a }, { 9984, 0x2c }, { 10426, 0x2e },
    { 10887, 0x30 }, { 11369, 0x32 }, { 11873, 0x34 }, { 12398, 0x36 }, { 12947, 0x38 }, { 13520, 0x3a },
    { 14119, 0x3c }, { 14744, 0x3e }, { 15067, 0x40 }, { 15397, 0x42 }, { 15734, 0x44 }, { 16079, 0x46 },
    { 16431, 0x48 }, { 16790, 0x4a }, { 17158, 0x4c }, { 17534, 0x4e }, { 17918, 0x50 }, { 18310, 0x52 },
    { 18711, 0x54 }, { 19121, 0x56 }, { 19540, 0x58 }, { 19967, 0x5a }, { 20405, 0x5c }, { 20851, 0x5e },
    { 21308, 0x60 }, { 21775, 0x62 }, { 22251, 0x64 }, { 22739, 0x66 }, { 23236, 0x68 }, { 23745, 0x6a },
    { 24265, 0x6c }, { 24797, 0x6e }, { 25340, 0x70 }, { 25894, 0x72 }, { 26462, 0x74 }, { 27041, 0x76 },
    { 27633, 0x78 }, { 28238, 0x7a }, { 28856, 0x7c }, { 29488, 0x7e }, { 30134, 0x80 }, { 30794, 0x82 },
    { 31468, 0x84 }, { 32157, 0x86 }, { 32861, 0x88 }, { 33581, 0x8a }, { 34316, 0x8c }, { 35068, 0x8e },
    { 35836, 0x90 }, { 36620, 0x92 }, { 37422, 0x94 }, { 38242, 0x96 }, { 39079, 0x98 }, { 39935, 0x9a },
    { 40809, 0x9c }, { 41703, 0x9e }, { 42616, 0xa0 }, { 43549, 0xa2 }, { 44503, 0xa4 }, { 45477, 0xa6 },
    { 46473, 0xa8 }, { 47491, 0xaa }, { 48531, 0xac }, { 49593, 0xae }, { 50679, 0xb0 }, { 51789, 0xb2 },
    { 52923, 0xb4 }, { 54082, 0xb6 }, { 55266, 0xb8 }, { 56476, 0xba }, { 57713, 0xbc }, { 58977, 0xbe },
    { 60268, 0xc0 }, { 61588, 0xc2 }, { 62936, 0xc4 }, { 64315, 0xc6 }, { 65535, 0xc8 },
};
static const uint16_t lfa[101][2] = {
    { 0, 0x0040 }, { 514, 0x8040 }, { 775, 0x0041 }, { 921, 0x8041 }, { 1096, 0x0042 }, { 1303, 0x8042 },
    { 1550, 0x0043 }, { 1843, 0x8043 }, { 2192, 0x0044 }, { 2606, 0x8044 }, { 3100, 0x0045 }, { 3686, 0x8045 },
    { 4383, 0x0046 }, { 5213, 0x8046 }, { 6199, 0x0047 }, { 7372, 0x8047 }, { 7698, 0x0048 }, { 8039, 0x8048 },
    { 8395, 0x0049 }, { 8767, 0x8049 }, { 9155, 0x004a }, { 9560, 0x804a }, { 9984, 0x004b }, { 10426, 0x804b },
    { 10887, 0x004c }, { 11369, 0x804c }, { 11873, 0x004d }, { 12398, 0x804d }, { 12947, 0x004e }, { 13520, 0x804e },
    { 14119, 0x004f }, { 14744, 0x804f }, { 15067, 0x0050 }, { 15397, 0x8050 }, { 15734, 0x0051 }, { 16079, 0x8051 },
    { 16431, 0x0052 }, { 16790, 0x8052 }, { 17158, 0x0053 }, { 17534, 0x8053 }, { 17918, 0x0054 }, { 18310, 0x8054 },
    { 18711, 0x0055 }, { 19121, 0x8055 }, { 19540, 0x0056 }, { 19967, 0x8056 }, { 20405, 0x0057 }, { 20851, 0x8057 },
    { 21308, 0x0058 }, { 21775, 0x8058 }, { 22251, 0x0059 }, { 22739, 0x8059 }, { 23236, 0x005a }, { 23745, 0x805a },
    { 24265, 0x005b }, { 24797, 0x805b }, { 25340, 0x005c }, { 25894, 0x805c }, { 26462, 0x005d }, { 27041, 0x805d },
    { 27633, 0x005e }, { 28238, 0x805e }, { 28856, 0x005f }, { 29488, 0x805f }, { 30134, 0x0060 }, { 30794, 0x8060 },
    { 31468, 0x0061 }, { 32157, 0x8061 }, { 32861, 0x0062 }, { 33581, 0x8062 }, { 34316, 0x0063 }, { 35068, 0x8063 },
    { 35836, 0x0064 }, { 36620, 0x8064 }, { 37422, 0x0065 }, { 38242, 0x8065 }, { 39079, 0x0066 }, { 39935, 0x8066 },
    { 40809, 0x0067 }, { 41703, 0x8067 }, { 42616, 0x0068 }, { 43549, 0x8068 }, { 44503, 0x0069 }, { 45477, 0x8069 },
    { 46473, 0x006a }, { 47491, 0x806a }, { 48531, 0x006b }, { 49593, 0x806b }, { 50679, 0x006c }, { 51789, 0x806c },
    { 52923, 0x006d }, { 54082, 0x806d }, { 55266, 0x006e }, { 56476, 0x806e }, { 57713, 0x006f }, { 58977, 0x806f },
    { 60268, 0x0070 }, { 61588, 0x8070 }, { 62936, 0x0071 }, { 64315, 0x8071 }, { 65535, 0x0072 },
};

static uint16_t lookup(const uint16_t t[101][2], uint16_t amplitude) {
    for (int i = 0; i < 101; i++)
        if (amplitude <= t[i][0]) return t[i][1];
    return t[100][1];
}

// Frequency code of f Hz: round(log2(f / 10) * 32), clamped to what the field holds.
static int freq_code(float f, int lo, int hi) {
    int c = (int)lroundf(log2f((f > 1 ? f : 1) / 10) * 32);
    return c < lo ? lo : c > hi ? hi : c;
}

void switch_rumble_encode(const float v[4], uint8_t out[4]) {
    float al = v[0] > 0 ? (v[0] < 1 ? v[0] : 1) : 0, ah = v[2] > 0 ? (v[2] < 1 ? v[2] : 1) : 0;   // NaN -> 0
    uint8_t hamp = (uint8_t)lookup(hfa, (uint16_t)lroundf(ah * 65535));
    uint16_t lamp = lookup(lfa, (uint16_t)lroundf(al * 65535));
    if (!hamp && lamp == 0x0040) {   // SDL's neutral value: both amplitudes zero
        static const uint8_t neutral[4] = { 0x00, 0x01, 0x40, 0x40 };
        memcpy(out, neutral, 4);
        return;
    }
    uint16_t hf = (uint16_t)((freq_code(v[3], 0x60, 0xDF) - 0x60) * 4);   // 9 bits
    uint8_t lf = (uint8_t)(freq_code(v[1], 0x40, 0xBF) - 0x40);           // 7 bits
    out[0] = hf & 0xFF;
    out[1] = hamp | ((hf >> 8) & 0x01);
    out[2] = lf | ((lamp >> 8) & 0x80);
    out[3] = lamp & 0xFF;
}

void switch_rumble_packet(const float v[2][4], uint8_t out[10]) {
    out[0] = 0x10;   // SwitchCommonOutputPacket_t: type k_eSwitchOutputReportIDs_Rumble, the driver sets the counter
    out[1] = 0;
    switch_rumble_encode(v[0], out + 2);
    switch_rumble_encode(v[1], out + 6);
}
