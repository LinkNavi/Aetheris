#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <algorithm>
#include <cmath>
#include <imgui.h>

// ── Easing functions ──────────────────────────────────────────────────────────

enum class EaseType : uint8_t {
    Linear = 0,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutBack,
    EaseInBack,
    COUNT
};

static constexpr const char* EASE_NAMES[] = {
    "Linear", "EaseInQuad", "EaseOutQuad", "EaseInOutQuad",
    "EaseInCubic", "EaseOutCubic", "EaseInOutCubic",
    "EaseOutBack", "EaseInBack"
};

inline float applyEase(EaseType type, float t) {
    t = std::clamp(t, 0.f, 1.f);
    switch (type) {
    case EaseType::Linear:        return t;
    case EaseType::EaseInQuad:    return t * t;
    case EaseType::EaseOutQuad:   return t * (2.f - t);
    case EaseType::EaseInOutQuad: return t < .5f ? 2*t*t : -1+(4-2*t)*t;
    case EaseType::EaseInCubic:   return t * t * t;
    case EaseType::EaseOutCubic:  { float s = t-1; return s*s*s+1; }
    case EaseType::EaseInOutCubic:return t < .5f ? 4*t*t*t : (t-1)*(2*t-2)*(2*t-2)+1;
    case EaseType::EaseOutBack:   { constexpr float c1=1.70158f,c3=c1+1; return 1+c3*(t-1)*(t-1)*(t-1)+c1*(t-1)*(t-1); }
    case EaseType::EaseInBack:    { constexpr float c1=1.70158f,c3=c1+1; return c3*t*t*t-c1*t*t; }
    default:                      return t;
    }
}

// ── Keyframe ──────────────────────────────────────────────────────────────────

struct AnimKeyframe {
    float     time     = 0.f;         // seconds from animation start
    glm::vec3 offset   = {0,0,0};     // delta from base transform offset
    glm::vec3 rotation = {0,0,0};     // delta from base transform rotation
    glm::vec3 scale    = {1,1,1};     // multiplier on base transform scale
    EaseType  easeIn   = EaseType::EaseOutCubic; // easing toward this keyframe
    std::string label  = "";
};

// ── Animation clip ────────────────────────────────────────────────────────────

struct AnimClip {
    std::string              name     = "New Clip";
    std::vector<AnimKeyframe> keyframes;
    bool                     loop     = false;
    float                    duration = 0.f; // auto-computed from last keyframe

    float computeDuration() const {
        if (keyframes.empty()) return 0.f;
        float d = 0.f;
        for (const auto& k : keyframes) d = std::max(d, k.time);
        return d;
    }

    // Sample the clip at time t, returning offset/rotation/scale deltas
    void sample(float t, glm::vec3& outOffset, glm::vec3& outRot, glm::vec3& outScale) const {
        if (keyframes.empty()) {
            outOffset = {0,0,0}; outRot = {0,0,0}; outScale = {1,1,1};
            return;
        }

        // Clamp or loop
        float dur = computeDuration();
        if (loop && dur > 0.f) t = std::fmod(t, dur);
        else                   t = std::clamp(t, 0.f, dur);

        // Find surrounding keyframes
        const AnimKeyframe* prev = &keyframes.front();
        const AnimKeyframe* next = &keyframes.front();

        for (size_t i = 0; i < keyframes.size(); i++) {
            if (keyframes[i].time <= t) prev = &keyframes[i];
            if (keyframes[i].time >= t) { next = &keyframes[i]; break; }
        }

        if (prev == next) {
            outOffset = prev->offset;
            outRot    = prev->rotation;
            outScale  = prev->scale;
            return;
        }

        float span = next->time - prev->time;
        float raw  = (span > 0.f) ? (t - prev->time) / span : 1.f;
        float alpha = applyEase(next->easeIn, raw);

        outOffset = glm::mix(prev->offset,   next->offset,   alpha);
        outRot    = glm::mix(prev->rotation, next->rotation, alpha);
        outScale  = glm::mix(prev->scale,    next->scale,    alpha);
    }
};

