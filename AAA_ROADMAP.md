# vlcrlib → AAA: аудит и дорожная карта

Дата аудита: 2026-10-25  
Статус: технический прототип / indie-уровень с сильной PBR-базой, **не AAA**.

> AAA — это не только движок. Это команда, производственный бюджет,
> контент-пайплайн, инструменты, консоли, сертификация и годы работы.
> Ниже — путь к **AAA-готовой технологии** и вертикальному слайсу,
> а не обещание сделать AAA в одиночку.

---

## 1. Что уже есть

| Область | Текущее состояние |
|---|---|
| Платформа | Windows, Vulkan 1.4, SDL3, C API |
| Рендер | Forward PBR metallic-roughness, GGX, IBL (irradiance/prefilter/BRDF LUT), directional/spot/point shadows, SSAO, bloom, ACES, fog, atmosphere, skybox, transmission/clearcoat/sheen/anisotropy/iridescence в материале |
| Освещение | Directional, point, spot, area, ambient; SSBO-массив до 64 источников |
| Ассеты | glTF/GLB (tinygltf), OBJ (Assimp/fallback), FBX (Assimp, опционально), USD (OpenUSD, stub) |
| Анимация | Скелет, один слой, линейные ключи, клипы glTF/Assimp, bind pose |
| Физика | Bullet 3D, Box2D, Jolt (опционально), персонажи, машины, cloth, destruction-примеры |
| Аудио | VLC: Sound/Music, без 3D-микшера |
| GUI | Собственный VGui: окна, кнопки, списки, таблицы, node graph |
| Инструменты | Примеры, cheat sheet, нет редактора и asset cooker |
| Тесты | 6 CTest-тестов (math/UBO/flags/coords/animator/render_graph), 7 GPU smoke-примеров |

### Метрики кодовой базы

```text
src:      60 файлов, ~23 300 строк C/C++, ~1.08 MB
examples: 62 файла, ~385 KB
shaders:  23 файла, ~70 KB
```

Крупнейшие файлы:

```text
vgraphics.c           5 494 строк
vfastgltf.c           2 071
vgui.c                1 986
vphysics.cpp          1 033
vphysics2d.cpp        1 028
vfastgltf_lib.cpp       953   ← вероятно, мёртвый дубль
vgraphics_pipeline.c    821
vgltf.cpp               649
```

### Результаты проверки (2026-10-25)

```text
CTest:                  6/6 passed (math, ubo, flags, coords, animator, render_graph)
VLCRLIB_STRICT:         build OK после фикса C4005 в vgltf.cpp
model_animation_viewer: build OK
api_smoke:              fails=0, frames=90
example_test_gltf:      fails=0
```

Найденные проблемы в аудите:

- `VLCRLIB_STRICT` падал на повторном `#define WIN32_LEAN_AND_MEAN` — исправлено.
- `vfastgltf_lib.cpp` (~953 строки) не входит в CMake, но остаётся в дереве.
- `vfastgltf.c` (~2071 строка) и tinygltf-путь сосуществуют; документация местами историческая.
- Тесты теперь включены по умолчанию (`VLCRLIB_BUILD_TESTS=ON`).
- Нет CI и golden-image тестов.

### Что дал validation-layer прогон

- Исправлен skin vertex shader: не было `vColor` (location 4), который ожидает `pbr.frag`.
- 3D unlit pipeline больше не объявляет неиспользуемые tangent/color attributes.
- GPU timestamp queries сбрасываются через `vkCmdResetQueryPool`, а не host-reset без feature.
- 2D descriptor sets разделены по frame slots — исчез race `vkUpdateDescriptorSets` с in-flight командами.
- Уничтожаются SSAO/bloom/scene/composite images, framebuffers, render passes, pipelines, DSL и pools.
- Итог: `VLCRLIB_VALIDATION=1` → **0 validation errors / 0 warnings**.

### Сильные стороны

- Понятный raylib-style C API — низкий порог входа.
- Реально работающий Vulkan PBR, а не «hello triangle».
- Хорошая математическая база и контракт координат.
- Много примеров, которые можно превратить в тесты.
- Материалы уже поддерживают больше glTF-расширений, чем типичный indie-движок.
- Есть GUI, физика, загрузчики — можно быстро собрать вертикальный слайс.

### Критические проблемы

