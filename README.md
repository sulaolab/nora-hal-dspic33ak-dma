# dspic33ak-hal-dma

> Want to run it on hardware first?
> Start with [dspic33ak-hal-starter](https://github.com/sulaolab/dspic33ak-hal-starter),
> which vendors validated snapshots of the dsPIC33AK HAL repositories and
> provides a ready-to-build MPLAB X project for the dsPIC33AK Curiosity board.

Small, readable low-level DMA HAL for Microchip dsPIC33AK devices.

This repository provides a thin, explicit abstraction over the dsPIC33AK DMA
controller: global setup, per-channel configuration, channel start/stop,
interrupt-flag/status handling, and a few hot-path helpers for ping-pong audio
streaming. It is sized for its real users (a framed-SPI/I2S/TDM audio transport
and a PWM audio path), not as a general DMA framework.

The goal is not a complete DMA framework, but compact, explicit, easy-to-review
DMA building blocks for evaluation, FAE demos, and early software architecture
experiments.

## Status

Current validation target:

- Device: dsPIC33AK512MPS512
- Compiler: XC-DSC v3.31.01
- DFP: Microchip dsPIC33AK-MP DFP 1.3.185 or compatible
- Validation projects:
  - `perseus_512_96K` — DMA channels 0..7 as the configured ping-pong substrate
    of the SPI/I2S/TDM audio transport (live WM8904 I2S/TDM loopback; the
    selected physical SPI bank determines the channel set)
  - `dspic33ak-hal-starter` — DMA under the SPI1 TDM8 codec-less smoke demo

This HAL is validated as the DMA layer beneath those audio paths. It is the
low-level building block; the ping-pong / block-streaming policy lives in the
consumer (e.g. the SPI/I2S/TDM HAL), not here.

## Supported Devices

Hardware validated:

- dsPIC33AK512MPS512

The channel hot-path helpers compile for the available channel count: channels
6 and 7 are guarded by `_DMA6IF` / `_DMA7IF` so a device with fewer DMA channels
builds without them. Other dsPIC33AK devices should have their DMA SFR layout
(`DMAxCH` / `DMAxSRC` / `DMAxDST` / `DMAxCNT` / `DMAxSTAT` / `DMAxSEL`,
`DMACON`, `DMALOW` / `DMAHIGH`, and the `_DMAxIF` / `_DMAxIE` / `_DMAxIP`
symbols) reviewed before being listed as hardware-validated.

## Design Policy

This HAL is intentionally small and low-level.

- It knows nothing about SPI, audio, PWM, DSP, `printf`, or application code.
- It contains no ping-pong / block-streaming policy. Ping-pong audio policy
  belongs to the consumer, which configures a channel through this HAL and
  decides what to do with each completed half/buffer.
- The caller owns the DMA buffers; this HAL only takes addresses.
- No callback framework: the DMA ISRs stay in the consumer modules. Each user
  owns its own `_DMAxInterrupt()` handler and calls the hot-path helpers from it.
- No dynamic memory allocation, no RTOS or scheduler dependency.
- No clock-tree or PLL setup (outside this HAL).
- No XC-DSC / DFP bitfield types are exposed in the public API; configuration is
  expressed through plain enums and a config struct.

## Scope

In scope:

- DMA controller global enable + allowed address-window programming
- DMA-priority SRAM arbitration (`BMXINITPR.DMAPR=1`) to prevent the observed
  CPU X/Y SRAM-starvation path under DSP-heavy audio workloads
- Per-channel configuration (source/destination/count, address modes, element
  size, transfer/repeat mode, reload flags, trigger select, IRQ priority/enable)
- Channel start/stop (`CHEN`)
- Interrupt-enable control and save/restore for short critical sections
- Status / interrupt-flag clear and read
- Ping-pong half detection from the `HALF` / `DONE` status flags (pure mechanism)
- An ISR-hot-path snapshot helper (clear flag, read status, clear status)

Out of scope:

- Scatter-gather, linked descriptors, match mode
- Peripheral-chained channels (`PCHEN` / `PPEN`)
- Runtime channel allocation
- Ping-pong / double-buffer streaming policy (lives in the consumer)
- OS / scheduler integration

## Files

```text
src/
  dspic33ak_dma.c
  dspic33ak_dma.h
  dspic33ak_dma_reg.h
```

## Integration

1. Add `src/dspic33ak_dma.c` to the project.
2. Add `src/` to the compiler include path.
3. Include `dspic33ak_dma.h`.
4. Call `dspic33ak_dma_global_init()` once before configuring channels.
5. Own each used channel's `_DMAxInterrupt()` vector in the consumer module and
   call the hot-path helpers from it.

## Basic Usage

Bring up the DMA controller once, then configure and start a channel:

```c
#include "dspic33ak_dma.h"

dspic33ak_dma_global_init();

const dspic33ak_dma_channel_cfg_t rx_cfg = {
    .src          = (volatile void *)&SPI1BUFR,
    .dst          = rx_buffer,
    .count        = RX_BUFFER_ELEMENTS,
    .src_mode     = DSPIC33AK_DMA_ADDR_FIXED,
    .dst_mode     = DSPIC33AK_DMA_ADDR_INCREMENT,
    .size         = DSPIC33AK_DMA_SIZE_WORD,        /* 32-bit */
    .tr_mode      = DSPIC33AK_DMA_TRMODE_REPEAT_CONTINUOUS,
    .reload_count = true,
    .reload_dst   = true,
    .half_int_en  = true,
    .done_int_en  = true,
    .trigger_sel  = SPI1_RX_DMA_TRIGGER_ID,
    .irq_priority_set = true,
    .irq_priority = 4u,
    .irq_enable   = true,
};

if (dspic33ak_dma_channel_config(0u, &rx_cfg)) {
    dspic33ak_dma_channel_enable(0u, true);
}
```

In the consumer's DMA ISR, use the hot-path helper (call with a compile-time
constant channel so the compiler folds it to direct register accesses):

