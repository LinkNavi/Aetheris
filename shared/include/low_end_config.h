#pragma once

// ── Low-end hardware profile (Intel HD 3000, ~3.7GB RAM, Sandy Bridge i5) ────
// Drop this into shared/include/ and include it from config.h

namespace LowEnd {
    // Detect at runtime — call once in main()
    inline bool detected = false;

    // Tuning knobs toggled on detection
    inline bool  disableGodrays      = true;   // ~40% GPU time saved
    inline bool  disableAurora       = true;   // shader branch savings
    inline bool  lowDetailClouds     = true;   // halve cloud FBM octaves
    inline bool  disableLeafAlpha    = false;  // keep leaves but simpler
    inline int   maxMeshesPerFrame   = 2;      // was 4 — reduce stutter
    inline int   statsFlushHz        = 5;      // was 10Hz — halve net load
    inline float defaultRenderDistXZ = 2.f;   // was user-set, force low default
    inline int   threadPoolSize      = 1;      // only 2 real cores, keep 1 for main
}
