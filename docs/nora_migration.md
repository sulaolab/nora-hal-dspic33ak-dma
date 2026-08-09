# NORA migration — where these sources come from (2026-08-09)

This repository used to be the place the DMA HAL was edited. It is not any more.
Since the NORA migration it is a **published snapshot**: `src/` is filled from
the tree that is actually built and run on hardware, and this file records which
tree, which commit, and how the equality was checked.

**For this module the migration is not only a rename.** The upstream tree changed
the public contract while this repository sat still, and the refresh brings all of
it across at once. The API differences are listed below and in the README's
*Migrating from the previous version* section.

## The chain

```
dsp-sonora audio board project        the tree that runs on hardware
  main = 91adb63
        |  vendored, byte-for-byte
        v
sulaolab/dspic33ak-hal-starter        MPLAB X project, 11 HAL modules
  refactor/nora-hal = b70982d
        |  published, byte-for-byte
        v
sulaolab/nora-hal-dspic33ak-dma       this repository
```

Direction matters: it used to run the other way (the starter vendored *from* the
standalone repos). It was reversed because only the board project exercises the
code on silicon, so it is the only place a fix can be validated before it is
published. A fix made here and not upstream would be a fork.

## What the two commits did

| commit | what |
|---|---|
| `76f8fe1` | **rename only** — 3 files, all detected as R100 (100 % similarity). No byte of content changed, so the rename is reviewable on its own. |
| `0f778e3` | **content refresh** — the 3 files replaced with the starter's bytes, plus one new file. |

### The rename mapping

| before | after | why |
|---|---|---|
| `src/dspic33ak_dma.h` | `src/nora_dma.h` | public header: no chip in the name |
| `src/dspic33ak_dma.c` | `src/nora_dma_dspic33ak.c` | backend: tagged |
| `src/dspic33ak_dma_reg.h` | `src/nora_dma_dspic33ak_reg.h` | backend-private register layer: tagged |
| *(new)* | `src/nora_dma_dspic33ak_fast.h` | backend-private ISR fast path |

The tag is `_dspic33ak`, not `_dspic33a`. `dsPIC33A` is the *core* family name;
these files drive dsPIC33AK DMA SFRs. A dsPIC33CK backend would be `_dspic33ck` —
a different silicon family (dsPIC33**C**), never shortened to `_dspic33c`.

## Proof of identity with the upstream tree

Git blob hashes, this repository at `0f778e3` vs `dspic33ak-hal-starter` at
`b70982d`. Identical hash = identical bytes; git normalises EOLs into the blob on
both sides, so the CRLF working trees do not disturb the comparison.

| file | blob | bytes |
|---|---|---|
| `nora_dma.h` | `6b713df0670b` | 10500 |
| `nora_dma_dspic33ak.c` | `c0397e8e9302` | 16367 |
| `nora_dma_dspic33ak_fast.h` | `080bda8e40e6` | 5976 |
| `nora_dma_dspic33ak_reg.h` | `0b3d5f88b821` | 5562 |

**4 of 4 identical.**

## What actually changed in the content refresh

Method: take each new file, reverse the naming (`nora_` → `dspic33ak_`,
`NORA_` → `DSPIC33AK_`, and strip the `_dspic33ak` backend tag from the file
names), and diff against the pre-rename blob. Whatever is left is *not* naming.

| file | +added | −removed | non-comment | verdict |
|---|---|---|---|---|
| `nora_dma.h` | 127 | 179 | +61 / −121 | real change |
| `nora_dma_dspic33ak.c` | 165 | 60 | +143 / −56 | real change |
| `nora_dma_dspic33ak_reg.h` | 48 | 14 | +26 / −13 | real change |
| `nora_dma_dspic33ak_fast.h` | — | — | — | new file |

**0 of 3 pure rename, 1 new file.** This is the module with the largest real
delta, and the direction of the header numbers is the point: `nora_dma.h` *lost*
about twice as many non-comment lines as it gained. The public header got smaller.

### API delta

Measured on the names the public header exposes: **15 → 17 functions, 19 → 30
macros/enumerators.** Nothing was removed from the function set; the two additions
are `nora_dma_status_has_completed_half()` and `nora_dma_status_has_overrun()`.

### What the delta consists of

