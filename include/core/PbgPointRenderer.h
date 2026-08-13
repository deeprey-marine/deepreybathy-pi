/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_POINT_RENDERER_H
#define PBG_POINT_RENDERER_H

#include <GL/glew.h>
#include <vector>
#include "Position.h"
#include "PbgTexture.h"
#include "PbgPointShader.h"
#include "Rcl.h"

class PbgPointRenderer {
public:
    PbgPointRenderer();
    ~PbgPointRenderer();
    
    bool Initialize();
    void Cleanup();
    
    void SetRcl(const Rcl& rcl) { m_rcl = rcl; }
   


    void RenderPointsWithMatrix(const std::vector<Position3D>& positions,
                                             PbgTexture* elevationTexture,
                                             PbgTexture* weightTexture,
                                             const float* customMatrix,
                                             float weightTrack = 1.0f);

    

private:
    PbgPointShader* m_shader;
    bool m_initialized;
    Rcl m_rcl;

};

#endif