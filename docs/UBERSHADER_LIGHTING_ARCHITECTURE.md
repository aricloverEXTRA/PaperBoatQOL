# PaperBoat Ubershader / Compute Lighting Architecture

## Goal
Native GPU-side lighting replacing CPU-side stylized fill, supporting:
- Complex material lighting
- Point lights
- Shadows
- Sunlight (directional)
- SSAO
- Modern Paper Mario style replications

## Approach
- Single ubershader with compile-time + runtime permutations (no runtime shader compile stutter).
- Compute passes for lighting accumulation (point/clustered lights, SSAO) feeding the ubershader.
- CPU only updates light lists / params; GPU does heavy lifting.

## Pipeline
1. G-buffer pass (position/normal/albedo) for SSAO + lighting.
2. SSAO compute pass.
3. Shadow map pass (sunlight directional + point cubemaps).
4. Lighting compute pass (clustered point lights + sunlight + ambient).
5. Ubershader composite (materials + lighting + stylized toon).

## Permutation flags (compile-time)
- LIGHTING_MODE: NONE / FLAT / TOON / PBR-lite
- HAS_POINT_LIGHTS
- HAS_SHADOWS
- HAS_SSAO
- HAS_SUNLIGHT
- STYLIZED_FILL

## Stability rules
- Null-check RAPI, shader handles, buffers before use.
- Clamp light counts; validate indices.
- Fallback to CPU stylized path if GPU compute unavailable.
- No dynamic shader compilation at runtime; all permutations precompiled.

## Crash-safety checklist
- Guard every pointer deref in render path.
- Bounds-check vertex/index buffers.
- Validate light list sizes before upload.
- Graceful degradation on missing extensions.