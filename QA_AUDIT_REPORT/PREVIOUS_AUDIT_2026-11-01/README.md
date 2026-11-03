# QA Audit Report — vlcrlib

Independent QA/Vulkan/code audit, 2026-11-01.

Start with `00_EXECUTIVE_SUMMARY.md`, then `08_BUG_TRACKER.md` for defects.
All measurements in `BENCHMARKS/`; raw logs/screenshots in `TEST_RESULTS/`.

## Reports

- `00_EXECUTIVE_SUMMARY.md`
- `01_ARCHITECTURE_AUDIT.md`
- `02_VULKAN_AUDIT.md`
- `03_API_TEST_REPORT.md`
- `04_GRAPHICS_TEST_REPORT.md`
- `05_PERFORMANCE_REPORT.md`
- `06_STRESS_TEST_REPORT.md`
- `07_SECURITY_MEMORY_AUDIT.md`
- `08_BUG_TRACKER.md`
- `09_FIX_HISTORY.md`
- `10_AAA_READINESS.md`

## Headline results

- Desktop ctest: 11/11 passed.
- CPU ctest: 9/9 passed.
- Validation smoke (PBR/glTF/shadow, 90 frames): 0 errors.
- Resize/shadow-resize validation: 0 errors.
- SPIR-V: 26/26 valid.
- WebGPU golden: mean 0.
- Vulkan vs WebGPU: mean 1.5520, PASS.
- Stress 1k/10k/100k objects: no crash; 100k is CPU-bound (~340 ms).
- QA-001 fixed: SSAO depth/color and velocity layout initialization; empty and
  1000-object stress are validation-clean.
- ASan/UBSan/RenderDoc/GPU-assisted validation were not available and are not
  claimed.
