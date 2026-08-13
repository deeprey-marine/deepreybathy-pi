/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "BathymetryRenderParams.h"
#include <algorithm>
#include <cmath>
#include "DpPbg.h"

void BathymetryRenderParams::SetTargetDepthRange(const MinMax& range) {
    const float EPSILON = DpPbg::DEPTH_RANGE_EPSILON;
    
    if (std::abs(range.min - m_targetDepthRange.min) > EPSILON ||
        std::abs(range.max - m_targetDepthRange.max) > EPSILON) {
        
        m_startDepthRange = m_currentDepthRange;
        m_targetDepthRange = range;
        m_transitionProgress = 0.0f;
    }
}

void BathymetryRenderParams::UpdateTransition(float deltaTime) {
    if (m_transitionProgress >= 1.0f) return;
    
    m_transitionProgress += deltaTime / m_transitionDuration;
    if (m_transitionProgress > 1.0f) {
        m_transitionProgress = 1.0f;
    }
    
    float t = m_transitionProgress;

<<<<<<< HEAD
=======
    // Ease Out Quad (fin lente)
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
    t = t * (2.0f - t);

    m_currentDepthRange = MinMax(
        m_startDepthRange.min + (m_targetDepthRange.min - m_startDepthRange.min) * t,
        m_startDepthRange.max + (m_targetDepthRange.max - m_startDepthRange.max) * t
    );
}

