/* SPDX-License-Identifier: MIT-0 */
/*
 * Low-level DMA HAL (dsPIC33AK) - implementation.
 *
 * Uses dspic33ak_dma_reg.h for register bit masks/positions and generic SFR
 * helpers; no raw XC-DSC bitfield names (DMA0CHbits.*, DMA0SELbits.*, _DMA0IF,
 * ...) appear in this file except inside the small, documented per-channel IRQ
 * switch (the DMA IRQ Flag/Enable/Priority bits do not channel-index).
 *
 * This is the low-level DMA path used by the SPI-TDM driver for DMA global init,
 * channel config/enable, flag clear, stat/src read, and the ping-pong half query.
 *
 * Global init must be performed once (dspic33ak_dma_global_init(), called from
 * main) before any channel config. channel_config()/channel_enable() return
 * false (and write nothing) if the DMA controller is not ready; the caller
 * decides how to react. This module performs no printf / halt / app handling.
 *
 * No dependency on SPI, audio, PWM, DSP, printf, or application code.
 *
 * Device note: AK512 has DMA channels 0..7; AK128 has 0..5.  Channels 6/7 are
 * guarded by _DMA6IF / _DMA7IF (device-header macros present only where the
 * channel exists), so this file builds on both devices.
 */

#include <xc.h>

#include "dspic33ak_dma.h"
#include "dspic33ak_dma_reg.h"

/* ---------------------------------------------------------------------------
 * Private: per-channel register mapping
 *
 * All DMAxCH/SRC/DST/CNT/SEL/STAT registers are uniform 32-bit SFRs, so the
 * channel number (otherwise baked into the symbol name) is collapsed into an
 * index here.  The table auto-sizes per device (6 entries on AK128, 8 on AK512).
 * ------------------------------------------------------------------------- */

typedef struct {
    volatile uint32_t *CH;
    volatile uint32_t *SRC;
    volatile uint32_t *DST;
    volatile uint32_t *CNT;
    volatile uint32_t *SEL;
    volatile uint32_t *STAT;
} dma_ch_regs_t;

static const dma_ch_regs_t s_dma_ch[] = {
    { &DMA0CH, &DMA0SRC, &DMA0DST, &DMA0CNT, &DMA0SEL, &DMA0STAT },
    { &DMA1CH, &DMA1SRC, &DMA1DST, &DMA1CNT, &DMA1SEL, &DMA1STAT },
    { &DMA2CH, &DMA2SRC, &DMA2DST, &DMA2CNT, &DMA2SEL, &DMA2STAT },
    { &DMA3CH, &DMA3SRC, &DMA3DST, &DMA3CNT, &DMA3SEL, &DMA3STAT },
    { &DMA4CH, &DMA4SRC, &DMA4DST, &DMA4CNT, &DMA4SEL, &DMA4STAT },
    { &DMA5CH, &DMA5SRC, &DMA5DST, &DMA5CNT, &DMA5SEL, &DMA5STAT },
#if defined(_DMA6IF)
    { &DMA6CH, &DMA6SRC, &DMA6DST, &DMA6CNT, &DMA6SEL, &DMA6STAT },
#endif
#if defined(_DMA7IF)
    { &DMA7CH, &DMA7SRC, &DMA7DST, &DMA7CNT, &DMA7SEL, &DMA7STAT },
#endif
};

#define DMA_CH_COUNT  (sizeof(s_dma_ch) / sizeof(s_dma_ch[0]))

static const dma_ch_regs_t *dma_regs(uint8_t ch)
{
    if (ch >= DMA_CH_COUNT) {
        return (const dma_ch_regs_t *)0;
    }
    return &s_dma_ch[ch];
}

/* ---------------------------------------------------------------------------
 * Private: interrupt Flag/Enable/Priority
 *
 * Unlike the data/config registers, the DMA IRQ bits live in scattered CPU
 * registers (IFS2/IFS3, IEC2/IEC3, IPC9/IPC10/IPC14/IPC15) and do not
 * channel-index.  The per-channel mapping is isolated here as small switches
 * built on the device-header convenience macros.  Cases 6/7 are guarded for
 * AK128 (which has no DMA6/DMA7).
 * ------------------------------------------------------------------------- */

static void dma_irq_clear_flag(uint8_t ch)
{
    switch (ch) {
    case 0: _DMA0IF = 0; break;
    case 1: _DMA1IF = 0; break;
    case 2: _DMA2IF = 0; break;
    case 3: _DMA3IF = 0; break;
    case 4: _DMA4IF = 0; break;
    case 5: _DMA5IF = 0; break;
#if defined(_DMA6IF)
    case 6: _DMA6IF = 0; break;
#endif
#if defined(_DMA7IF)
    case 7: _DMA7IF = 0; break;
#endif
    default: break;
    }
}