Three deliberate changes, all upstream decisions this snapshot inherits:

1. **The SFR-touching inlines left the portable header.** `nora_dma.h` used to
   define `static inline` functions that read DMA registers, which forced every
   consumer of the portable API to have `<xc.h>` and the DFP in scope. They are
   now out-of-line in the backend `.c` (that is most of the header's −121 and the
   implementation's +143), and the ISR-speed versions moved to
   `nora_dma_dspic33ak_fast.h` as `_hot` inlines. A call site that needs cycle
   counts includes the `_fast.h` header explicitly and calls
   `nora_dma_isr_snapshot_hot()`; one that does not keeps calling
   `nora_dma_isr_snapshot()` and pays a call.
2. **Raw hardware IDs and bit masks became enums and accessors.** Channels are
   `nora_dma_channel_t` rather than plain integers, `config.trigger_sel` (a raw
   trigger number) became `config.trigger` (`NORA_DMA_TRIGGER_*`), and the
   `NORA_DMA_STAT_*` bit tests became `nora_dma_status_has_*()` predicates. That
   is where the +30 macros/enumerators come from.
3. **`ADDR_WINDOW_LOW` / `ADDR_WINDOW_HIGH` were removed** from the public
   header.

None of this is discoverable from the rename; all of it breaks call sites, so the
README carries a migration table with the old and new spelling side by side.

## Comment corrections made here, ahead of upstream

Everything above describes the state as published on 2026-08-08, when every file under
`src/` was byte-identical to upstream. On **2026-08-09** a documentation review found a
class of error that the identity proof above cannot see, and it was fixed here first
rather than waiting for the next upstream refresh.

* `src/nora_dma.h`, `src/nora_dma_dspic33ak.c`, `src/nora_dma_dspic33ak_fast.h` — five
  comments said `dsPIC33A` where they mean the dsPIC33AK backend, and seven wrote the
  HAL family name as `Nora` rather than `NORA`.

No executable code changed. The edits are comments and Markdown; the compiled
result is unchanged.

### Why the proof in "Proof of identity" does not catch this

Step 3 reverse-normalises the NORA names back to `dspic33ak_*` and diffs against the
pre-rename blob, so whatever is left is not naming. Two error classes cancel out exactly
in that diff and are therefore invisible to it:

* **A document reference to a file that was renamed.** A prose mention of
  `nora_<mod>_hw.{c,h}` reverse-normalises to `dspic33ak_<mod>_hw.{c,h}`, which is the
  *correct* pre-rename name — the diff is empty, yet the file is now called
  `nora_<mod>_dspic33ak_hw.{c,h}` and the reference is dead. The same cancellation hides
  `Nora` vs `NORA` and `dsPIC33A` vs `dsPIC33AK`: both sides of the diff are naming, so
  naming errors are exactly what it is blind to.
* **A document that omits a file the refresh added.** An absent line produces no diff
  line at all.

Both are real here. Neither is detectable by reverse-normalisation; both are detectable
by resolving every `nora_*.{c,h}` mentioned in prose against the actual contents of
`src/`, which is now how they were found.

## Hardware evidence

There is no build or test in this repository — it is sources only. The evidence is
the upstream project's: `dspic33ak-hal-starter`
`docs/nora_hal_migration_analysis.md` §11e records a PASS run of all 11 NORA-ised
modules on PKOB4 `020085204RYN000057` (dsPIC33AK512MPS512, Device ID `0xa77c`) on
2026-08-09.

Scope, stated plainly: DMA is exercised there under the TDM8 audio path — the
ping-pong channels that feed and drain the codec stream, which is also where the
`_hot` inlines matter, since they run inside the audio ISR. There is no standalone
DMA unit test in that run. Unlike the pure-rename modules, this module's bytes
are **not** the ones this repository published before, so the §11e run is the only
evidence for the new contract, and it is integration evidence.

## Consumer impact

* The public namespace changed from `dspic33ak_*` / `DSPIC33AK_*` to `nora_*` /
  `NORA_*` and **no compatibility aliases were added**.
* The `#include` names changed — see the rename mapping above.
* **The API changed.** A textual namespace substitution is not sufficient for this
  module: channel arguments, the trigger field, and the status bit tests all need
  hand edits. See *Migrating from the previous version* in the README.