// ── Built-in default animations ───────────────────────────────────────────────

inline AnimClip makeDefaultLightAttack() {
    AnimClip c;
    c.name = "LightAttack";
    c.loop = false;
    // Keyframe 0: rest pose
    AnimKeyframe k0; k0.time = 0.f; k0.label = "Rest";
    k0.offset = {0,0,0}; k0.rotation = {0,0,0}; k0.scale = {1,1,1};
    k0.easeIn = EaseType::Linear;
    // Keyframe 1: wind-up (pull back + tilt)
    AnimKeyframe k1; k1.time = 0.08f; k1.label = "WindUp";
    k1.offset = {0.05f, 0.02f, 0.06f}; k1.rotation = {-15.f, -8.f, 5.f}; k1.scale = {1,1,1};
    k1.easeIn = EaseType::EaseInQuad;
    // Keyframe 2: strike (lunge forward + downward arc)
    AnimKeyframe k2; k2.time = 0.18f; k2.label = "Strike";
    k2.offset = {-0.04f, -0.06f, -0.12f}; k2.rotation = {25.f, 6.f, -8.f}; k2.scale = {1,1,1};
    k2.easeIn = EaseType::EaseOutBack;
    // Keyframe 3: follow-through
    AnimKeyframe k3; k3.time = 0.28f; k3.label = "FollowThrough";
    k3.offset = {-0.02f, -0.03f, -0.05f}; k3.rotation = {10.f, 2.f, -3.f}; k3.scale = {1,1,1};
    k3.easeIn = EaseType::EaseOutQuad;
    // Keyframe 4: recover to rest
    AnimKeyframe k4; k4.time = 0.55f; k4.label = "Recover";
    k4.offset = {0,0,0}; k4.rotation = {0,0,0}; k4.scale = {1,1,1};
    k4.easeIn = EaseType::EaseInOutCubic;

    c.keyframes = {k0, k1, k2, k3, k4};
    return c;
}

inline AnimClip makeDefaultHeavyAttack() {
    AnimClip c;
    c.name = "HeavyAttack";
    c.loop = false;

    AnimKeyframe k0; k0.time = 0.f;    k0.label = "Rest";
    k0.offset = {0,0,0}; k0.rotation = {0,0,0}; k0.scale = {1,1,1};
    k0.easeIn = EaseType::Linear;

    AnimKeyframe k1; k1.time = 0.15f;  k1.label = "BigWindUp";
    k1.offset = {0.08f, 0.08f, 0.14f}; k1.rotation = {-30.f, -15.f, 12.f}; k1.scale = {1,1,1};
    k1.easeIn = EaseType::EaseInCubic;

    AnimKeyframe k2; k2.time = 0.38f;  k2.label = "Strike";
    k2.offset = {-0.06f, -0.10f, -0.18f}; k2.rotation = {40.f, 10.f, -15.f}; k2.scale = {1,1,1};
    k2.easeIn = EaseType::EaseOutBack;

    AnimKeyframe k3; k3.time = 0.55f;  k3.label = "FollowThrough";
    k3.offset = {-0.04f, -0.06f, -0.08f}; k3.rotation = {18.f, 4.f, -5.f}; k3.scale = {1,1,1};
    k3.easeIn = EaseType::EaseOutCubic;

    AnimKeyframe k4; k4.time = 0.95f;  k4.label = "Recover";
    k4.offset = {0,0,0}; k4.rotation = {0,0,0}; k4.scale = {1,1,1};
    k4.easeIn = EaseType::EaseInOutCubic;

    c.keyframes = {k0, k1, k2, k3, k4};
    return c;
}

