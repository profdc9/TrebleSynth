/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

static const uint8_t font68 [] = {
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 000
0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 001
0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,  // 002
0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,  // 003
0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,  // 004
0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,  // 005
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,  // 006
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,  // 007
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 010
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 011
0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,  // 012
0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x18,  // 013
0x18,0x18,0x18,0x18,0xFF,0x7E,0x3C,0x18,  // 014
0x00,0x3C,0x7E,0x7E,0x7E,0x7E,0x3C,0x00,  // 015
0x00,0xC0,0xE0,0x7C,0x06,0x62,0x72,0x3E,
0x00,0x10,0x54,0x38,0xEE,0x38,0x54,0x10,
0x00,0xFE,0xFE,0x7C,0x7C,0x38,0x38,0x10,
0x00,0x10,0x38,0x38,0x7C,0x7C,0xFE,0xFE,
0x00,0x00,0x28,0x44,0xFE,0x44,0x28,0x00,
0x00,0xDE,0xDE,0x00,0x00,0xDE,0xDE,0x00,
0x00,0x0C,0x1E,0x12,0x12,0xFE,0x02,0xFE,
0x00,0x48,0x94,0xA4,0x4A,0x52,0x24,0x00,
0x00,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,
0x00,0x00,0xA8,0xC4,0xFE,0xC4,0xA8,0x00,
0x00,0x10,0x18,0xFC,0xFE,0xFC,0x18,0x10,
0x00,0x10,0x30,0x7E,0xFE,0x7E,0x30,0x10,
0x00,0x38,0x38,0x38,0xFE,0x7C,0x38,0x10,
0x00,0x10,0x38,0x7C,0xFE,0x38,0x38,0x38,
0x00,0xF8,0xF8,0xF8,0xC0,0xC0,0xC0,0xC0,
0x00,0x10,0x38,0x54,0x10,0x54,0x38,0x10,
0x00,0xC0,0xF0,0xFC,0xFE,0xFC,0xF0,0xC0,
0x00,0x06,0x1E,0x7E,0xFE,0x7E,0x1E,0x06,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x0C,0xBE,0xBE,0x0C,0x00,0x00,
0x00,0x00,0x06,0x0E,0x00,0x0E,0x06,0x00,
0x00,0x28,0xFE,0xFE,0x28,0xFE,0xFE,0x28,
0x00,0x00,0x48,0x54,0xD6,0x54,0x24,0x00,
0x00,0x46,0x66,0x30,0x18,0xCC,0xC4,0x00,
0x00,0x64,0xFE,0x8A,0x9A,0xEE,0xC4,0xA0,
0x00,0x00,0x10,0x1E,0x0E,0x00,0x00,0x00,
0x00,0x00,0x00,0x38,0x7C,0xC6,0x82,0x00,
0x00,0x82,0xC6,0x7C,0x38,0x00,0x00,0x00,
0x00,0x10,0x54,0x7C,0x38,0x7C,0x54,0x10,
0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,
0x00,0x80,0xF0,0x70,0x00,0x00,0x00,0x00,
0x00,0x00,0x10,0x10,0x10,0x10,0x00,0x00,
0x00,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,
0x00,0x40,0x60,0x30,0x18,0x0C,0x04,0x00,
0x00,0x7C,0xFE,0x92,0x8A,0xFE,0x7C,0x00,
0x00,0x80,0x88,0xFE,0xFE,0x80,0x80,0x00,
0x00,0xC4,0xE6,0xA2,0x92,0x9E,0x8C,0x00,
0x00,0x44,0xC6,0x92,0x92,0xFE,0x6C,0x00,
0x00,0x30,0x28,0x24,0xFE,0xFE,0x20,0x00,
0x00,0x4E,0xCE,0x8A,0x8A,0xFA,0x72,0x00,
0x00,0x7C,0xFE,0x92,0x92,0xF6,0x64,0x00,
0x00,0x06,0x06,0xE2,0xFA,0x1E,0x06,0x00,
0x00,0x6C,0xFE,0x92,0x92,0xFE,0x6C,0x00,
0x00,0x4C,0xDE,0x92,0x92,0xFE,0x7C,0x00,
0x00,0x00,0x00,0x6C,0x6C,0x00,0x00,0x00,
0x00,0x00,0x80,0xEC,0x6C,0x00,0x00,0x00,
0x00,0x00,0x10,0x38,0x6C,0xC6,0x82,0x00,
0x00,0x00,0x28,0x28,0x28,0x28,0x00,0x00,
0x00,0x82,0xC6,0x6C,0x38,0x10,0x00,0x00,
0x00,0x04,0x06,0xB2,0xB2,0x1E,0x0C,0x00,
0x00,0x3C,0x42,0x5A,0x5A,0x4C,0x20,0x00,
0x00,0xFC,0xFE,0x12,0x12,0xFE,0xFC,0x00,
0x00,0xFE,0xFE,0x92,0x92,0xFE,0x6C,0x00,
0x00,0x7C,0xFE,0x82,0x82,0xC6,0x44,0x00,
0x00,0xFE,0xFE,0x82,0x82,0xFE,0x7C,0x00,
0x00,0xFE,0xFE,0x92,0x92,0x92,0x82,0x00,
0x00,0xFE,0xFE,0x12,0x12,0x12,0x02,0x00,
0x00,0x7C,0xFE,0x82,0xA2,0xE6,0x64,0x00,
0x00,0xFE,0xFE,0x10,0x10,0xFE,0xFE,0x00,
0x00,0x00,0x82,0xFE,0xFE,0x82,0x00,0x00,
0x00,0x60,0xE0,0x82,0xFE,0x7E,0x02,0x00,
0x00,0xFE,0xFE,0x38,0x6C,0xC6,0x82,0x00,
0x00,0xFE,0xFE,0x80,0x80,0x80,0x80,0x00,
0x00,0xFE,0xFE,0x0C,0x18,0x0C,0xFE,0xFE,
0x00,0xFE,0xFE,0x0C,0x18,0x30,0xFE,0xFE,
0x00,0x7C,0xFE,0x82,0x82,0xFE,0x7C,0x00,
0x00,0xFE,0xFE,0x22,0x22,0x3E,0x1C,0x00,
0x00,0x3C,0x7E,0x42,0x62,0xFE,0xBC,0x00,
0x00,0xFE,0xFE,0x32,0x72,0xDE,0x8C,0x00,
0x00,0x4C,0xDE,0x92,0x92,0xF6,0x64,0x00,
0x00,0x06,0x02,0xFE,0xFE,0x02,0x06,0x00,
0x00,0x7E,0xFE,0x80,0x80,0xFE,0xFE,0x00,
0x00,0x3E,0x7E,0xC0,0xC0,0x7E,0x3E,0x00,
0x00,0xFE,0xFE,0x60,0x30,0x60,0xFE,0xFE,
0x00,0xC6,0xEE,0x38,0x10,0x38,0xEE,0xC6,
0x00,0x0E,0x1E,0xF0,0xF0,0x1E,0x0E,0x00,
0x00,0xC2,0xE2,0xB2,0x9A,0x8E,0x86,0x00,
0x00,0x00,0x00,0xFE,0xFE,0x82,0x82,0x00,
0x00,0x04,0x0C,0x18,0x30,0x60,0x40,0x00,
0x00,0x82,0x82,0xFE,0xFE,0x00,0x00,0x00,
0x00,0x10,0x08,0x04,0x02,0x04,0x08,0x10,
0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x00,0x00,0x00,0x00,0x06,0x0E,0x08,0x00,
0x00,0x40,0xE8,0xA8,0xA8,0xF8,0xF0,0x00,
0x00,0xFE,0xFE,0x90,0x90,0xF0,0x60,0x00,
0x00,0x70,0xF8,0x88,0x88,0xD8,0x50,0x00,
0x00,0x60,0xF0,0x90,0x90,0xFE,0xFE,0x00,
0x00,0x70,0xF8,0xA8,0xA8,0xB8,0x30,0x00,
0x00,0x20,0xFC,0xFE,0x22,0x26,0x04,0x00,
0x00,0x18,0xBC,0xA4,0xA4,0xFC,0x7C,0x00,
0x00,0xFE,0xFE,0x10,0x10,0xF0,0xE0,0x00,
0x00,0x00,0x80,0xF4,0xF4,0x80,0x00,0x00,
0x00,0x60,0xE0,0x80,0xFA,0x7A,0x00,0x00,
0x00,0xFE,0xFE,0x20,0x70,0xD8,0x88,0x00,
0x00,0x00,0x00,0xFE,0xFE,0x00,0x00,0x00,
0x00,0xF8,0xF8,0x30,0xE0,0x30,0xF8,0xF8,
0x00,0xF8,0xF8,0x18,0x18,0xF8,0xF0,0x00,
0x00,0x70,0xF8,0x88,0x88,0xF8,0x70,0x00,
0x00,0xFC,0xFC,0x24,0x24,0x3C,0x18,0x00,
0x00,0x18,0x3C,0x24,0xFC,0xFC,0x80,0xC0,
0x00,0xF8,0xF8,0x08,0x08,0x38,0x30,0x00,
0x00,0x90,0xA8,0xA8,0xA8,0xA8,0x48,0x00,
0x00,0x10,0x10,0xFC,0xFC,0x10,0x10,0x00,
0x00,0x78,0xF8,0x80,0x80,0xF8,0xF8,0x00,
0x00,0x30,0x70,0xC0,0xC0,0x70,0x30,0x00,
0x00,0x78,0xF8,0x80,0xF0,0x80,0xF8,0x78,
0x00,0x88,0xD8,0x70,0x70,0xD8,0x88,0x00,
0x00,0x18,0xB8,0xA0,0xA0,0xF8,0x78,0x00,
0x00,0x00,0xC8,0xE8,0xB8,0x98,0x00,0x00,
0x00,0x00,0x10,0x7C,0xEE,0x82,0x82,0x00,
0x00,0x00,0x00,0xEE,0xEE,0x00,0x00,0x00,
0x00,0x82,0x82,0xEE,0x7C,0x10,0x00,0x00,
0x00,0x10,0x18,0x08,0x18,0x10,0x08,0x00,
0x00,0xF0,0x98,0x8C,0x86,0x8C,0x98,0xF0
};


