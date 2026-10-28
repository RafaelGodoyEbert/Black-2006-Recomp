# Physics sleep timing

This asset-free harness compiles the production wrapper for BLACK's native
physics initializer after its title-specific overrides. At 60/120/240 Hz, squared per-update movement and the
number of quiet updates must describe the same speed and settling duration
as the original 30 Hz clock. The original clock and unsupported rates retain
the native settings. Linear and angular per-update drag are adjusted to retain
the same one-second decay at each supported clock.

The regression includes falling motion with gravity and drag: the old limits
put the synthetic unsupported body to sleep at 120/240 Hz, while the scaled
limits keep it moving. It also checks constructor re-entry, untouched memory,
bounds checks and returned floating-point/register state. This is a solver
units regression; it does not replace a real-game corpse/collision check.

```powershell
cmake -S tests/physics_timing -B build-physics-timing
cmake --build build-physics-timing --config Release
ctest --test-dir build-physics-timing -C Release --output-on-failure
```