inline AnimClip makeDefaultIdle() {
    AnimClip c;
    c.name = "Idle";
    c.loop = true;

    AnimKeyframe k0; k0.time = 0.f;
    k0.offset = {0,0,0}; k0.rotation = {0,0,0}; k0.scale = {1,1,1};
    k0.easeIn = EaseType::EaseInOutQuad;

    AnimKeyframe k1; k1.time = 1.2f;
    k1.offset = {0.f, -0.004f, 0.f}; k1.rotation = {0.3f, 0.f, 0.f}; k1.scale = {1,1,1};
    k1.easeIn = EaseType::EaseInOutQuad;

    AnimKeyframe k2; k2.time = 2.4f;
    k2.offset = {0,0,0}; k2.rotation = {0,0,0}; k2.scale = {1,1,1};
    k2.easeIn = EaseType::EaseInOutQuad;

    c.keyframes = {k0, k1, k2};
    return c;
}

// ── AnimationPlayer ───────────────────────────────────────────────────────────

enum class AnimSlot : uint8_t {
    Idle        = 0,
    LightAttack = 1,
    HeavyAttack = 2,
    COUNT       = 3
};

static constexpr const char* ANIM_SLOT_NAMES[] = { "Idle", "LightAttack", "HeavyAttack" };

struct AnimationPlayer {
    std::array<AnimClip, (int)AnimSlot::COUNT> clips;

    // Playback state
    AnimSlot  activeSlot  = AnimSlot::Idle;
    float     playTime    = 0.f;
    bool      playing     = true;

    // Blending: when a non-idle animation finishes, we blend back to idle
    float blendWeight     = 0.f; // 0 = fully idle, 1 = fully active clip
    float blendSpeed      = 8.f;
    bool  blending        = false;

    AnimationPlayer() {
        clips[(int)AnimSlot::Idle]        = makeDefaultIdle();
        clips[(int)AnimSlot::LightAttack] = makeDefaultLightAttack();
        clips[(int)AnimSlot::HeavyAttack] = makeDefaultHeavyAttack();
    }

    void play(AnimSlot slot) {
        if (slot == activeSlot && playing) return;
        activeSlot = slot;
        playTime   = 0.f;
        playing    = true;
        blendWeight = (slot == AnimSlot::Idle) ? 0.f : 1.f;
        blending   = (slot != AnimSlot::Idle);
    }

    void update(float dt) {
        if (!playing) return;

        const AnimClip& clip = clips[(int)activeSlot];
        float dur = clip.computeDuration();

        playTime += dt;

        if (!clip.loop && playTime >= dur && activeSlot != AnimSlot::Idle) {
            // Finished non-idle clip — blend back to idle
            activeSlot  = AnimSlot::Idle;
            playTime    = 0.f;
            blending    = false;
            blendWeight = 0.f;
        }

        // Blend weight
        float targetWeight = (activeSlot == AnimSlot::Idle) ? 0.f : 1.f;
        blendWeight += (targetWeight - blendWeight) * std::min(blendSpeed * dt, 1.f);
    }

    // Returns combined transform deltas
    void getCurrentDelta(glm::vec3& outOffset, glm::vec3& outRot, glm::vec3& outScale) const {
        glm::vec3 idleOff, idleRot, idleScl;
        clips[(int)AnimSlot::Idle].sample(playTime, idleOff, idleRot, idleScl);

        if (blendWeight < 0.001f || activeSlot == AnimSlot::Idle) {
            outOffset = idleOff; outRot = idleRot; outScale = idleScl;
            return;
        }

        glm::vec3 activeOff, activeRot, activeScl;
        clips[(int)activeSlot].sample(playTime, activeOff, activeRot, activeScl);

        outOffset = glm::mix(idleOff,  activeOff,  blendWeight);
        outRot    = glm::mix(idleRot,  activeRot,  blendWeight);
        outScale  = glm::mix(idleScl,  activeScl,  blendWeight);
    }
};

// ── ImGui Animation Editor ────────────────────────────────────────────────────

struct ViewModelAnimEditor {
    bool     open            = false;
    int      selectedSlot    = (int)AnimSlot::LightAttack;
    int      selectedKeyframe = 0;
    bool     previewPlaying  = false;
    float    previewTime     = 0.f;

