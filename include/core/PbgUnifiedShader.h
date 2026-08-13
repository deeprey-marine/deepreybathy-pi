/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once
#include <memory>
#include <GL/glew.h>
#include <string>
#include "IsobathParameters.h"

class PbgUnifiedShader {
public:
    PbgUnifiedShader();
    ~PbgUnifiedShader();
    
    bool Initialize();
    void Cleanup();
    void Use();
    
    void SetTransform(const float* matrix);
    void SetTextureSize(float size);
    void SetElevationTexture(int unit);
    void SetDepthRange(float minDepth, float maxDepth);
    void SetNoData(float value);
    
    void SetGradientTexture(int unit);
    void SetGradientRow(float row);
    void SetShowTileFrame(bool show);
    

    // Isobathes
    void SetIsobathsEnabled(bool enabled);
    void SetIsolineStep(float step);
    void SetIsolineColor(float r, float g, float b);
    void SetContourLineLuminance(float luminance);
    
    
    

    void SetBorderPadding(float padding);

    // Phase 1: New uniforms
    void SetBathymetryOpacity(float opacity);
    void SetContourRangeLimited(bool limited);

    GLuint GetGradientTexture() const { return m_gradientTexture; }
    int GetGradientTextureHeight() const { return m_gradientTextureHeight; }

private:
    bool LoadGradientTexture();
    GLuint CompileShader(GLenum type, const std::string& source);
    GLuint CreateProgram(const std::string& vertexSource, const std::string& fragmentSource);
    
    GLuint m_program;
    GLuint m_gradientTexture;
    int m_gradientTextureWidth;
    int m_gradientTextureHeight;
    bool m_initialized;
    
    GLint m_transformLoc;
    GLint m_textureSizeLoc;

    GLint m_elevationTexLoc;
    GLint m_depthRangeLoc;
    GLint m_noDataLoc;
    GLint m_gradientTexLoc;
    GLint m_gradientRowLoc;
    GLint m_showTileFrameLoc;
    GLint m_enableIsobathsLoc;
    GLint m_isolineStepLoc;
    GLint m_isolineColorLoc;
    GLint m_contourLineLuminanceLoc;
    GLint m_borderPaddingLoc;

    // Phase 1: New uniform locations
    GLint m_bathymetryOpacityLoc{-1};
    GLint m_contourRangeLimitedLoc{-1};
};