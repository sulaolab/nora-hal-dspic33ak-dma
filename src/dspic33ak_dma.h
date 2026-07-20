/* SPDX-License-Identifier: MIT-0 */
#ifndef DSPIC33AK_DMA_H
#define DSPIC33AK_DMA_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Low-level DMA HAL (dsPIC33AK) - public interface.
 *
 * Clean low-level DMA abstraction used by SPI-TDM and PWM-audio consumers.
 * Channel ownership is a consumer/project-config decision, not a HAL policy;
 * a system enabling multiple DMA users must assign non-overlapping channels.
 *
 * Design boundaries (intentional)
 * -------------------------------
 *  - Knows nothing about SPI, audio, PWM, DSP, printf, or application code.
 *  - Contains NO ping-pong / block-streaming policy.  Ping-pong audio policy
 *    belongs later under audio (e.g. tdm_audio_dma_stream), which configures a
 *    channel through this HAL and decides what to do with each half/buffer.
 *  - The caller owns the DMA buffers; this HAL only takes addresses.
 *  - No callback framework: the DMA ISRs stay in the consumer modules.  Both
 *    current users already own their own _DMAxInterrupt() handlers.
 *
 * Not generalized (no current user needs it): scatter-gather, linked
 * descriptors, match mode, peripheral-chained channels (PCHEN/PPEN), runtime
 * channel allocation, OS integration.
 */

/* ----- DMA global address window -----
 * The allowed DMA address range programmed into DMALOW / DMAHIGH by
 * dspic33ak_dma_global_init(). Owned by the DMA HAL.
 */
#define DSPIC33AK_DMA_ADDR_WINDOW_LOW   (0x00000100UL)   /* -> DMALOW  */
#define DSPIC33AK_DMA_ADDR_WINDOW_HIGH  (0x00FFFFFFUL)   /* -> DMAHIGH */

/* DMAxSTAT status flags (raw status interpretation). */
#define DSPIC33AK_DMA_STAT_OVERRUN      (1UL << 3)        /* DMAxSTATbits OVERRUN */
#define DSPIC33AK_DMA_STAT_HALF         (1UL << 4)        /* DMAxSTATbits HALF */
#define DSPIC33AK_DMA_STAT_DONE         (1UL << 5)        /* DMAxSTATbits DONE */

/* DMAxCHbits.SIZE : transfer element width. */
typedef enum {
    DSPIC33AK_DMA_SIZE_BYTE     = 0,   /* 00: 1 byte    */
    DSPIC33AK_DMA_SIZE_HALFWORD = 1,   /* 01: 16-bit    */
    DSPIC33AK_DMA_SIZE_WORD     = 2,   /* 10: 32-bit (used by both current users) */
} dspic33ak_dma_size_t;

/* DMAxCHbits.SAMODE / DAMODE : address behavior after each element. */
typedef enum {
    DSPIC33AK_DMA_ADDR_FIXED     = 0,  /* 00: unchanged         */
    DSPIC33AK_DMA_ADDR_INCREMENT = 1,  /* 01: increment by SIZE */
    DSPIC33AK_DMA_ADDR_DECREMENT = 2,  /* 10: decrement by SIZE */
} dspic33ak_dma_addr_mode_t;

/* DMAxCHbits.TRMODE : transfer/repeat mode. */
typedef enum {
    DSPIC33AK_DMA_TRMODE_ONESHOT           = 0, /* 00: One-Shot                 */
    DSPIC33AK_DMA_TRMODE_REPEAT_ONESHOT    = 1, /* 01: Repeated One-Shot (used) */
    DSPIC33AK_DMA_TRMODE_CONTINUOUS        = 2, /* 10: Continuous               */
    DSPIC33AK_DMA_TRMODE_REPEAT_CONTINUOUS = 3, /* 11: Repeated Continuous      */
} dspic33ak_dma_trmode_t;

/*
 * One channel's configuration.
 *
 * Mirrors exactly the register fields the current code sets per channel; nothing
 * more.  RELOADS/RELOADD/RELOADC are explicit because the current RX/TX channels
 * use them asymmetrically (RX reloads dst, TX reloads src).
 */
