# 07 — Security & Memory Audit

## Tools actually used

| Tool | Status | Result |
|---|---|---|
| `VK_LAYER_KHRONOS_validation` | RUN | main smoke, resize, empty and 1000-object stress clean after QA-001 fix |
| `spirv-val` | RUN | 26/26 valid |
| AddressSanitizer | **NOT RUN** | MSYS gcc has no `-lasan` runtime (`cannot find -lasan`) |
| UndefinedBehaviorSanitizer | **NOT RUN** | no `-lubsan` runtime |
| ThreadSanitizer | **NOT RUN** | not available / API not thread-safe |
| RenderDoc | **NOT RUN** | not installed/launched |
| GPU-assisted validation | **NOT RUN** | not enabled |
| Synchronization validation | **NOT RUN** | layer feature not enabled |

No claim of sanitizer cleanliness is made.

## Static scan

- `strcpy/strcat/sprintf`: one `strcpy(path, "/")` with literal source — safe.
- Public texture/mesh/material handles use generation counters; stale IDs are
  rejected in the audited lookup paths.
- `LoadFileData`/`LoadFileText`/save/file APIs reject NULL paths (contract test).
- `SceneAddNode` clamps invalid parent to -1 and defaults scale to 1.
- No obvious `%s` format-string vulnerabilities found in the audited modules.

## Confirmed memory defect and fix

Five upload paths called `vkMapMemory` without checking `VkResult` and then
`memcpy`'d into the result. On OOM, `mapped` would be NULL → crash/corruption.

Fixed in `src/vgraphics.c`:

1. texture upload staging (`~line 813`)
2. mesh vertex upload (`~line 1447`)
3. mesh index upload (`~line 1455`)
4. render-target readback staging (`~line 2141`)
5. line-buffer upload (`~line 3084`)

Each now checks the result, destroys the staging buffer/memory where ownership is
local, and returns failure. See QA-005 in `08_BUG_TRACKER.md`.

## Lifecycle observations

- `CloseGraphicsDevice` calls `vkDeviceWaitIdle` then destroys shadow/pipeline/
  descriptor resources; validation reports 0 errors after shutdown in the smoke.
- Deferred release tables exist for textures/meshes/materials.
- Repeated `api_smoke` process runs (5x) all exited 0; no OS-level leak
  measurement was performed.
- No device-lost/OOM recovery (QA-008).

## Security verdict

No exploitable memory-safety issue was demonstrated. The unchecked map paths
were a real OOM crash/corruption risk and are fixed. Without ASan/TSan/RenderDoc,
the project cannot be called memory-safety-certified.