    void draw(AnimationPlayer& player) {
        if (!open) return;

        ImGui::SetNextWindowSize({700, 560}, ImGuiCond_Once);
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Once);
        ImGui::Begin("Viewmodel Animation Editor", &open,
                     ImGuiWindowFlags_NoCollapse);

        // ── Slot selector ─────────────────────────────────────────────────
        ImGui::Text("Animation Slot:");
        ImGui::SameLine();
        for (int i = 0; i < (int)AnimSlot::COUNT; i++) {
            if (i > 0) ImGui::SameLine();
            bool sel = (selectedSlot == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, {0.2f,0.6f,0.2f,1.f});
            if (ImGui::Button(ANIM_SLOT_NAMES[i])) selectedSlot = i;
            if (sel) ImGui::PopStyleColor();
        }

        AnimClip& clip = player.clips[selectedSlot];

        ImGui::Separator();

        // ── Clip settings ─────────────────────────────────────────────────
        char nameBuf[64]; strncpy(nameBuf, clip.name.c_str(), 63);
        if (ImGui::InputText("Name##clipname", nameBuf, 64))
            clip.name = nameBuf;
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &clip.loop);

        ImGui::Spacing();

        // ── Preview controls ──────────────────────────────────────────────
        float dur = clip.computeDuration();
        ImGui::Text("Duration: %.3fs", dur);
        ImGui::SameLine(200);
        if (ImGui::Button(previewPlaying ? "Pause Preview" : "Play Preview"))
            previewPlaying = !previewPlaying;
        ImGui::SameLine();
        if (ImGui::Button("Reset##preview")) { previewTime = 0.f; previewPlaying = false; }

        ImGui::SliderFloat("Preview Time", &previewTime, 0.f, std::max(dur, 0.01f));

        // Animate preview time
        if (previewPlaying) {
            previewTime += ImGui::GetIO().DeltaTime;
            if (previewTime > dur) previewTime = clip.loop ? 0.f : dur;
        }

        // Preview sample display
        glm::vec3 pOff, pRot, pScl;
        clip.sample(previewTime, pOff, pRot, pScl);
        ImGui::TextDisabled("  Sample → Offset(%.3f %.3f %.3f)  Rot(%.1f %.1f %.1f)  Scale(%.3f %.3f %.3f)",
            pOff.x, pOff.y, pOff.z,
            pRot.x, pRot.y, pRot.z,
            pScl.x, pScl.y, pScl.z);

        ImGui::Separator();

        // ── Keyframe list ─────────────────────────────────────────────────
        ImGui::Text("Keyframes  (%d)", (int)clip.keyframes.size());
        ImGui::SameLine(200);
        if (ImGui::Button("+ Add")) {
            AnimKeyframe nk;
            nk.time  = clip.keyframes.empty() ? 0.f : clip.keyframes.back().time + 0.2f;
            nk.label = "Key " + std::to_string(clip.keyframes.size());
            clip.keyframes.push_back(nk);
            selectedKeyframe = (int)clip.keyframes.size()-1;
        }
        ImGui::SameLine();
        if (ImGui::Button("- Remove") && !clip.keyframes.empty()) {
            clip.keyframes.erase(clip.keyframes.begin() + selectedKeyframe);
            selectedKeyframe = std::max(0, selectedKeyframe-1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Sort by Time")) {
            std::sort(clip.keyframes.begin(), clip.keyframes.end(),
                [](const AnimKeyframe& a, const AnimKeyframe& b){ return a.time < b.time; });
        }

        // ── Timeline bar ─────────────────────────────────────────────────
        if (dur > 0.f && !clip.keyframes.empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p  = ImGui::GetCursorScreenPos();
            float barW = ImGui::GetContentRegionAvail().x - 4.f;
            float barH = 20.f;

            dl->AddRectFilled(p, {p.x+barW, p.y+barH}, IM_COL32(40,40,40,200), 3.f);
            for (int i = 0; i < (int)clip.keyframes.size(); i++) {
                float xf = clip.keyframes[i].time / dur * barW;
                bool sel = (i == selectedKeyframe);
                ImU32 col = sel ? IM_COL32(100,220,100,255) : IM_COL32(180,180,60,200);
                dl->AddCircleFilled({p.x+xf, p.y+barH/2}, sel ? 7.f : 5.f, col);
            }
            // Preview cursor
            float cx = previewTime / dur * barW;
            dl->AddLine({p.x+cx, p.y}, {p.x+cx, p.y+barH}, IM_COL32(255,100,100,200), 2.f);

            ImGui::Dummy({barW, barH});

            // Click to select keyframe
            if (ImGui::IsItemClicked()) {
                float mx = (ImGui::GetMousePos().x - p.x) / barW * dur;
                float best = 1e9f; int bestIdx = 0;
                for (int i = 0; i < (int)clip.keyframes.size(); i++) {
                    float d = std::abs(clip.keyframes[i].time - mx);
                    if (d < best) { best = d; bestIdx = i; }
                }
                selectedKeyframe = bestIdx;
            }
        }

        ImGui::Separator();

        // ── Selected keyframe editor ──────────────────────────────────────
        if (selectedKeyframe < (int)clip.keyframes.size()) {
            AnimKeyframe& kf = clip.keyframes[selectedKeyframe];

            ImGui::PushStyleColor(ImGuiCol_Header, {0.15f,0.35f,0.15f,1.f});
            bool hdrOpen = ImGui::CollapsingHeader(
                ("Keyframe " + std::to_string(selectedKeyframe) + "  [" + kf.label + "]").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();

            if (hdrOpen) {
                char lblBuf[64]; strncpy(lblBuf, kf.label.c_str(), 63);
                if (ImGui::InputText("Label", lblBuf, 64)) kf.label = lblBuf;

                ImGui::DragFloat("Time (s)##kftime", &kf.time, 0.005f, 0.f, 20.f, "%.3f");

                ImGui::Spacing();
                ImGui::Text("Transform Deltas:");
                ImGui::DragFloat3("Offset##kfoff",   &kf.offset.x,   0.001f, -1.f,   1.f,  "%.4f");
                ImGui::DragFloat3("Rotation##kfrot", &kf.rotation.x, 0.25f,  -180.f, 180.f, "%.2f°");
                ImGui::DragFloat3("Scale##kfscl",    &kf.scale.x,    0.005f,  0.01f,  3.f,  "%.3f");

                ImGui::Spacing();
                ImGui::Text("Ease Into This Keyframe:");
                if (ImGui::BeginCombo("##easecombo", EASE_NAMES[(int)kf.easeIn])) {
                    for (int e = 0; e < (int)EaseType::COUNT; e++) {
                        bool sel = ((int)kf.easeIn == e);
                        if (ImGui::Selectable(EASE_NAMES[e], sel))
                            kf.easeIn = (EaseType)e;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();

                // Quick-set buttons
                if (ImGui::Button("Zero Offset")) kf.offset = {0,0,0};
                ImGui::SameLine();
                if (ImGui::Button("Zero Rotation")) kf.rotation = {0,0,0};
                ImGui::SameLine();
                if (ImGui::Button("Reset Scale")) kf.scale = {1,1,1};
            }
        }

        ImGui::Separator();

        // ── Trigger buttons ───────────────────────────────────────────────
        ImGui::Text("Trigger in-game:");
        ImGui::SameLine();
        if (ImGui::Button("Light Attack")) player.play(AnimSlot::LightAttack);
        ImGui::SameLine();
        if (ImGui::Button("Heavy Attack")) player.play(AnimSlot::HeavyAttack);
        ImGui::SameLine();
        if (ImGui::Button("Reset to Idle")) player.play(AnimSlot::Idle);

        ImGui::Spacing();
        ImGui::TextDisabled("Press ] to show/hide this editor and the transform panel.");

        ImGui::End();
    }
};