static void dma_irq_enable(uint8_t ch, bool enable)
{
    const uint8_t v = enable ? 1u : 0u;

    switch (ch) {
    case 0: _DMA0IE = v; break;
    case 1: _DMA1IE = v; break;
    case 2: _DMA2IE = v; break;
    case 3: _DMA3IE = v; break;
    case 4: _DMA4IE = v; break;
    case 5: _DMA5IE = v; break;
#if defined(_DMA6IF)
    case 6: _DMA6IE = v; break;
#endif
#if defined(_DMA7IF)
    case 7: _DMA7IE = v; break;
#endif
    default: break;
    }
}

static bool dma_irq_is_enabled(uint8_t ch)
{
    switch (ch) {
    case 0: return (_DMA0IE != 0u);
    case 1: return (_DMA1IE != 0u);
    case 2: return (_DMA2IE != 0u);
    case 3: return (_DMA3IE != 0u);
    case 4: return (_DMA4IE != 0u);
    case 5: return (_DMA5IE != 0u);
#if defined(_DMA6IF)
    case 6: return (_DMA6IE != 0u);
#endif
#if defined(_DMA7IF)
    case 7: return (_DMA7IE != 0u);
#endif
    default: return false;
    }
}

static void dma_irq_set_priority(uint8_t ch, uint8_t prio)
{
    switch (ch) {
    case 0: _DMA0IP = prio; break;
    case 1: _DMA1IP = prio; break;
    case 2: _DMA2IP = prio; break;
    case 3: _DMA3IP = prio; break;
    case 4: _DMA4IP = prio; break;
    case 5: _DMA5IP = prio; break;
#if defined(_DMA6IF)
    case 6: _DMA6IP = prio; break;
#endif
#if defined(_DMA7IF)
    case 7: _DMA7IP = prio; break;
#endif
    default: break;
    }
}

/* ---------------------------------------------------------------------------
 * Global
 * ------------------------------------------------------------------------- */

void dspic33ak_dma_global_init(void)
{
    /* Configure DMA global state explicitly.
     * This turns the controller on and programs the allowed address window every
     * time (the window is written even if the controller was already on).
     * No printf / halt / application handling is performed here. */
    dspic33ak_dma_reg_set(&DMACON, DSPIC33AK_DMA_CON_ON);  /* DMACONbits.ON = 1 */

    /* The CPU X/Y data buses outrank DMA by default when both contend for SRAM.
     * Audio SPI requests arrive every 32-bit word and the channel has only one
     * pending CHREQ slot, so a delayed request becomes DMAxSTAT.OVERRUN. Give
     * DMA RAM transactions priority over the CPU; SFR arbitration is unchanged. */
    dspic33ak_dma_reg_set(&BMXINITPR, DSPIC33AK_BMX_INITPR_DMAPR);

    DMAHIGH = DSPIC33AK_DMA_ADDR_WINDOW_HIGH;
    DMALOW  = DSPIC33AK_DMA_ADDR_WINDOW_LOW;
}

