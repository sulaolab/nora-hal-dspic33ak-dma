/* SPDX-License-Identifier: MIT-0 */
#ifndef DSPIC33AK_DMA_REG_H
#define DSPIC33AK_DMA_REG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Internal DMA register helper layer.
 *
 * Similar in spirit to dspic33ak_i2c_reg.h: this file intentionally uses plain
 * 32-bit bit masks / bit positions and a few minimal generic helpers instead of
 * XC-DSC bitfield structures such as DMAxCHbits.  The goal is to keep
 * compiler/DFP-specific details away from the readable driver.
 *
 * This file is NOT an active register-access driver.  It deliberately does NOT
 * contain:
 *   - channel lookup functions / register pointer maps,
 *   - IRQ clear/enable/priority operations.
 * Those live as private static helpers in dspic33ak_dma.c.
 *
 * No SPI, audio, ping-pong, or DSP knowledge.  No owned state.
 *
 * Bit positions/masks were checked against:
 *   Microchip dsPIC33AK-MP_DFP 1.2.135  xc16/support/dsPIC33A/h/p33AK512MPS512.h
 *   Microchip dsPIC33AK-MC_DFP 1.4.172  xc16/support/dsPIC33A/h/p33AK128MC106.h
 * The DMAxCH / DMAxSEL / DMAxSTAT bit layout is identical across both devices
 * and across all channels.
 *
 * Keep this file small.  Add only the bits actually used by the readable driver.
 */

/* ---- DMACON (global control) ---- */
#define DSPIC33AK_DMA_CON_ON          (1UL << 15)   /* DMACONbits.ON           */
#define DSPIC33AK_BMX_INITPR_DMAPR    (1UL << 0)    /* BMXINITPRbits.DMAPR     */

/* ---- DMAxCH single-bit fields ---- */
#define DSPIC33AK_DMA_CH_CHEN         (1UL << 0)    /* DMAxCHbits.CHEN          */
#define DSPIC33AK_DMA_CH_HALFEN       (1UL << 1)    /* DMAxCHbits.HALFEN        */
#define DSPIC33AK_DMA_CH_DONEEN       (1UL << 3)    /* DMAxCHbits.DONEEN        */
#define DSPIC33AK_DMA_CH_RELOADS      (1UL << 24)   /* DMAxCHbits.RELOADS       */
#define DSPIC33AK_DMA_CH_RELOADD      (1UL << 25)   /* DMAxCHbits.RELOADD       */
#define DSPIC33AK_DMA_CH_RELOADC      (1UL << 26)   /* DMAxCHbits.RELOADC       */

/* ---- DMAxCH multi-bit fields (position + mask) ---- */
#define DSPIC33AK_DMA_CH_SIZE_POS     (6)           /* DMAxCHbits.SIZE          */
#define DSPIC33AK_DMA_CH_SIZE_MASK    (0x3UL  << DSPIC33AK_DMA_CH_SIZE_POS)
#define DSPIC33AK_DMA_CH_TRMODE_POS   (10)          /* DMAxCHbits.TRMODE        */
#define DSPIC33AK_DMA_CH_TRMODE_MASK  (0x3UL  << DSPIC33AK_DMA_CH_TRMODE_POS)
#define DSPIC33AK_DMA_CH_DAMODE_POS   (12)          /* DMAxCHbits.DAMODE        */
#define DSPIC33AK_DMA_CH_DAMODE_MASK  (0x3UL  << DSPIC33AK_DMA_CH_DAMODE_POS)
#define DSPIC33AK_DMA_CH_SAMODE_POS   (14)          /* DMAxCHbits.SAMODE        */
#define DSPIC33AK_DMA_CH_SAMODE_MASK  (0x3UL  << DSPIC33AK_DMA_CH_SAMODE_POS)

/* ---- DMAxSEL field ---- */
#define DSPIC33AK_DMA_SEL_CHSEL_POS   (0)           /* DMAxSELbits.CHSEL        */
#define DSPIC33AK_DMA_SEL_CHSEL_MASK  (0xFFUL << DSPIC33AK_DMA_SEL_CHSEL_POS)

/* ---- Minimal generic 32-bit SFR access helpers ---- */
static inline void dspic33ak_dma_reg_set(volatile uint32_t *reg, uint32_t mask)
{
    *reg |= mask;
}

static inline void dspic33ak_dma_reg_clear(volatile uint32_t *reg, uint32_t mask)
{
    *reg &= ~mask;
}

static inline bool dspic33ak_dma_reg_is_set(volatile uint32_t *reg, uint32_t mask)
{
    return ((*reg & mask) != 0u);
}

static inline void dspic33ak_dma_reg_write_field(volatile uint32_t *reg,
                                                 uint32_t mask,
                                                 uint32_t pos,
                                                 uint32_t value)
{
    *reg = (*reg & ~mask) | ((value << pos) & mask);
}

#endif /* DSPIC33AK_DMA_REG_H */
