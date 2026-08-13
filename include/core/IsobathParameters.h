/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once

#include "DpPbg.h"

// Whether contours are drawn at all is BathymetryRenderer::m_isobathsEnabled;
// this struct only describes how they look.
struct IsobathParameters {
    enum class Mode { Manual = 0, Auto = 1 };
    Mode mode = Mode::Manual;
    int autoDensity = 2;              // 1=Low, 2=Medium, 3=High

    // Spacing in metres, recomputed per zoom level when mode is Auto.
    float isolineStep = DpPbg::ISOLINE_STEP;

    struct Color {
        float r, g, b;
        Color() : r(0.0f), g(0.0f), b(0.0f) {}
        Color(float _r, float _g, float _b) : r(_r), g(_g), b(_b) {}
    };

    Color isolineColor;
    float contourLineLuminance = 1.0f;
};