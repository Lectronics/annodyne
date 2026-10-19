---
paths:
  - "src/core/render/**"
  - "src/core/synthesis/**"
  - "src/core/dsp/**"
  - "src/host/**"
---

# Real-time audio rules

Code that runs on the audio callback (`render_block`, plug-in `process`, anything they call) must meet these rules. Mark such functions with a `// RT-SAFE` comment above the declaration (and `[[clang::nonblocking]]` if the toolchain supports RealtimeSanitizer). Anything not marked is assumed not real-time safe and must not be called from the audio thread.

## Never on the audio thread
- Heap allocation or deallocation (including `std::vector` growth, `std::string`, `std::function` captures that allocate, `shared_ptr` copies that may free the last owner).
- Locks, condition variables, waiting on futures.
- File, network or console I/O, including logging.
- Exceptions, `dynamic_cast` in hot paths, unbounded loops.
- Calls into analysis or ML models.

## How to get data to the audio thread
- Heavy work (analysis, re-synthesis of edited notes) runs on background threads and writes into the `RenderCache`.
- The audio thread only **reads** finished cache blocks. Publish them with a lock-free single-producer/single-consumer queue or atomic pointer swap of an immutable block.
- If a block is not ready, output the best available fallback (original audio for that region, crossfaded) and request it at high priority. Never block.
- Parameter changes reach the audio thread through atomics or a lock-free queue, and are smoothed (ramps of 5–20 ms) to avoid zipper noise.

## Buffers
- Preallocate all scratch buffers in `prepare(sample_rate, max_block_size)`. Size them for the worst case.
- Process functions take `std::span<const float>` inputs and `std::span<float>` outputs.
- Handle any block size from 1 to `max_block_size`, and sample-rate changes via a fresh `prepare`.

## Verification
- Real-time code gets a test that runs it under RealtimeSanitizer (if available) or with an allocation-counting hook that fails on any allocation during `process`.
- Threading code gets a TSan test with concurrent edit, render and playback.
