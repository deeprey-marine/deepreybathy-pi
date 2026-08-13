/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_COMPUTE_SHADER_H
#define PBG_COMPUTE_SHADER_H

#include <GL/glew.h>

class PbgComputeShader {
public:
    PbgComputeShader();
    ~PbgComputeShader();
    
    bool Initialize();
    void Cleanup();
    
    void Use();
    void SetWeightedElevationTexture(int unit);
    void SetWeightTexture(int unit);
    
private:
    GLuint m_program;
    GLint m_weightedElevationLoc;
    GLint m_weightLoc;
    
    bool m_initialized;
    
    bool CompileShaders();
    GLuint CompileShader(const char* source, GLenum type);
};

#endif