/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_DILATION_SHADER_H
#define PBG_DILATION_SHADER_H

#include <GL/glew.h>

class PbgDilationShader {
public:
    enum Direction {
        HORIZONTAL = 0,
        VERTICAL = 1,
        DIAGONAL = 2
    };
    
    PbgDilationShader();
    ~PbgDilationShader();
    
    bool Initialize();
    void Cleanup();
    
    void Use();
    void SetElevationTexture(int unit);
    void SetWeightTexture(int unit);
    void SetDirection(Direction dir);
    void SetPixelEnlarge(int pixels);
    void SetWeightTable(const float* weights, int count);
    void SetHalfPixelOffset();

    void SetQuadScale(float s);
    void SetTextureMatrix(const float* m); // mat4 (16 floats)
    
    void SetTextureSize(float size);
private:
    GLuint m_program;
    
    GLint m_halfPixelOffsetLoc;
    GLint m_elevationTexLoc;
    GLint m_weightTexLoc;
    GLint m_directionLoc;
    GLint m_pixelEnlargeLoc;
    GLint m_tabWeightLoc;
    GLint m_quadScaleLoc;
    GLint m_textureMatrixLoc;
    GLint m_textureSizeLoc;

    bool m_initialized;
    float m_currentTextureSize;
    
    bool CompileShaders();
    GLuint CompileShader(const char* source, GLenum type);
};

#endif