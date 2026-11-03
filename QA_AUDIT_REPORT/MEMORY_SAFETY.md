# Memory Safety Audit

## Tool status

| Tool | Status |
|---|---|
| Vulkan Validation | RUN (0 errors on Windows smoke/resize/stress after fixes) |
| `spirv-val` | RUN (26/26 valid) |
| AddressSanitizer | **NOT RUN** — MSYS gcc has no `-lasan` runtime |
| UndefinedBehaviorSanitizer | **NOT RUN** — no `-lubsan` runtime |
| ThreadSanitizer | **NOT RUN** |
| RenderDoc | **NOT RUN** — not installed/launched |
| GPU-assisted validation | **NOT RUN** |

No sanitizer-clean claim is made.

## Confirmed and fixed

### QA-004 — five unchecked `vkMapMemory`

Locations in `src/vgraphics.c`:

1. texture upload staging;
2. mesh vertex upload;
3. mesh index upload;
4. render-target readback;
5. line-buffer upload.

Previously `vkMapMemory` result was ignored and `memcpy` used the result pointer;
on `VK_ERROR_MEMORY_MAP_FAILED`/OOM this is NULL dereference/corruption. Each now
checks `VkResult`, cleans local staging buffer/memory and returns failure.

### QA-001 — image layout lifecycle

Debug names identified `ssao_depth`, `ssao_color`, `velocity_color`; initial
transitions and render-pass final layouts were fixed. Full-draw stress now
`validation=0`.

## Static scan

- `strcpy/strcat/sprintf`: only `strcpy(path, "/")`, literal source, safe.
- Public handle lookups use generation counters; stale texture/mesh/material IDs
  are rejected.
- NULL file/scene/animator paths covered by API contract tests.
- No demonstrated buffer overflow or use-after-free in the audited paths.

## Lifecycle / leaks

- `CloseGraphicsDevice` waits idle, releases deferred textures/meshes/materials,
  destroys shadow/pipeline/descriptor resources; validation after shutdown = 0.
- Repeated `api_smoke` process runs (5x) exited 0.
- No OS-level RAM/VRAM leak measurement (no allocator counters/Valgrind/ASan).

## Verdict

The five map paths were a real OOM crash/corruption risk and are fixed. The
project cannot be called memory-safety-certified without ASan/UBSan and
GPU-capture tooling.