bool dspic33ak_dma_global_is_ready(void)
{
    /* Side-effect-free readiness check: controller on and address window set to
     * the configured values. Returns a bool; never prints or halts. */
    if (!dspic33ak_dma_reg_is_set(&DMACON, DSPIC33AK_DMA_CON_ON)) {
        return false;
    }
    if (DMAHIGH != DSPIC33AK_DMA_ADDR_WINDOW_HIGH) {
        return false;
    }
    if (DMALOW != DSPIC33AK_DMA_ADDR_WINDOW_LOW) {
        return false;
    }
    if (!dspic33ak_dma_reg_is_set(&BMXINITPR, DSPIC33AK_BMX_INITPR_DMAPR)) {
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Per channel
 * ------------------------------------------------------------------------- */

bool dspic33ak_dma_channel_config(uint8_t ch, const dspic33ak_dma_channel_cfg_t *cfg)
{
    const dma_ch_regs_t *r = dma_regs(ch);

    /* Validate inputs and global state before touching any register. On failure
     * no DMA channel register is written. dspic33ak_dma_global_init() is never
     * called from here. */
    if ((r == 0) || (cfg == 0)) {
        return false;
    }
    if (!dspic33ak_dma_global_is_ready()) {
        return false;
    }

    /* Reject out-of-range enum / priority values rather than letting them mask
     * silently into the register fields (channel_config is allowed to fail). */
    if (cfg->size > DSPIC33AK_DMA_SIZE_WORD) {
        return false;
    }
    if (cfg->src_mode > DSPIC33AK_DMA_ADDR_DECREMENT) {
        return false;
    }
    if (cfg->dst_mode > DSPIC33AK_DMA_ADDR_DECREMENT) {
        return false;
    }
    if (cfg->tr_mode > DSPIC33AK_DMA_TRMODE_REPEAT_CONTINUOUS) {
        return false;
    }
    if (cfg->irq_priority_set && (cfg->irq_priority > 7u)) {
        return false;
    }

    /* Mask this channel's CPU IRQ before reconfiguring. If the channel ran
     * before, its _DMAxIE may still be enabled and a _DMAxIF may still be
     * pending; masking first prevents a stale DMA interrupt from firing while
     * the channel is being reprogrammed (re-config / restart safety). */
    dma_irq_enable(ch, false);

    /* Start from a known-disabled state (matches "DMAxCH = 0; CHEN = 0;") and
     * clear stale channel-side status + pending CPU flag before programming.
     * HALF/DONE left over from a previous run must not leak into the next run's
     * ping-pong half decision. */
    *r->CH   = 0u;
    *r->STAT = 0u;
    dma_irq_clear_flag(ch);

    /* Addresses and element count. Cast via uintptr_t (pointer -> integer) before
     * narrowing to the 32-bit register width. */
    *r->SRC = (uint32_t)(uintptr_t)cfg->src;
    *r->DST = (uint32_t)(uintptr_t)cfg->dst;
    *r->CNT = cfg->count;

    /* DMAxCH fields (CHEN intentionally left 0 here). */
    dspic33ak_dma_reg_write_field(r->CH, DSPIC33AK_DMA_CH_SAMODE_MASK,
                                  DSPIC33AK_DMA_CH_SAMODE_POS, (uint32_t)cfg->src_mode);
    dspic33ak_dma_reg_write_field(r->CH, DSPIC33AK_DMA_CH_DAMODE_MASK,
                                  DSPIC33AK_DMA_CH_DAMODE_POS, (uint32_t)cfg->dst_mode);
    dspic33ak_dma_reg_write_field(r->CH, DSPIC33AK_DMA_CH_SIZE_MASK,
                                  DSPIC33AK_DMA_CH_SIZE_POS, (uint32_t)cfg->size);
    dspic33ak_dma_reg_write_field(r->CH, DSPIC33AK_DMA_CH_TRMODE_MASK,
                                  DSPIC33AK_DMA_CH_TRMODE_POS, (uint32_t)cfg->tr_mode);

    if (cfg->reload_count) { dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_RELOADC); }
    if (cfg->reload_src)   { dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_RELOADS); }
    if (cfg->reload_dst)   { dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_RELOADD); }

    if (cfg->half_int_en)  { dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_HALFEN); }
    if (cfg->done_int_en)  { dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_DONEEN); }

    /* Trigger source (DMAxSELbits.CHSEL). */
    dspic33ak_dma_reg_write_field(r->SEL, DSPIC33AK_DMA_SEL_CHSEL_MASK,
                                  DSPIC33AK_DMA_SEL_CHSEL_POS, (uint32_t)cfg->trigger_sel);

    /* Clear stale channel status + pending CPU flag again after programming and
     * before (re-)enabling the IRQ, so the first post-config interrupt reflects
     * only the newly started transfer. */
    *r->STAT = 0u;
    dma_irq_clear_flag(ch);

    /* CPU interrupt: priority only if requested (preserves PWM's untouched IP),
     * then enable per cfg. */
    if (cfg->irq_priority_set) {
        dma_irq_set_priority(ch, cfg->irq_priority);
    }
    dma_irq_enable(ch, cfg->irq_enable);

    return true;
}

bool dspic33ak_dma_channel_enable(uint8_t ch, bool enable)
{
    const dma_ch_regs_t *r = dma_regs(ch);

    if (r == 0) {
        return false;
    }
    if (enable) {
        /* Do not start a channel when the DMA controller is not ready. */
        if (!dspic33ak_dma_global_is_ready()) {
            return false;
        }
        dspic33ak_dma_reg_set(r->CH, DSPIC33AK_DMA_CH_CHEN);
    } else {
        /* Disable is always allowed (safe direction), even if not "ready". */
        dspic33ak_dma_reg_clear(r->CH, DSPIC33AK_DMA_CH_CHEN);
    }
    return true;
}

void dspic33ak_dma_irq_enable(uint8_t ch, bool enable)
{
    dma_irq_enable(ch, enable);
}

bool dspic33ak_dma_irq_is_enabled(uint8_t ch)
{
    return dma_irq_is_enabled(ch);
}

void dspic33ak_dma_clear_status(uint8_t ch)
{
    const dma_ch_regs_t *r = dma_regs(ch);

    if (r == 0) {
        return;
    }
    *r->STAT = 0u;
}

void dspic33ak_dma_clear_irq_flag(uint8_t ch)
{
    dma_irq_clear_flag(ch);
}

uint32_t dspic33ak_dma_read_status(uint8_t ch)
{
    const dma_ch_regs_t *r = dma_regs(ch);

    if (r == 0) {
        return 0u;
    }
    return *r->STAT;
}

dspic33ak_dma_half_t dspic33ak_dma_half_from_status(uint32_t status)
{
    /* DONE takes precedence over HALF, matching the current RX handler
     * (tdm_get_src_ptr: the DONE branch overwrites the HALF branch). */
    if ((status & DSPIC33AK_DMA_STAT_DONE) != 0u) {
        return DSPIC33AK_DMA_HALF_SECOND;
    }
    if ((status & DSPIC33AK_DMA_STAT_HALF) != 0u) {
        return DSPIC33AK_DMA_HALF_FIRST;
    }
    return DSPIC33AK_DMA_HALF_NONE;
}