typedef struct {
    volatile void            *src;          /* -> DMAxSRC  */
    volatile void            *dst;          /* -> DMAxDST  */
    /* Raw value written verbatim to DMAxCNT. On dsPIC33AK DMAxCNT is the number
     * of elements (of `size` width) to transfer per repeat -- it is NOT an
     * "elements - 1" register. Current users pass the element count of one
     * ping-pong half (ARRAY_SIZE() of that half-buffer). */
    uint32_t                  count;        /* -> DMAxCNT */

    dspic33ak_dma_addr_mode_t src_mode;     /* DMAxCHbits.SAMODE */
    dspic33ak_dma_addr_mode_t dst_mode;     /* DMAxCHbits.DAMODE */
    dspic33ak_dma_size_t      size;         /* DMAxCHbits.SIZE   */
    dspic33ak_dma_trmode_t    tr_mode;      /* DMAxCHbits.TRMODE */

    bool                      reload_count; /* DMAxCHbits.RELOADC */
    bool                      reload_src;   /* DMAxCHbits.RELOADS */
    bool                      reload_dst;   /* DMAxCHbits.RELOADD */

    bool                      half_int_en;  /* DMAxCHbits.HALFEN */
    bool                      done_int_en;  /* DMAxCHbits.DONEEN */

    uint8_t                   trigger_sel;  /* DMAxSELbits.CHSEL (Table 13-2 trigger ID) */

    /* CPU interrupt control.
     * irq_priority is written to _DMAxIP ONLY when irq_priority_set is true.
     * This preserves the current PWM behavior, where dma4_pwm5_config() does not
     * touch _DMA4IP (leaves the reset/default priority), while TDM does set it. */
    bool                      irq_priority_set;
    uint8_t                   irq_priority; /* _DMAxIP (0..7), used iff irq_priority_set */
    bool                      irq_enable;   /* _DMAxIE                                   */
} dspic33ak_dma_channel_cfg_t;

/* Pure DMA ping-pong timing mechanism (NOT policy):
 * maps the DMAxSTAT HALF/DONE flags to which buffer half just completed.
 * DONE takes precedence over HALF, matching the current RX handler behavior.
 */
typedef enum {
    DSPIC33AK_DMA_HALF_NONE   = 0,   /* neither HALF nor DONE set             */
    DSPIC33AK_DMA_HALF_FIRST  = 1,   /* HALF: first half just filled/emptied  */
    DSPIC33AK_DMA_HALF_SECOND = 2,   /* DONE: second half just filled/emptied */
} dspic33ak_dma_half_t;

static inline bool dspic33ak_dma_status_has_half_done_conflict(uint32_t status)
{
    const uint32_t mask = DSPIC33AK_DMA_STAT_HALF | DSPIC33AK_DMA_STAT_DONE;

    return ((status & mask) == mask);
}

/* ---- Global ---- */

/* Configure DMA global state.
 * Turns the DMA controller on and programs the allowed DMA address window.
 * Safe to call more than once; the address window is written each time.
 * No printf / halt / application handling. */
void dspic33ak_dma_global_init(void);

/* Returns true if the controller is on and the address window matches the
 * configured values. Side-effect-free: no register writes, no printf, no halt. */
bool dspic33ak_dma_global_is_ready(void);

/* ---- Per channel ---- */

/*
 * Invalid-channel handling convention across this API (ch >= device channel
 * count):
 *   - config / enable          return false (and write nothing).
 *   - void IRQ/status helpers  silently ignore the call (no register write).
 *   - read helpers             return 0.
 */

/* Configure a channel (SRC/DST/CNT, CH fields, trigger, IRQ priority/enable).
 * Leaves the channel DISABLED. Call dspic33ak_dma_channel_enable(ch, true) to
 * start.
 * Returns false (and writes NO channel register) if cfg is NULL, the channel
 * index is invalid, the DMA controller is not ready (dspic33ak_dma_global_init()
 * must have been called first), or cfg holds an out-of-range enum / IRQ
 * priority. Returns true on success. Never calls dspic33ak_dma_global_init()
 * itself.
 * Re-config safe: masks the channel's CPU IRQ and clears stale DMAxSTAT /
 * _DMAxIF before and after programming, so a stale interrupt or leftover
 * HALF/DONE status cannot disturb a stop -> re-config -> restart cycle. */
bool dspic33ak_dma_channel_config(uint8_t ch, const dspic33ak_dma_channel_cfg_t *cfg);

/* Set/clear DMAxCHbits.CHEN (start/stop the channel).
 * enable==true: returns false (writes nothing) if the channel index is invalid
 * or the DMA controller is not ready; otherwise sets CHEN and returns true.
 * enable==false: always disables (safe direction) and returns true, except for
 * an invalid channel index which returns false. */
bool dspic33ak_dma_channel_enable(uint8_t ch, bool enable);

/* General IRQ control: set/clear the channel's CPU interrupt enable (_DMAxIE),
 * independently of CHEN.
 * Needed by the TDM soft-stop path, which masks the DMA IRQ before stopping the
 * channel so the ISR cannot run during teardown. */
void dspic33ak_dma_irq_enable(uint8_t ch, bool enable);

/* General IRQ control: read the channel's CPU interrupt enable (_DMAxIE);
 * false for an invalid channel.
 * Lets a caller save/restore the IE state around a brief mask without hardcoding the
 * channel's SFR (used by the TDM core's per-instance RX-IE guard). */
bool dspic33ak_dma_irq_is_enabled(uint8_t ch);

/* Fast save/mask helper for short critical sections.
 * Prefer this over open-coded irq_is_enabled()+irq_enable(false) sequences.
 * static inline: compile-time-constant ch folds to direct _DMAxIE reads/writes,
 * which is useful for short application critical sections on hot audio paths.
 * The returned value is intended for dspic33ak_dma_irq_restore(). */
