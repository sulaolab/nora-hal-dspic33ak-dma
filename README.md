# nora-hal-dspic33ak-dma

**NORA-HAL** — *Native On-chip Resource Assistant*

Small, readable low-level DMA HAL for Microchip dsPIC33AK devices — part of
**NORA-HAL**, a HAL family whose public API is namespaced `nora_*` /
`NORA_*`.

> Want to run it on hardware first?
> Start with [dspic33ak-hal-starter](https://github.com/sulaolab/dspic33ak-hal-starter),
> which vendors validated snapshots of the NORA-HAL repositories and
> provides a ready-to-build MPLAB X project for the dsPIC33AK Curiosity board.

> **This repository is a published snapshot, not the development tree.** Every
> file under `src/` is byte-identical to its counterpart in
> `dspic33ak-hal-starter`, which is in turn byte-identical to the audio-board
> project that runs these sources on hardware. Fixes flow *into* here from that
> validated tree — see [docs/nora_migration.md](docs/nora_migration.md).
>
> **One exception, 2026-08-09 — since converged.** Documentation and comment
> corrections under `src/` were made here first, ahead of the audio-board upstream.
> **No executable code changed.** They have since been merged into that upstream
> tree, so nothing here is ahead of it any more and the direction above holds
> without exception. The files, the corrections, and the upstream commit are listed
> in [docs/nora_migration.md](docs/nora_migration.md).

This repository provides a thin, explicit abstraction over the dsPIC33AK DMA
controller: global setup, per-channel configuration, channel start/stop,
interrupt-flag/status handling, and a few hot-path helpers for ping-pong audio
streaming. It is sized for its real users (a framed-SPI/I2S/TDM audio transport
and a PWM audio path), not as a general DMA framework.

The goal is not a complete DMA framework, but compact, explicit, easy-to-review
DMA building blocks for evaluation, FAE demos, and early software architecture
experiments.

## Naming

The public API is `nora_*` / `NORA_*`. It replaces the `dspic33ak_*` /
`DSPIC33AK_*` namespace this repository used before 2026-08, and **there are no
compatibility aliases** — a consumer moving to this version renames its call
sites. The **public** namespace migration is textual: `dspic33ak_` → `nora_`,
`DSPIC33AK_` → `NORA_`. It is not a tree-wide substitution — backend-private
names deliberately retain the silicon tag, as below. (This version also changes
two things that a rename alone will not cover; see
[Migrating from the previous version](#migrating-from-the-previous-version).)

The chip name survives in exactly two places, both deliberate:

* **Implementation file names** carry a backend tag: `nora_dma_dspic33ak.c` is
  the dsPIC33AK backend of the processor-neutral `nora_dma.h`. A second
  processor would add `nora_dma_<tag>.c` beside it, not a second header.
* **Backend-private identifiers** inside those files (register-layer macros and
  statics), which no caller sees.

The tag is `_dspic33ak`, the device family this backend actually drives — not
`_dspic33a`, which is the *core* family name (dsPIC33A) and one level too
coarse. A dsPIC33CK backend would be tagged `_dspic33ck`: a different silicon
family (dsPIC33**C**), and never abbreviated to `_dspic33c`.

## Status

Current validation target:

- Device: dsPIC33AK512MPS512
- Compiler: XC-DSC v3.31.01
- DFP: Microchip dsPIC33AK-MP DFP 1.3.185 or compatible
- Validation projects:
  - the upstream audio project — DMA channels 0..7 as the configured ping-pong substrate
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
  nora_dma.h                    public contract — no device header pulled in
  nora_dma_dspic33ak.c          dsPIC33AK backend
  nora_dma_dspic33ak_fast.h     backend-private ISR fast path (exposes SFRs)
  nora_dma_dspic33ak_reg.h      backend-private register layer
  README.md                     the module contract, as the upstream project states it
```

`nora_dma.h` deliberately does **not** `#include <xc.h>`: including the contract
does not drag the device header into a translation unit that only wants the API.
The two `_dspic33ak` headers do expose SFRs and are for the backend and for
backend-aware, measured hot paths only.

## Integration

1. Add `src/nora_dma_dspic33ak.c` to the project.
2. Add `src/` to the compiler include path.
3. Include `nora_dma.h`.
4. Call `nora_dma_global_init()` once before configuring channels.
5. Own each used channel's `_DMAxInterrupt()` vector in the consumer module. Use
   the ordinary calls, or include `nora_dma_dspic33ak_fast.h` and use the `_hot`
   inlines when the ISR is a measured hot path.

## Basic Usage

Bring up the DMA controller once, then configure and start a channel:

```c
#include "nora_dma.h"

nora_dma_global_init();

const nora_dma_channel_cfg_t rx_cfg = {
    .src          = (volatile void *)&SPI1BUFR,
    .dst          = rx_buffer,
    .count        = RX_BUFFER_ELEMENTS,
    .src_mode     = NORA_DMA_ADDR_FIXED,
    .dst_mode     = NORA_DMA_ADDR_INCREMENT,
    .size         = NORA_DMA_SIZE_WORD,        /* 32-bit */
    .tr_mode      = NORA_DMA_TRMODE_REPEAT_CONTINUOUS,
    .reload_count = true,
    .reload_dst   = true,
    .half_int_en  = true,
    .done_int_en  = true,
    .trigger      = NORA_DMA_TRIGGER_SPI1_RX,
    .irq_priority_set = true,
    .irq_priority = 4u,
    .irq_enable   = true,
};

if (nora_dma_channel_config(NORA_DMA_CHANNEL_0, &rx_cfg)) {
    nora_dma_channel_enable(NORA_DMA_CHANNEL_0, true);
}
```

`.trigger` names the peripheral event; the backend owns the device trigger ID, so
the call site does not need the trigger table from the data sheet.

In the consumer's DMA ISR, the hot-path variant folds to direct register accesses
when the channel is a compile-time constant:

```c
#include "nora_dma_dspic33ak_fast.h"   /* backend-private: exposes SFRs */

void __attribute__((interrupt, context)) _DMA0Interrupt(void)
{
    /* clear IF, read+clear STAT */
    nora_dma_status_t stat = nora_dma_isr_snapshot_hot(NORA_DMA_CHANNEL_0);

    switch (nora_dma_half_from_status(stat)) {
    case NORA_DMA_HALF_FIRST:  /* first half ready  */ break;
    case NORA_DMA_HALF_SECOND: /* second half ready */ break;
    default: break;
    }
}
```

For short critical sections that must not be interrupted by a channel's DMA ISR,
save and restore its interrupt-enable:

```c
bool was = nora_dma_irq_disable_save(NORA_DMA_CHANNEL_0);
/* ... brief critical work ... */
nora_dma_irq_restore(NORA_DMA_CHANNEL_0, was);
```

`nora_dma_irq_disable_save_hot()` / `_restore_hot()` in
`nora_dma_dspic33ak_fast.h` are the inline equivalents for a hot path.

## API Summary

Global:

- `nora_dma_global_init()` — turn the DMA controller on, give DMA SRAM
  accesses priority over CPU X/Y traffic (`BMXINITPR.DMAPR=1`), and program the
  allowed DMA address window (`DMALOW` / `DMAHIGH`). Safe to call more than once.
- `nora_dma_global_is_ready()` — return whether the controller is on and the
  priority/address-window configuration matches the required values.
  Side-effect-free.

Per channel:

- `nora_dma_channel_config()` — configure a channel and leave it disabled.
  Returns false (and writes no register) for a NULL config, invalid channel,
  controller not ready, or an out-of-range enum / IRQ priority in the config.
  Re-config safe: masks the channel's CPU IRQ and clears stale `DMAxSTAT` /
  `_DMAxIF` before and after programming, so a leftover interrupt or `HALF` /
  `DONE` status from a previous run cannot disturb a stop → re-config → restart
  cycle.
- `nora_dma_channel_enable()` — set/clear `CHEN` (start/stop the channel).
- `nora_dma_irq_enable()` — set/clear the channel's CPU interrupt enable
  (`_DMAxIE`) independently of `CHEN`.
- `nora_dma_irq_is_enabled()` — read the channel's `_DMAxIE`.
- `nora_dma_irq_disable_save()` / `nora_dma_irq_restore()` — save/mask and
  restore a channel's interrupt enable for short critical sections.
- `nora_dma_clear_status()` — clear `DMAxSTAT`.
- `nora_dma_clear_irq_flag()` — clear `_DMAxIF`.
- `nora_dma_read_status()` — read raw `DMAxSTAT` as a `nora_dma_status_t`.
- `nora_dma_read_src()` — read raw `DMAxSRC`.

Ping-pong / ISR:

- `nora_dma_status_has_completed_half()` — true if the snapshot reports a
  completed half (`HALF`).
- `nora_dma_status_has_overrun()` — true if the snapshot reports `OVERRUN`.
- `nora_dma_status_has_half_done_conflict()` — true if both `HALF` and
  `DONE` are set in a status snapshot (a missed-deadline indicator for the
  consumer).
- `nora_dma_half_from_status()` — interpret a status value as a ping-pong half
  indicator (`DONE` takes precedence over `HALF`).
- `nora_dma_isr_snapshot()` — clear `_DMAxIF`, snapshot `DMAxSTAT`, clear
  `DMAxSTAT`; returns the snapshot.

The status bits are read through those accessors rather than through exported
bit masks, so `nora_dma_status_t` stays an opaque carrier.

Hot path (`nora_dma_dspic33ak_fast.h`, backend-private):

Each entry is a `static inline` shadow of the portable call above, named
`<portable stem>_hot`, and the out-of-line version in the backend `.c` is
literally a call to the inline. Call them with a compile-time-constant channel
so the switch folds to a direct SFR access.

- `nora_dma_isr_snapshot_hot()`
- `nora_dma_irq_disable_save_hot()` / `nora_dma_irq_restore_hot()`
- `nora_dma_read_src_hot()`
- `nora_dma_status_has_completed_half_hot()` / `_has_overrun_hot()` /
  `_has_half_done_conflict_hot()`

The `_hot` suffix sits on the portable stem on purpose. An ISR body written
against `_hot` names ports to another backend unchanged — only the `_fast.h`
that supplies the inline differs.

## Behavior Notes

- **`count` semantics.** `nora_dma_channel_cfg_t.count` is written verbatim
  to `DMAxCNT`. On dsPIC33AK `DMAxCNT` is the number of elements (of the
  configured `size`) to transfer per repeat — it is **not** an "elements − 1"
  register. Pass the element count of one ping-pong half.
- **Invalid-channel convention** (channel index ≥ the device channel count):
  - `config` / `enable` return `false` and write nothing.
  - `void` IRQ/status helpers silently ignore the call.
  - read helpers return `0`.
- **ISR snapshot ordering.** `nora_dma_isr_snapshot()` (and its `_hot` inline)
  performs an *ordered*
  sequence (`_DMAxIF=0`, read `DMAxSTAT`, `DMAxSTAT=0`), not a single atomic
  instruction. The order is chosen so a `HALF` / `DONE` event raised mid-sequence
  stays latched rather than being lost; confirm the latching behavior against the
  device data sheet for the DMA modes you use.
- **Global arbitration policy.** `nora_dma_global_init()` sets
  `BMXINITPR.DMAPR=1` for the entire device. This prioritizes DMA over CPU X/Y
  SRAM accesses while leaving SFR arbitration unchanged. It prevents the
  observed CPU X/Y SRAM-starvation path that caused `DMAxSTAT.OVERRUN` under
  DSP-heavy audio workloads, but it is not a blanket guarantee against every
  possible overrun cause. Integrators must include every DMA consumer and
  CPU/DSP timing path in system-level regression tests.

## Migrating from the previous version

Beyond the textual `dspic33ak_` → `nora_` rename, two things need a real edit at
the call site. Both are visible at compile time — nothing changes meaning
silently.

| before | now |
|---|---|
| `.trigger_sel = <raw Table 13-2 trigger ID>` | `.trigger = NORA_DMA_TRIGGER_SPI1_RX` (and the other SPI1..SPI4 RX/TX values) |
| `NORA_DMA_STAT_HALF` / `_DONE` / `_OVERRUN` bit tests | `nora_dma_status_has_completed_half()` / `_has_half_done_conflict()` / `_has_overrun()` |

Also worth knowing, though neither needs an edit:

* The channel argument is `nora_dma_channel_t` (`NORA_DMA_CHANNEL_0..7`) instead
  of a bare `uint8_t`; an existing `0u` still compiles.
* `nora_dma_isr_snapshot()`, `_read_src()`, `_irq_disable_save()` and
  `_irq_restore()` used to be `static inline` in the public header and are now
  ordinary functions. If you relied on them inlining inside an ISR, include
  `nora_dma_dspic33ak_fast.h` and call the `_hot` names.
* `NORA_DMA_ADDR_WINDOW_LOW` / `_HIGH` are gone from the header. The backend
  still programs `DMALOW` / `DMAHIGH` in `nora_dma_global_init()`; the constants
  were simply not anyone's to read.

## Notes

- This repository does not include Microchip DFP header files.
- This HAL is the DMA layer only; clock setup and peripheral (SPI/PWM/...) setup
  belong to the board/application and the consuming HALs.
- This is the canonical DMA HAL used by
  [nora-hal-dspic33ak-spi-i2s-tdm](https://github.com/sulaolab/nora-hal-dspic33ak-spi-i2s-tdm)
  and vendored into the dsPIC33AK HAL starter and CMSIS-Driver SAI repositories.

## License

MIT No Attribution License (MIT-0). See [LICENSE](LICENSE).

Attribution is appreciated but not required.
