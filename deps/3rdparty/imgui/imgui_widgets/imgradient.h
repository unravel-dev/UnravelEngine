#pragma once

#include <array>
#include <cstdint>

const int32_t MarkerMax = 256;

struct ImGradientHDRState
{

    struct Element
    {
        std::array<float, 4> Color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct Marker
    {
        float Position{};
        Element Value{};
    };
    bool Linear = true;

    int MarkerCount = 0;
    std::array<Marker, MarkerMax> Markers;

    auto GetMarker(int32_t index) -> Marker*;

    auto AddMarker(float x, Element value) -> int;

    auto RemoveMarker(int32_t index) -> bool;

    auto GetElement(float x) const -> Element;
};

bool ImGradient(const char* title, ImGradientHDRState& state,
                const char* xtitle = "X");