1. **Нет render graph / frame graph** — проходы вручную, барьеры и ресурсы размазаны по `vgraphics.c`.
2. **Нет TAA, motion vectors, upscaling** — картинка не AAA-уровня на движении.
3. **Нет cascaded shadow maps** — одна directional-тень на большую сцену.
4. **Нет GPU instancing / indirect / culling** — CPU-цикл по объектам.
5. **Нет cook-пайплайна**: KTX2/BC7/ASTC, Draco/meshopt, LOD, streaming.
6. **Анимация слишком простая**: нет blend tree, state machine, root motion, IK, events, morph targets.
7. **Нет редактора/сериализации сцены** — контент-пайплайн ручной.
8. **Аудио не 3D** — нет spatialization, mixer buses, DSP, occlusion.
9. **Тесты и CI слабые** — нет golden-image, validation-layer CI, fuzz.
10. **Монолитный `vgraphics.c`** и глобальное состояние мешают масштабированию и многопоточности.
11. **Дубли загрузчиков** (`vfastgltf.c`, `vfastgltf_lib.cpp`, исторические `cgltf_*` имена).
12. **OpenUSD — stub**, USDZ не читается без OpenUSD.
13. **Нет HDR swapchain / color management** (HDR-мониторы, wide gamut).
14. **Нет платформенного слоя для консолей/мобилок**, только desktop Vulkan + WebGPU-ветка.

---

## 2. Оценка зрелости

| Домен | Оценка /5 | Комментарий |
|---|---:|---|
| Математика / ядро | 4 | Прочно, покрыто тестами |
| Рендеринг | 3 | Хороший forward PBR, нет temporal/upscale/SSR/volumetrics |
| Ассеты | 2 | Загрузка есть, cook/streaming/compression нет |
| Анимация | 2 | Клипы есть, production-графа нет |
| Физика | 3 | Обёртки есть, интеграции и ragdoll нет |
| Аудио | 1 | Только воспроизведение файлов |
| GUI | 3 | Богатый набор виджетов, нет editor UX |
| Инструменты | 1 | Нет редактора, asset browser, undo |
| Производительность | 2 | Нет profiler, job system, GPU-driven |
| Тестирование | 2 | 6 unit-тестов, CI, validation layer; нет golden |
| Документация | 3 | Cheat sheet, model loaders; нет architecture guide |
| Платформы | 2 | Windows desktop; WebGPU частично |

**Итог: 2.3 / 5.** Это хороший фундамент, но не AAA.

---

## 3. Целевая архитектура

```text
┌─────────────────────────────────────────────────────────────┐
│ tools: editor, asset cooker, scene validator, profiler      │
├─────────────────────────────────────────────────────────────┤
│ game / samples: gameplay, UI, scripting                     │
├─────────────────────────────────────────────────────────────┤
│ scene: world, components, serialization, streaming          │
│ animation: graph, state machine, IK, events, root motion    │
│ audio: mixer, 3D buses, DSP, streaming                      │
│ physics: Jolt primary, fixed step, ragdoll, destruction     │
├─────────────────────────────────────────────────────────────┤
│ renderer: frame graph, passes, materials, lighting, post    │
├─────────────────────────────────────────────────────────────┤
│ rhi: Vulkan 1.4, WebGPU, future platforms                   │
├─────────────────────────────────────────────────────────────┤
│ core: platform, math, memory, jobs, containers, logging     │
└─────────────────────────────────────────────────────────────┘
```

### Правила

- Публичный **C ABI** оставить для примеров и обратной совместимости.
- Внутри — C++17/20, data-oriented подсистемы, без глобального `VK`-контекста.
- Все GPU-ресурсы — через **Render Graph** с автоматическими барьерами.
- Все ассеты — через **cooker**; рантайм не читает FBX/USD напрямую в продакшене.
- Один физический бэкенд в продакшене — **Jolt**; Bullet/Box2D оставить как legacy.
- Один аудиобэкенд — собственный миксер (miniaudio/WASAPI), VLC убрать из runtime.

---

## 4. Дорожная карта

### Phase 0 — Стабилизация (2–3 недели)

**Цель:** предсказуемая сборка, тесты, метрики, никаких «мертвых» загрузчиков.

