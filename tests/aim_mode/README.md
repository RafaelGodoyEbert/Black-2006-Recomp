# Aim mode regression

This asset-free Windows fixture extracts the shipping `BLACK_PC_AIM` wrapper
from `src/recomp_manual.c`. It verifies that Toggle leaves native aim state
untouched, Hold follows decoded levels and remapped controls, invalid memory
is skipped, and emulated registers and floating-point state reach the original
function unchanged.

The generated original is represented by a small toggle/latch stub. This test
does not establish actual weapon, scope or animation behavior. The production
input/settings tests in `tests/ambient_occlusion` cover held Aim across camera
gaps, release, pause, focus loss, defaults, persistence and native menu choices.
Launcher settings and page persistence are covered by `launcher/tests`.

```powershell
cmake -S tests/aim_mode -B build-aim-mode -A x64
cmake --build build-aim-mode --config Release
ctest --test-dir build-aim-mode -C Release --output-on-failure
```
