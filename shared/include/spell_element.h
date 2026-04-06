#pragma once
#include <cstdint>
#include <string>
enum class SpellElement : uint8_t {
    None = 0,
    Fire, Ice, Lightning, Void, Arcane, Nature,
    Wind, Earth, Water,
    COUNT
};

static constexpr const char* ELEMENT_NAMES[] = {
    "none","fire","ice","lightning","void","arcane","nature",
    "wind","earth","water"
};

struct ElementVisual {
    float r, g, b;
    float glowR, glowG, glowB;
    float trailDensity;
    uint8_t trailType; // 0=none 1=sparkle 2=smoke 3=crystal 4=static 5=ribbon
};

static constexpr ElementVisual ELEMENT_VISUALS[] = {
    {0.8f, 0.8f, 1.0f,  0.6f, 0.6f, 1.0f,  0.3f, 1}, // None
    {1.0f, 0.3f, 0.0f,  1.0f, 0.6f, 0.0f,  0.8f, 2}, // Fire
    {0.4f, 0.8f, 1.0f,  0.8f, 0.95f,1.0f,  0.6f, 3}, // Ice
    {0.9f, 0.9f, 0.2f,  1.0f, 1.0f, 0.6f,  0.9f, 4}, // Lightning
    {0.2f, 0.0f, 0.6f,  0.5f, 0.0f, 1.0f,  0.5f, 1}, // Void
    {0.8f, 0.4f, 1.0f,  0.9f, 0.6f, 1.0f,  0.4f, 1}, // Arcane
    {0.2f, 0.8f, 0.2f,  0.4f, 1.0f, 0.3f,  0.6f, 2}, // Nature
    {0.8f, 0.9f, 1.0f,  0.6f, 0.8f, 1.0f,  0.7f, 5}, // Wind — pale blue ribbon
    {0.5f, 0.3f, 0.1f,  0.7f, 0.5f, 0.2f,  0.4f, 2}, // Earth — brown smoke
    {0.1f, 0.4f, 0.9f,  0.3f, 0.6f, 1.0f,  0.6f, 2}, // Water — deep blue smoke
};

inline SpellElement elementFromString(const std::string& s) {
    for (int i = 0; i < (int)SpellElement::COUNT; i++)
        if (s == ELEMENT_NAMES[i])
            return (SpellElement)i;
    return SpellElement::None;
}

inline const ElementVisual& getElementVisual(SpellElement e) {
    int i = (int)e;
    if (i < 0 || i >= (int)SpellElement::COUNT) i = 0;
    return ELEMENT_VISUALS[i];
}
