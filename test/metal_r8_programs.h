/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned LLVM Bonaire vectors used by test_metal_r8_projection. */
/* Every VDATA register is initialized for sparse masks; array/nonarray bindings share coordinate bytecode. */
#ifndef PALEBLOOD_METAL_R8_PROGRAMS_H
#define PALEBLOOD_METAL_R8_PROGRAMS_H
#include <stddef.h>
#include <stdint.h>
typedef struct { const char *name; const uint32_t *words; size_t dwords; unsigned dim, da, op, mask; } R8Program;
static const uint32_t r8_load_d0_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0x7e1002ffu,
    0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu, 0xa5a5a5a5u, 0x7e1602ffu,
    0xa5a5a5a5u, 0xf0001f00u, 0x00020800u, 0xbf8c0f70u, 0x34180684u, 0xe0781000u,
    0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d0_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0xf0201f00u,
    0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_rw_d0_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0x7e1002ffu,
    0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu, 0xa5a5a5a5u, 0x7e1602ffu,
    0xa5a5a5a5u, 0xf0001f00u, 0x00020800u, 0xbf8c0f70u, 0x34180684u, 0xe0781000u,
    0x8000080cu, 0xbf8c0f70u, 0xf0201f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d0_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0x7e1002ffu,
    0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu, 0xa5a5a5a5u, 0x7e1602ffu,
    0xa5a5a5a5u, 0xf0005f00u, 0x00020800u, 0xbf8c0f70u, 0x34180684u, 0xe0781000u,
    0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d0_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0xf0205f00u,
    0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_rw_d0_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x4a000710u, 0x7e020312u,
    0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u, 0x7e0e0207u, 0x7e1002ffu,
    0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu, 0xa5a5a5a5u, 0x7e1602ffu,
    0xa5a5a5a5u, 0xf0005f00u, 0x00020800u, 0xbf8c0f70u, 0x34180684u, 0xe0781000u,
    0x8000080cu, 0xbf8c0f70u, 0xf0205f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m1[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001100u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m2[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001200u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m3[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001300u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m4[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001400u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m5[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001500u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m6[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001600u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m7[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001700u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m8[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001800u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_m9[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001900u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_ma[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001a00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_mb[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001b00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_mc[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001c00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_md[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001d00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_me[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001e00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001f00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m0[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201000u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m1[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201100u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m2[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201200u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m3[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201300u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m4[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201400u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m5[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201500u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m6[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201600u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m7[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201700u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m8[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201800u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_m9[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201900u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_ma[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201a00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_mb[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201b00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_mc[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201c00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_md[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201d00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_me[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201e00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0201f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_rw_d1_a0_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0001f00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xf0201f00u, 0x00020400u,
    0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m1[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005100u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m2[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005200u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m3[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005300u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m4[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005400u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m5[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005500u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m6[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005600u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m7[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005700u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m8[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005800u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_m9[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005900u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_ma[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005a00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_mb[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005b00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_mc[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005c00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_md[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005d00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_me[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005e00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_load_d1_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005f00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m0[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205000u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m1[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205100u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m2[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205200u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m3[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205300u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m4[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205400u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m5[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205500u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m6[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205600u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m7[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205700u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m8[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205800u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_m9[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205900u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_ma[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205a00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_mb[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205b00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_mc[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205c00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_md[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205d00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_me[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205e00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_store_d1_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0xf0205f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_rw_d1_a1_mf[] = {
    0x7e060300u, 0xe0380400u, 0x80001000u, 0xbf8c0f70u, 0x36000687u, 0x4a000110u,
    0x2c020683u, 0x4a020311u, 0x7e040312u, 0x7e080204u, 0x7e0a0205u, 0x7e0c0206u,
    0x7e0e0207u, 0x7e1002ffu, 0xa5a5a5a5u, 0x7e1202ffu, 0xa5a5a5a5u, 0x7e1402ffu,
    0xa5a5a5a5u, 0x7e1602ffu, 0xa5a5a5a5u, 0xf0005f00u, 0x00020800u, 0xbf8c0f70u,
    0x34180684u, 0xe0781000u, 0x8000080cu, 0xbf8c0f70u, 0xf0205f00u, 0x00020400u,
    0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_raw_copy[] = {
    0x8f088608u, 0x4a000008u, 0x34000082u, 0xe0301000u, 0x80000400u, 0xbf8c0f70u,
    0xe0701000u, 0x80010400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_dual_a0[] = {
    0x7e000283u, 0x7e020282u, 0x7e040280u, 0x7e0802ffu, 0x3e000000u, 0x7e0a02ffu,
    0x3ea00000u, 0x7e0c02ffu, 0x3f100000u, 0x7e0e02ffu, 0x3f500000u, 0xf0201f00u,
    0x00000400u, 0xbf8c0f70u, 0xf0201f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const uint32_t r8_dual_a1[] = {
    0x7e000283u, 0x7e020282u, 0x7e040280u, 0x7e0802ffu, 0x3e000000u, 0x7e0a02ffu,
    0x3ea00000u, 0x7e0c02ffu, 0x3f100000u, 0x7e0e02ffu, 0x3f500000u, 0xf0205f00u,
    0x00000400u, 0xbf8c0f70u, 0xf0205f00u, 0x00020400u, 0xbf8c0f70u, 0xbf810000u,
};
static const R8Program r8_programs[] = {
    {"r8_load_d0_a0_mf",r8_load_d0_a0_mf,sizeof r8_load_d0_a0_mf/sizeof r8_load_d0_a0_mf[0],0,0,0,15},
    {"r8_store_d0_a0_mf",r8_store_d0_a0_mf,sizeof r8_store_d0_a0_mf/sizeof r8_store_d0_a0_mf[0],0,0,1,15},
    {"r8_rw_d0_a0_mf",r8_rw_d0_a0_mf,sizeof r8_rw_d0_a0_mf/sizeof r8_rw_d0_a0_mf[0],0,0,2,15},
    {"r8_load_d0_a1_mf",r8_load_d0_a1_mf,sizeof r8_load_d0_a1_mf/sizeof r8_load_d0_a1_mf[0],0,1,0,15},
    {"r8_store_d0_a1_mf",r8_store_d0_a1_mf,sizeof r8_store_d0_a1_mf/sizeof r8_store_d0_a1_mf[0],0,1,1,15},
    {"r8_rw_d0_a1_mf",r8_rw_d0_a1_mf,sizeof r8_rw_d0_a1_mf/sizeof r8_rw_d0_a1_mf[0],0,1,2,15},
    {"r8_load_d1_a0_m1",r8_load_d1_a0_m1,sizeof r8_load_d1_a0_m1/sizeof r8_load_d1_a0_m1[0],1,0,0,1},
    {"r8_load_d1_a0_m2",r8_load_d1_a0_m2,sizeof r8_load_d1_a0_m2/sizeof r8_load_d1_a0_m2[0],1,0,0,2},
    {"r8_load_d1_a0_m3",r8_load_d1_a0_m3,sizeof r8_load_d1_a0_m3/sizeof r8_load_d1_a0_m3[0],1,0,0,3},
    {"r8_load_d1_a0_m4",r8_load_d1_a0_m4,sizeof r8_load_d1_a0_m4/sizeof r8_load_d1_a0_m4[0],1,0,0,4},
    {"r8_load_d1_a0_m5",r8_load_d1_a0_m5,sizeof r8_load_d1_a0_m5/sizeof r8_load_d1_a0_m5[0],1,0,0,5},
    {"r8_load_d1_a0_m6",r8_load_d1_a0_m6,sizeof r8_load_d1_a0_m6/sizeof r8_load_d1_a0_m6[0],1,0,0,6},
    {"r8_load_d1_a0_m7",r8_load_d1_a0_m7,sizeof r8_load_d1_a0_m7/sizeof r8_load_d1_a0_m7[0],1,0,0,7},
    {"r8_load_d1_a0_m8",r8_load_d1_a0_m8,sizeof r8_load_d1_a0_m8/sizeof r8_load_d1_a0_m8[0],1,0,0,8},
    {"r8_load_d1_a0_m9",r8_load_d1_a0_m9,sizeof r8_load_d1_a0_m9/sizeof r8_load_d1_a0_m9[0],1,0,0,9},
    {"r8_load_d1_a0_ma",r8_load_d1_a0_ma,sizeof r8_load_d1_a0_ma/sizeof r8_load_d1_a0_ma[0],1,0,0,10},
    {"r8_load_d1_a0_mb",r8_load_d1_a0_mb,sizeof r8_load_d1_a0_mb/sizeof r8_load_d1_a0_mb[0],1,0,0,11},
    {"r8_load_d1_a0_mc",r8_load_d1_a0_mc,sizeof r8_load_d1_a0_mc/sizeof r8_load_d1_a0_mc[0],1,0,0,12},
    {"r8_load_d1_a0_md",r8_load_d1_a0_md,sizeof r8_load_d1_a0_md/sizeof r8_load_d1_a0_md[0],1,0,0,13},
    {"r8_load_d1_a0_me",r8_load_d1_a0_me,sizeof r8_load_d1_a0_me/sizeof r8_load_d1_a0_me[0],1,0,0,14},
    {"r8_load_d1_a0_mf",r8_load_d1_a0_mf,sizeof r8_load_d1_a0_mf/sizeof r8_load_d1_a0_mf[0],1,0,0,15},
    {"r8_store_d1_a0_m0",r8_store_d1_a0_m0,sizeof r8_store_d1_a0_m0/sizeof r8_store_d1_a0_m0[0],1,0,1,0},
    {"r8_store_d1_a0_m1",r8_store_d1_a0_m1,sizeof r8_store_d1_a0_m1/sizeof r8_store_d1_a0_m1[0],1,0,1,1},
    {"r8_store_d1_a0_m2",r8_store_d1_a0_m2,sizeof r8_store_d1_a0_m2/sizeof r8_store_d1_a0_m2[0],1,0,1,2},
    {"r8_store_d1_a0_m3",r8_store_d1_a0_m3,sizeof r8_store_d1_a0_m3/sizeof r8_store_d1_a0_m3[0],1,0,1,3},
    {"r8_store_d1_a0_m4",r8_store_d1_a0_m4,sizeof r8_store_d1_a0_m4/sizeof r8_store_d1_a0_m4[0],1,0,1,4},
    {"r8_store_d1_a0_m5",r8_store_d1_a0_m5,sizeof r8_store_d1_a0_m5/sizeof r8_store_d1_a0_m5[0],1,0,1,5},
    {"r8_store_d1_a0_m6",r8_store_d1_a0_m6,sizeof r8_store_d1_a0_m6/sizeof r8_store_d1_a0_m6[0],1,0,1,6},
    {"r8_store_d1_a0_m7",r8_store_d1_a0_m7,sizeof r8_store_d1_a0_m7/sizeof r8_store_d1_a0_m7[0],1,0,1,7},
    {"r8_store_d1_a0_m8",r8_store_d1_a0_m8,sizeof r8_store_d1_a0_m8/sizeof r8_store_d1_a0_m8[0],1,0,1,8},
    {"r8_store_d1_a0_m9",r8_store_d1_a0_m9,sizeof r8_store_d1_a0_m9/sizeof r8_store_d1_a0_m9[0],1,0,1,9},
    {"r8_store_d1_a0_ma",r8_store_d1_a0_ma,sizeof r8_store_d1_a0_ma/sizeof r8_store_d1_a0_ma[0],1,0,1,10},
    {"r8_store_d1_a0_mb",r8_store_d1_a0_mb,sizeof r8_store_d1_a0_mb/sizeof r8_store_d1_a0_mb[0],1,0,1,11},
    {"r8_store_d1_a0_mc",r8_store_d1_a0_mc,sizeof r8_store_d1_a0_mc/sizeof r8_store_d1_a0_mc[0],1,0,1,12},
    {"r8_store_d1_a0_md",r8_store_d1_a0_md,sizeof r8_store_d1_a0_md/sizeof r8_store_d1_a0_md[0],1,0,1,13},
    {"r8_store_d1_a0_me",r8_store_d1_a0_me,sizeof r8_store_d1_a0_me/sizeof r8_store_d1_a0_me[0],1,0,1,14},
    {"r8_store_d1_a0_mf",r8_store_d1_a0_mf,sizeof r8_store_d1_a0_mf/sizeof r8_store_d1_a0_mf[0],1,0,1,15},
    {"r8_rw_d1_a0_mf",r8_rw_d1_a0_mf,sizeof r8_rw_d1_a0_mf/sizeof r8_rw_d1_a0_mf[0],1,0,2,15},
    {"r8_load_d1_a1_m1",r8_load_d1_a1_m1,sizeof r8_load_d1_a1_m1/sizeof r8_load_d1_a1_m1[0],1,1,0,1},
    {"r8_load_d1_a1_m2",r8_load_d1_a1_m2,sizeof r8_load_d1_a1_m2/sizeof r8_load_d1_a1_m2[0],1,1,0,2},
    {"r8_load_d1_a1_m3",r8_load_d1_a1_m3,sizeof r8_load_d1_a1_m3/sizeof r8_load_d1_a1_m3[0],1,1,0,3},
    {"r8_load_d1_a1_m4",r8_load_d1_a1_m4,sizeof r8_load_d1_a1_m4/sizeof r8_load_d1_a1_m4[0],1,1,0,4},
    {"r8_load_d1_a1_m5",r8_load_d1_a1_m5,sizeof r8_load_d1_a1_m5/sizeof r8_load_d1_a1_m5[0],1,1,0,5},
    {"r8_load_d1_a1_m6",r8_load_d1_a1_m6,sizeof r8_load_d1_a1_m6/sizeof r8_load_d1_a1_m6[0],1,1,0,6},
    {"r8_load_d1_a1_m7",r8_load_d1_a1_m7,sizeof r8_load_d1_a1_m7/sizeof r8_load_d1_a1_m7[0],1,1,0,7},
    {"r8_load_d1_a1_m8",r8_load_d1_a1_m8,sizeof r8_load_d1_a1_m8/sizeof r8_load_d1_a1_m8[0],1,1,0,8},
    {"r8_load_d1_a1_m9",r8_load_d1_a1_m9,sizeof r8_load_d1_a1_m9/sizeof r8_load_d1_a1_m9[0],1,1,0,9},
    {"r8_load_d1_a1_ma",r8_load_d1_a1_ma,sizeof r8_load_d1_a1_ma/sizeof r8_load_d1_a1_ma[0],1,1,0,10},
    {"r8_load_d1_a1_mb",r8_load_d1_a1_mb,sizeof r8_load_d1_a1_mb/sizeof r8_load_d1_a1_mb[0],1,1,0,11},
    {"r8_load_d1_a1_mc",r8_load_d1_a1_mc,sizeof r8_load_d1_a1_mc/sizeof r8_load_d1_a1_mc[0],1,1,0,12},
    {"r8_load_d1_a1_md",r8_load_d1_a1_md,sizeof r8_load_d1_a1_md/sizeof r8_load_d1_a1_md[0],1,1,0,13},
    {"r8_load_d1_a1_me",r8_load_d1_a1_me,sizeof r8_load_d1_a1_me/sizeof r8_load_d1_a1_me[0],1,1,0,14},
    {"r8_load_d1_a1_mf",r8_load_d1_a1_mf,sizeof r8_load_d1_a1_mf/sizeof r8_load_d1_a1_mf[0],1,1,0,15},
    {"r8_store_d1_a1_m0",r8_store_d1_a1_m0,sizeof r8_store_d1_a1_m0/sizeof r8_store_d1_a1_m0[0],1,1,1,0},
    {"r8_store_d1_a1_m1",r8_store_d1_a1_m1,sizeof r8_store_d1_a1_m1/sizeof r8_store_d1_a1_m1[0],1,1,1,1},
    {"r8_store_d1_a1_m2",r8_store_d1_a1_m2,sizeof r8_store_d1_a1_m2/sizeof r8_store_d1_a1_m2[0],1,1,1,2},
    {"r8_store_d1_a1_m3",r8_store_d1_a1_m3,sizeof r8_store_d1_a1_m3/sizeof r8_store_d1_a1_m3[0],1,1,1,3},
    {"r8_store_d1_a1_m4",r8_store_d1_a1_m4,sizeof r8_store_d1_a1_m4/sizeof r8_store_d1_a1_m4[0],1,1,1,4},
    {"r8_store_d1_a1_m5",r8_store_d1_a1_m5,sizeof r8_store_d1_a1_m5/sizeof r8_store_d1_a1_m5[0],1,1,1,5},
    {"r8_store_d1_a1_m6",r8_store_d1_a1_m6,sizeof r8_store_d1_a1_m6/sizeof r8_store_d1_a1_m6[0],1,1,1,6},
    {"r8_store_d1_a1_m7",r8_store_d1_a1_m7,sizeof r8_store_d1_a1_m7/sizeof r8_store_d1_a1_m7[0],1,1,1,7},
    {"r8_store_d1_a1_m8",r8_store_d1_a1_m8,sizeof r8_store_d1_a1_m8/sizeof r8_store_d1_a1_m8[0],1,1,1,8},
    {"r8_store_d1_a1_m9",r8_store_d1_a1_m9,sizeof r8_store_d1_a1_m9/sizeof r8_store_d1_a1_m9[0],1,1,1,9},
    {"r8_store_d1_a1_ma",r8_store_d1_a1_ma,sizeof r8_store_d1_a1_ma/sizeof r8_store_d1_a1_ma[0],1,1,1,10},
    {"r8_store_d1_a1_mb",r8_store_d1_a1_mb,sizeof r8_store_d1_a1_mb/sizeof r8_store_d1_a1_mb[0],1,1,1,11},
    {"r8_store_d1_a1_mc",r8_store_d1_a1_mc,sizeof r8_store_d1_a1_mc/sizeof r8_store_d1_a1_mc[0],1,1,1,12},
    {"r8_store_d1_a1_md",r8_store_d1_a1_md,sizeof r8_store_d1_a1_md/sizeof r8_store_d1_a1_md[0],1,1,1,13},
    {"r8_store_d1_a1_me",r8_store_d1_a1_me,sizeof r8_store_d1_a1_me/sizeof r8_store_d1_a1_me[0],1,1,1,14},
    {"r8_store_d1_a1_mf",r8_store_d1_a1_mf,sizeof r8_store_d1_a1_mf/sizeof r8_store_d1_a1_mf[0],1,1,1,15},
    {"r8_rw_d1_a1_mf",r8_rw_d1_a1_mf,sizeof r8_rw_d1_a1_mf/sizeof r8_rw_d1_a1_mf[0],1,1,2,15},
    {"r8_load_d2_a0_mf",r8_load_d0_a0_mf,sizeof r8_load_d0_a0_mf/sizeof r8_load_d0_a0_mf[0],2,0,0,15},
    {"r8_store_d2_a0_mf",r8_store_d0_a0_mf,sizeof r8_store_d0_a0_mf/sizeof r8_store_d0_a0_mf[0],2,0,1,15},
    {"r8_rw_d2_a0_mf",r8_rw_d0_a0_mf,sizeof r8_rw_d0_a0_mf/sizeof r8_rw_d0_a0_mf[0],2,0,2,15},
    {"r8_load_d2_a1_mf",r8_load_d0_a1_mf,sizeof r8_load_d0_a1_mf/sizeof r8_load_d0_a1_mf[0],2,1,0,15},
    {"r8_store_d2_a1_mf",r8_store_d0_a1_mf,sizeof r8_store_d0_a1_mf/sizeof r8_store_d0_a1_mf[0],2,1,1,15},
    {"r8_rw_d2_a1_mf",r8_rw_d0_a1_mf,sizeof r8_rw_d0_a1_mf/sizeof r8_rw_d0_a1_mf[0],2,1,2,15},
    {"r8_load_d3_a0_mf",r8_load_d1_a0_mf,sizeof r8_load_d1_a0_mf/sizeof r8_load_d1_a0_mf[0],3,0,0,15},
    {"r8_store_d3_a0_m2",r8_store_d1_a0_m2,sizeof r8_store_d1_a0_m2/sizeof r8_store_d1_a0_m2[0],3,0,1,2},
    {"r8_store_d3_a0_mf",r8_store_d1_a0_mf,sizeof r8_store_d1_a0_mf/sizeof r8_store_d1_a0_mf[0],3,0,1,15},
    {"r8_rw_d3_a0_mf",r8_rw_d1_a0_mf,sizeof r8_rw_d1_a0_mf/sizeof r8_rw_d1_a0_mf[0],3,0,2,15},
    {"r8_load_d3_a1_mf",r8_load_d1_a1_mf,sizeof r8_load_d1_a1_mf/sizeof r8_load_d1_a1_mf[0],3,1,0,15},
    {"r8_store_d3_a1_m2",r8_store_d1_a1_m2,sizeof r8_store_d1_a1_m2/sizeof r8_store_d1_a1_m2[0],3,1,1,2},
    {"r8_store_d3_a1_mf",r8_store_d1_a1_mf,sizeof r8_store_d1_a1_mf/sizeof r8_store_d1_a1_mf[0],3,1,1,15},
    {"r8_rw_d3_a1_mf",r8_rw_d1_a1_mf,sizeof r8_rw_d1_a1_mf/sizeof r8_rw_d1_a1_mf[0],3,1,2,15},
    {"r8_raw_copy",r8_raw_copy,sizeof r8_raw_copy/sizeof r8_raw_copy[0],0,0,3,0},
    {"r8_dual_a0",r8_dual_a0,sizeof r8_dual_a0/sizeof r8_dual_a0[0],3,0,4,15},
    {"r8_dual_a1",r8_dual_a1,sizeof r8_dual_a1/sizeof r8_dual_a1[0],3,1,4,15},
};
#endif