- [x] Включить `VLCRLIB_BUILD_TESTS=ON` по умолчанию.
- [x] GitHub Actions workflow: configure + build + ctest.
- [x] Validation layers + debug messenger + `VLCRLIB_VALIDATION=1` smoke: **0 errors, 0 warnings**.
- [ ] Удалить/архивировать `vfastgltf_lib.cpp` и дублирующие заголовки.
- [ ] Переименовать исторические `cgltf_*` примеры.
- [x] Добавить `FrameStats`: CPU frame time, draw calls, triangles, shadow draws.
- [x] GPU timestamps (`gpuFrameMs`), per-pass `shadowMs/sceneMs/postMs`, оценка VRAM.
- [x] PNG screenshot capture (`vgraphics_CaptureScreenshot`).
- [x] Golden-image differ (`vdiff`), skinned+TAA golden test, `perf_probe` JSON.
- [ ] Зафиксировать бюджеты: 1080p60 на RTX 3060 / RX 6600.
- [x] Добавить [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) и [docs/ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md).

**Exit:** CI зелёный, 0 warning-as-error в strict, smoke без validation errors.

---

### Phase 1 — Renderer foundation (6–8 недель)

**Цель:** картинка и производительность уровня AA.

1. **Render Graph**
   - [x] CPU-side graph MVP: images, passes, read/write uses;
   - [x] dependency ordering (read-after-write / write-after-read / write-after-write);
   - [x] automatic layout/access/stage barrier plan;
   - [x] final-layout transitions;
   - [x] unit tests for barriers, imported images, read-before-write rejection;
   - [x] transient Vulkan image allocation + views;
   - [x] `vrgExecute()` with per-pass callbacks and barrier emission;
   - [x] composite + bloom + SSAO passes migrated via external-image imports;
- [x] TAA: jitter, model + skinned velocity buffer, depth fallback, history clamp;
   - [x] `render_graph_example` prints pass order and barrier plan;
   - [ ] pass culling / resource aliasing;
   - [ ] debug visualization;
   - [ ] migrate SSAO/bloom/composite to the graph.

2. **HDR / color**
   - linear HDR scene target (RGBA16F);
   - ACES + grading (LUT);
   - HDR swapchain (`VK_EXT_swapchain_colorspace`);
   - sRGB-correct UI.

3. **Shadows**
   - [x] 4-cascade directional CSM in a 2x2 atlas (validation clean);
   - [ ] stable fit / texel snapping per cascade;
   - cache static shadow casters;
   - contact shadows;
   - улучшить PCF/PCSS.

4. **Geometry**
   - [x] CPU frustum culling with per-mesh AABBs + `culledDraws` stat;
   - [ ] GPU instancing;
   - [ ] indirect draw;
   - [ ] occlusion culling;
   - [ ] LOD groups;
   - [ ] meshlet-путь (позже).

5. **Profiling**
   - [x] GPU frame time;
   - [x] shadow/scene/post split;
   - [ ] per-pass graph timings;
   - [ ] GPU memory breakdown by resource.

6. **Temporal**
   - motion vectors (camera + skinned);
   - TAA;
   - FSR2/DLSS/XeSS как плагины;
   - jitter + history reprojection.

7. **Post**
   - [x] FXAA in composite;
   - [ ] SSR;
   - [ ] SSAO → GTAO;
   - [ ] bloom mip-chain;
   - [ ] DOF, motion blur, vignette, chromatic aberration.

**Exit:** 1080p60 на средней GPU, TAA без ghosting, CSM на 200 м, draw calls -70%.

---

### Phase 2 — Content pipeline (6–8 недель)

**Цель:** ассеты готовятся офлайн, а не парсятся в рантайме.

- [x] `vlcrc` asset validator/manifest: glTF + FBX/OBJ counts, bounds, warnings;
- [x] `.vmesh` v2/v3 cooking (multi-mesh, materials, KTX2 refs) + runtime `LoadCookedModel`;
- [ ] `vlcrc` full cooker:
  - glTF/FBX/USD → engine binary;
  - meshoptimizer: vertex cache, overdraw, simplification;
  - LOD generation;
  - [x] KTX2 + BC7/BC5 cooker and runtime;
  - [x] KTX2 mip generation;
  - [ ] BC6H/ASTC;
  - texture arrays/cubemaps;
  - material graph → flat material.
