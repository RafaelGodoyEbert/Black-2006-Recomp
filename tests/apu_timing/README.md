# Audio timing regression

This suite compiles the patched runtime's `apu_timing.h`. It checks QPC scaling
after long Windows uptime, exact accumulated time for 90 minutes of 256-sample
48 kHz blocks, retention of ordinary scheduler debt, and bounded recovery after
a longer interruption. It does not claim a 90-minute audible gameplay test.

```powershell
cmake -S tests/apu_timing -B build-apu-timing -A x64
cmake --build build-apu-timing --config Release
ctest --test-dir build-apu-timing -C Release --output-on-failure
```

Use `-DXBOXRECOMP_DIR=<patched-runtime>` for an alternate runtime checkout.
The audio CI workflow runs Release and Debug configurations.

The throttle regression compiles the production `throttle`, `mcpx_apu_read`
and `mcpx_apu_write` functions from `apu_core.c`, with `apu_lock.h`. An
always-overdue simulated DSP must allow ordinary register access each eight-frame epoch without a
pacing wait, including across deadline rebases. It also checks normal pacing,
no delay without contention, and control changes during the handoff. On Windows,
two real competing threads exercise register reads/writes while the producer
holds a critical section; both must finish before the bounded producer run
ends. No audio device or
retail game data is needed. Recursive access under an IRQ-style outer lock is
also checked on Windows. The existing audio-output suite covers the frame
loop's pause, exit and front-end rechecks after throttle returns.

For a negative control, set `-DAPU_TIMING_CORE_SOURCE=<baseline-apu_core.c>`:
the overdue and threaded cases fail against the pre-fix source. These are
lock-progress regressions, not a claim of successful gameplay on Linux/Wine.

The contention handoff uses a dedicated condition variable: callers announce
their lock attempt and signal after acquisition. The audio producer uses a
1ms timeout when a caller is pending and adds no wait otherwise. A simple unlock
plus `SwitchToThread` still starved the reader in the Wine 9.0 threaded test.
