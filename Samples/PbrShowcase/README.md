# PBR & Rendering Showcase Sample

Demonstrates UniVex Engine's physically-based rendering stack:
- **Forward+ Clustered Lighting** (`LightClusterGridUVE`, up to 256 dynamic lights)
- **Temporal Anti-Aliasing (TAA)** + **WBOIT** transparency
- **HDR Reflection Probes** with 2-probe blending & L2 Spherical Harmonics
- **Hybrid Global Illumination** (baked UV2 lightmap + L2 SH probe grid)
- **Visual Material Graph** (`.uvmatgraph` compiled to PBR GLSL)

Open `project.uvproj` in `EditorApp` to inspect and edit materials, reflection probes, and lighting live.
