/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef BATHYMETRY_RENDER_PARAMS_H
#define BATHYMETRY_RENDER_PARAMS_H

#include "MinMax.h"
#include <chrono>

class BathymetryRenderParams {
public:
    BathymetryRenderParams() 
        : m_targetDepthRange(-1000.0, 0.0)
        , m_currentDepthRange(-1000.0, 0.0)
        , m_useGlobalDepthRange(true)
        , m_transitionDuration(0.5f)
        , m_transitionProgress(1.0f) {
    }
    
    MinMax GetGlobalDepthRange() const { return m_currentDepthRange; }
    void SetTargetDepthRange(const MinMax& range);
    
    bool UseGlobalDepthRange() const { return m_useGlobalDepthRange; }
    void SetUseGlobalDepthRange(bool use) { m_useGlobalDepthRange = use; }
    
    void UpdateTransition(float deltaTime);
    bool IsTransitioning() const { return m_transitionProgress < 1.0f; }
    
    void SetTransitionDuration(float duration) { m_transitionDuration = duration; }
   float GetTransitionDuration() const { return m_transitionDuration; }
    
private:
    MinMax m_targetDepthRange;
    MinMax m_currentDepthRange;
    MinMax m_startDepthRange;
    bool m_useGlobalDepthRange;
    
    float m_transitionDuration;
    float m_transitionProgress;
};

#endif