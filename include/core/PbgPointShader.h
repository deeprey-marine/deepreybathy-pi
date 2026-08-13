/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_POINT_SHADER_H
#define PBG_POINT_SHADER_H

#include <GL/glew.h>

class PbgPointShader {
public:
    PbgPointShader();
    ~PbgPointShader();
    
    bool Initialize();
    void Cleanup();
    
    void Use();
    void SetPointMatrix(const float* matrix);
    void SetTextureSize(float size);
    void SetWeightTrack(float weight);
    void SetAltitudeRange(float minAlt, float maxAlt);
    
private:
    GLuint m_program;
    GLint m_pointMatrixLoc;
    GLint m_textureSizeLoc;
    GLint m_weightTrackLoc;
    GLint m_altitudeRangeLoc;
    
    bool m_initialized;
    
    bool CompileShaders();
    GLuint CompileShader(const char* source, GLenum type);
};

#endif