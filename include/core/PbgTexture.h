/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_TEXTURE_H
#define PBG_TEXTURE_H

#include <GL/glew.h>
#include "DpPbg.h"

class PbgTexture {
public:
    PbgTexture(int width = DpPbg::PIXEL_TILE_SIZE, int height = DpPbg::PIXEL_TILE_SIZE, bool isRenderTarget = true);
    ~PbgTexture();
    
    bool Initialize();
    void Cleanup();
    
    void BindAsRenderTarget(int attachment = 0);
    void BindAsTexture(int unit = 0);
    void Unbind();
    void Clear();
    
    GLuint GetTextureId() const { return m_texture; }
    GLuint GetFramebuffer() const { return m_framebuffer; }

    
    void SetSamplerState(GLenum minFilter, GLenum magFilter);
    void SetSamplingMode(GLint minFilter, GLint magFilter,
                         GLint wrapS, GLint wrapT);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    
private:
    int m_width;
    int m_height;
    bool m_isRenderTarget;
    
    GLuint m_texture;
    GLuint m_framebuffer;
    
    bool m_initialized;
};

#endif