- [ ] Runtime: async load, streaming, ref-counted assets.
- [ ] Hot reload материалов и текстур.
- [ ] Asset database с GUID/hash.
- [ ] Валидатор: UV, нормали, тангенты, масштаб, missing textures.
- [ ] USD: только offline-конвертация; OpenUSD — инструмент, не runtime.

**Exit:** 2 ГБ сцена грузится < 3 с, streaming < 16 мс, память текстур -60%.

---

### Phase 3 — Animation 2.0 (6–8 недель)

- [ ] Skeleton runtime: bind pose, retargeting, bone masks.
- [x] Two-clip matrix crossfade (`DrawSkinnedModelBlend`).
- [x] Named-state animator with timed crossfade (`vanimator.h`).
- [x] Synthetic two-bone `assets/skinned_anim.glb` + `animator_example` with 0 validation errors.
- [x] `Animator` integrated into `model_animation_viewer` (Idle/Walk crossfade).
- [ ] Animation graph: 1D/2D blend, layers, additive.
- [ ] State machine: idle/walk/run/jump, transitions, events.
- [ ] Root motion + motion warping.
- [ ] IK: two-bone, look-at, foot placement.
- [ ] Morph targets + facial animation.
- [ ] Animation compression (key reduction, quantization).
- [x] 256-bone skeleton support (Walking.fbx has 65/mesh, 130 total).
- [x] Root motion extraction (`GetAnimationRootMotion`, `AnimatorGetRootMotion`);
- [x] Parameter transitions (float/bool/trigger) in Animator;
- [x] Overlay/additive layers + pose API + two-bone IK on skeleton chains;
- [x] Animation events (`AnimatorAddEvent` / `AnimatorNextEvent`);
- [x] JSON scene serialization + `SceneLoadModels`;
- [x] Scene editor-lite example (`scene_editor_lite`) + hierarchy + undo/redo;
- [x] 3D audio: occlusion, FIR convolution reverb, streaming, seek/pause;
- [ ] GPU skinning 8 bones/vertex, dual-quaternion вариант.
- [ ] Animation editor-lite в VGui.

**Exit:** персонаж с idle/walk/run/jump, foot IK, events, без popping.

---

### Phase 4 — Audio / Physics / Tools (8–12 недель)

**Audio**
- [ ] miniaudio/WASAPI backend.
- [ ] 3D spatialization, attenuation, cones, Doppler.
- [ ] Mixer buses, snapshots, ducking.
- [ ] DSP: reverb, EQ, compressor, limiter.
- [ ] Streaming, compressed formats, voice limits.
- [ ] Occlusion/obstruction через physics raycast.

**Physics**
- [ ] Jolt primary; fixed timestep + interpolation.
- [ ] Ragdolls, joints, motors, destructibles.
- [ ] Character controller: slopes, steps, moving platforms.
- [ ] Physics-to-animation sync.

**Tools**
- [ ] Scene serialization (JSON/binary).
- [ ] Editor: hierarchy, inspector, asset browser, gizmos, undo.
- [ ] Play-in-editor, prefabs, lighting bake.
- [ ] Profiler: CPU/GPU timeline, memory, draw calls.

**Exit:** сцена собирается в редакторе и запускается без кода.

---

### Phase 5 — Scale / AAA production (6–12+ месяцев)

- Job system, parallel culling/animation/physics.
- Open-world streaming, HLOD, occlusion.
- GPU-driven renderer, meshlets, bindless.
- Console/mobile RHI, platform certification.
- Build farm, localization, accessibility, telemetry.
- AAA team: 80–150+ человек, 3–5 лет, бюджет $100M+.

---

## 5. Первые 30 / 60 / 90 дней

### 30 дней

1. CI + CTest + validation.
2. FrameStats и budgets.
3. Удалить мёртвые загрузчики, обновить docs.
4. Render Graph MVP (только texture passes).
5. CSM prototype.

### 60 дней

1. Render Graph для всех проходов.
2. HDR + grading.
3. GPU instancing + indirect.
4. KTX2/BC7/BC5 cooker MVP — done via `vlcrc --ktx [--ktx-bc5]`.
5. TAA с motion vectors.

#### Следующий крупный milestone

Детальный план: [docs/MILESTONE_RENDERING.md](docs/MILESTONE_RENDERING.md).

- TAA velocity MRT + previous transforms;
- SSAO/Bloom passes fully owned by Render Graph;
- convolution reverb IR node + zones.

## 90 дней