```c
void __attribute__((interrupt, context)) _DMA0Interrupt(void)
{
    uint32_t stat = dspic33ak_dma_isr_snapshot(0u);   /* clear IF, read+clear STAT */

    switch (dspic33ak_dma_half_from_status(stat)) {
    case DSPIC33AK_DMA_HALF_FIRST:  /* first half ready  */ break;
    case DSPIC33AK_DMA_HALF_SECOND: /* second half ready */ break;
    default: break;
    }
}
```

For short critical sections that must not be interrupted by a channel's DMA ISR,
save and restore its interrupt-enable:

```c
bool was = dspic33ak_dma_irq_disable_save(0u);
/* ... brief critical work ... */
dspic33ak_dma_irq_restore(0u, was);
```

## API Summary

Global:

- `dspic33ak_dma_global_init()` — turn the DMA controller on, give DMA SRAM
  accesses priority over CPU X/Y traffic (`BMXINITPR.DMAPR=1`), and program the
  allowed DMA address window (`DMALOW` / `DMAHIGH`). Safe to call more than once.
- `dspic33ak_dma_global_is_ready()` — return whether the controller is on and the
  priority/address-window configuration matches the required values.
  Side-effect-free.

Per channel:

- `dspic33ak_dma_channel_config()` — configure a channel and leave it disabled.
  Returns false (and writes no register) for a NULL config, invalid channel,
  controller not ready, or an out-of-range enum / IRQ priority in the config.
  Re-config safe: masks the channel's CPU IRQ and clears stale `DMAxSTAT` /
  `_DMAxIF` before and after programming, so a leftover interrupt or `HALF` /
  `DONE` status from a previous run cannot disturb a stop → re-config → restart
  cycle.
- `dspic33ak_dma_channel_enable()` — set/clear `CHEN` (start/stop the channel).
- `dspic33ak_dma_irq_enable()` — set/clear the channel's CPU interrupt enable
  (`_DMAxIE`) independently of `CHEN`.
- `dspic33ak_dma_irq_is_enabled()` — read the channel's `_DMAxIE`.
- `dspic33ak_dma_irq_disable_save()` / `dspic33ak_dma_irq_restore()` — fast
  inline save/mask and restore for short critical sections.
- `dspic33ak_dma_clear_status()` — clear `DMAxSTAT`.
- `dspic33ak_dma_clear_irq_flag()` — clear `_DMAxIF`.
- `dspic33ak_dma_read_status()` — read raw `DMAxSTAT`.
- `dspic33ak_dma_read_src()` — read raw `DMAxSRC` (inline).

Ping-pong / ISR hot path:

- `dspic33ak_dma_status_has_half_done_conflict()` — true if both `HALF` and
  `DONE` are set in a status snapshot (a missed-deadline indicator for the
  consumer).
- `dspic33ak_dma_half_from_status()` — interpret a `DMAxSTAT` value as a
  ping-pong half indicator (`DONE` takes precedence over `HALF`).
- `dspic33ak_dma_isr_snapshot()` — inline: clear `_DMAxIF`, snapshot `DMAxSTAT`,
  clear `DMAxSTAT`; returns the raw snapshot. Call with a compile-time-constant
  channel.

## Behavior Notes

- **`count` semantics.** `dspic33ak_dma_channel_cfg_t.count` is written verbatim
  to `DMAxCNT`. On dsPIC33AK `DMAxCNT` is the number of elements (of the
  configured `size`) to transfer per repeat — it is **not** an "elements − 1"
  register. Pass the element count of one ping-pong half.
- **Invalid-channel convention** (channel index ≥ the device channel count):
  - `config` / `enable` return `false` and write nothing.
  - `void` IRQ/status helpers silently ignore the call.
  - read helpers return `0`.
- **ISR snapshot ordering.** `dspic33ak_dma_isr_snapshot()` performs an *ordered*
  sequence (`_DMAxIF=0`, read `DMAxSTAT`, `DMAxSTAT=0`), not a single atomic
  instruction. The order is chosen so a `HALF` / `DONE` event raised mid-sequence
  stays latched rather than being lost; confirm the latching behavior against the
  device data sheet for the DMA modes you use.
- **Global arbitration policy.** `dspic33ak_dma_global_init()` sets
  `BMXINITPR.DMAPR=1` for the entire device. This prioritizes DMA over CPU X/Y
  SRAM accesses while leaving SFR arbitration unchanged. It prevents the
  observed CPU X/Y SRAM-starvation path that caused `DMAxSTAT.OVERRUN` under
  DSP-heavy audio workloads, but it is not a blanket guarantee against every
  possible overrun cause. Integrators must include every DMA consumer and
  CPU/DSP timing path in system-level regression tests.

## Notes

- This repository does not include Microchip DFP header files.
- This HAL is the DMA layer only; clock setup and peripheral (SPI/PWM/...) setup
  belong to the board/application and the consuming HALs.
- This is the canonical DMA HAL used by
  [dspic33ak-hal-spi-i2s-tdm](https://github.com/sulaolab/dspic33ak-hal-spi-i2s-tdm)
  and vendored into the dsPIC33AK HAL starter and CMSIS-Driver SAI repositories.

## License

MIT No Attribution License (MIT-0). See [LICENSE](LICENSE).

Attribution is appreciated but not required.