static inline bool dspic33ak_dma_irq_disable_save(uint8_t ch)
{
    bool was_enabled;

    switch (ch) {
    case 0: was_enabled = (_DMA0IE != 0u); _DMA0IE = 0u; break;
    case 1: was_enabled = (_DMA1IE != 0u); _DMA1IE = 0u; break;
    case 2: was_enabled = (_DMA2IE != 0u); _DMA2IE = 0u; break;
    case 3: was_enabled = (_DMA3IE != 0u); _DMA3IE = 0u; break;
    case 4: was_enabled = (_DMA4IE != 0u); _DMA4IE = 0u; break;
    case 5: was_enabled = (_DMA5IE != 0u); _DMA5IE = 0u; break;
#if defined(_DMA6IF)
    case 6: was_enabled = (_DMA6IE != 0u); _DMA6IE = 0u; break;
#endif
#if defined(_DMA7IF)
    case 7: was_enabled = (_DMA7IE != 0u); _DMA7IE = 0u; break;
#endif
    default: was_enabled = false; break;
    }
    return was_enabled;
}

/* Fast restore helper for short critical sections.
 * Restores a CPU interrupt enable state saved by dspic33ak_dma_irq_disable_save().
 * static inline for the same compile-time-constant folding as the save helper. */
static inline void dspic33ak_dma_irq_restore(uint8_t ch, bool was_enabled)
{
    const uint8_t v = was_enabled ? 1u : 0u;

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

/* Clear DMAxSTAT (status flags). */
void dspic33ak_dma_clear_status(uint8_t ch);

/* Clear the channel's CPU interrupt flag (_DMAxIF). */
void dspic33ak_dma_clear_irq_flag(uint8_t ch);

/* Read DMAxSTAT (raw). Use dspic33ak_dma_half_from_status() to interpret HALF/DONE. */
uint32_t dspic33ak_dma_read_status(uint8_t ch);

/* Read DMAxSRC (raw). The TX-side ping-pong consumer compares this against its
 * own half-buffer address; that comparison is buffer-layout policy and stays in
 * the consumer, not here.
 * static inline: compile-time-constant ch folds to a single register read
 * (no table lookup, no NULL guard). */
static inline uint32_t dspic33ak_dma_read_src(uint8_t ch)
{
    switch (ch) {
    case 0: return DMA0SRC;
    case 1: return DMA1SRC;
    case 2: return DMA2SRC;
    case 3: return DMA3SRC;
    case 4: return DMA4SRC;
    case 5: return DMA5SRC;
#if defined(_DMA6IF)
    case 6: return DMA6SRC;
#endif
#if defined(_DMA7IF)
    case 7: return DMA7SRC;
#endif
    default: return 0u;
    }
}

/* Interpret a DMAxSTAT value as a ping-pong half indicator (pure mechanism). */
dspic33ak_dma_half_t dspic33ak_dma_half_from_status(uint32_t status);

/* ---- ISR hot-path ---- */

/* Ordered ISR snapshot sequence (NOT a single atomic instruction): clear the CPU
 * interrupt flag, snapshot DMAxSTAT, then clear DMAxSTAT. Returns the raw
 * DMAxSTAT snapshot. Call with a compile-time-constant ch; the compiler folds the
 * switch to direct register accesses with no branch overhead. Operation order:
 * _DMAxIF=0, read DMAxSTAT, DMAxSTAT=0.
 *
 * Order note (verify against the device data sheet for the trigger/repeat modes
 * you use): clearing _DMAxIF before reading+clearing DMAxSTAT is intended so that
 * a HALF/DONE event occurring between the STAT read and the STAT clear remains
 * latched in DMAxSTAT (and re-asserts the flag) rather than being silently lost.
 * This ordering has not been independently characterised against every DMA mode;
 * confirm the DMAxSTAT/_DMAxIF latching behaviour if you rely on it. */
static inline uint32_t dspic33ak_dma_isr_snapshot(uint8_t ch)
{
    uint32_t stat;

    switch (ch) {
    case 0: _DMA0IF = 0; stat = DMA0STAT; DMA0STAT = 0; break;
    case 1: _DMA1IF = 0; stat = DMA1STAT; DMA1STAT = 0; break;
    case 2: _DMA2IF = 0; stat = DMA2STAT; DMA2STAT = 0; break;
    case 3: _DMA3IF = 0; stat = DMA3STAT; DMA3STAT = 0; break;
    case 4: _DMA4IF = 0; stat = DMA4STAT; DMA4STAT = 0; break;
    case 5: _DMA5IF = 0; stat = DMA5STAT; DMA5STAT = 0; break;
#if defined(_DMA6IF)
    case 6: _DMA6IF = 0; stat = DMA6STAT; DMA6STAT = 0; break;
#endif
#if defined(_DMA7IF)
    case 7: _DMA7IF = 0; stat = DMA7STAT; DMA7STAT = 0; break;
#endif
    default: stat = 0u; break;
    }
    return stat;
}

#endif /* DSPIC33AK_DMA_H */
