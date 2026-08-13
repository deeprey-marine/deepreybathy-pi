/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_DILATION_RENDERER_H
#define PBG_DILATION_RENDERER_H

#include <memory>
#include "PbgTexture.h"
#include "PbgDilationShader.h"
#include "PbgComputeShader.h"

class PbgDilationRenderer {
public:
    PbgDilationRenderer();
    ~PbgDilationRenderer();
    
    bool Initialize();
    void Cleanup();
    
    void DrawPbgPointEnlarge(PbgTexture* sourceElevation, PbgTexture* sourceWeight,
                   PbgTexture* destElevation, PbgTexture* destWeight,
                   int pixelEnlarge, PbgDilationShader::Direction direction,
                   int totalBrushSize);
    
    
    void ComputeElevationOnly(PbgTexture* weightedElevation, 
                             PbgTexture* weight,
                             PbgTexture* finalElevation);

    void RenderPbgData(PbgTexture* elevationTexture, 
                              PbgTexture* weightTexture,
                              int brushSize);
    
private:
    std::unique_ptr<PbgDilationShader> m_dilationShader;
    std::unique_ptr<PbgComputeShader> m_computeShader;
    
    std::unique_ptr<PbgTexture> m_tempTexture1;
    std::unique_ptr<PbgTexture> m_tempTexture2;
    std::unique_ptr<PbgTexture> m_tempTexture3;
    std::unique_ptr<PbgTexture> m_tempTexture4;
    
    GLuint m_quadVAO;
    GLuint m_quadVBO;
    
    bool m_initialized;
    
    void CreateQuadVAO();
    void DrawFullscreenQuad();
    void CalculateWeights(float* weights, int count, int brushSize, int pixelEnlarge);

    
};

#endif