1. TAA + FSR2.
2. Animation graph MVP.
3. Asset streaming.
4. Editor-lite: hierarchy + inspector.
5. Vertical slice: 1 уровень, 1 персонаж, 3 материала, 60 fps.

---

## 6. Приоритетный backlog

| # | Задача | Impact | Effort | Зависимости |
|---|---|---:|---:|---|
| 1 | CI + CTest + validation | Высокий | S | — |
| 2 | FrameStats/profiler | Высокий | S | — |
| 3 | Render Graph | Очень высокий | L | — |
| 4 | CSM 4 cascades | Высокий | M | Render Graph |
| 5 | GPU instancing + indirect | Очень высокий | M | Render Graph |
| 6 | KTX2/BC7 cooker | Очень высокий | L | Asset DB |
| 7 | TAA + motion vectors | Очень высокий | L | HDR, velocity |
| 8 | Animation graph | Высокий | L | Skeleton runtime |
| 9 | 3D audio mixer | Высокий | M | — |
| 10 | Scene serialization | Высокий | M | — |
| 11 | Editor-lite | Очень высокий | L | Serialization, GUI |
| 12 | Jolt production integration | Высокий | M | — |
| 13 | Async asset streaming | Высокий | L | Cooker |
| 14 | GTAO/SSR | Средний | M | Render Graph |
| 15 | Meshlet/GPU culling | Очень высокий | XL | Render Graph |
| 16 | HDR output | Средний | M | Swapchain |
| 17 | USD offline pipeline | Средний | M | Cooker |
| 18 | Golden-image tests | Высокий | M | CI |
| 19 | Memory budgets/allocators | Высокий | M | — |
| 20 | Console RHI | Очень высокий | XL | Team |

---

## 7. Бюджеты производительности

| Параметр | Target |
|---|---|
| Resolution | 1920×1080 |
| GPU | RTX 3060 / RX 6600 |
| Frame | 16.6 ms |
| Shadows | ≤ 2.5 ms |
| Scene | ≤ 7 ms |
| Post | ≤ 2.5 ms |
| UI | ≤ 0.5 ms |
| Draw calls | ≤ 2 000 (later GPU-driven) |
| Triangles | 2–5M visible |
| VRAM | ≤ 6 GB |
| Load | 100 MB < 2 s |
| Streaming hitch | < 16 ms |

---

## 8. Тестирование и качество

- Unit: math, UBO layout, flags, coords — уже есть.
- Integration: glTF/FBX/OBJ loaders, animation sampling.
- GPU smoke: все примеры с `--frames N --exit-after`.
- Golden images: PBR, shadows, IBL, SSAO, bloom, TAA.
- Validation layers: debug CI, fail on error.
- Fuzz: glTF/GLB, image decode.
- Performance regression: frame time budget.
- Crash reporting: minidump + callstack.

---

## 9. Риски

| Риск | Митигация |
|---|---|
| «AAA» без команды/бюджета | Цель — vertical slice и AA-технология; не обещать AAA |
| Переписывание с нуля | Эволюционная замена: Render Graph → RHI → scene |
| Слишком много форматов | FBX/USD только offline; runtime — cooked binary |
| Монолитный `vgraphics.c` | Выносить подсистемы, не ломая C API |
| Нет редактора | Editor-lite в первую очередь; иначе контент не масштабируется |
| Вендорные зависимости | Зафиксировать версии, лицензии, SBOM |
| Потеря производительности | Бюджеты и профайлер с Phase 0 |

---

## 10. Чего НЕ делать

- Не начинать с DLSS/FSR без TAA и motion vectors.
- Не добавлять ещё 10 форматов в runtime.
- Не делать «сначала редактор, потом рендер» — рендер-фундамент важнее.
- Не переписывать всё на ECS «потому что модно».
- Не называть проект AAA до вертикального слайса с 60 fps и полным циклом контента.
- Не тащить OpenUSD в runtime — только cooker.

---

## 11. Ближайший практический шаг

**Phase 0, неделя 1:**

1. Включить тесты и CI.
2. Добавить FrameStats.
3. Убрать мёртвый `vfastgltf_lib.cpp`.
4. Спроектировать Render Graph API.
5. Собрать benchmark-сцену: 1000 объектов, 4K текстуры, скинированный персонаж.

После этого можно начинать Phase 1 с Render Graph и CSM.
