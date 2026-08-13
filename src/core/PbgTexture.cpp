/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgTexture.h"
#include <wx/log.h>

PbgTexture::PbgTexture(int width, int height, bool isRenderTarget)
    : m_width(width)
    , m_height(height)
    , m_isRenderTarget(isRenderTarget)
    , m_texture(0)
    , m_framebuffer(0)
    , m_initialized(false)
{
}

PbgTexture::~PbgTexture() {
    Cleanup();
}

bool PbgTexture::Initialize() {
    if (m_initialized) return true;
    
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_width, m_height, 0, GL_RED, GL_FLOAT, nullptr);
    

   
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
 
   /*
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    */

    if (m_isRenderTarget) {
        glGenFramebuffers(1, &m_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    m_initialized = true;
    return true;
}

void PbgTexture::Cleanup() {
    if (m_framebuffer) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    
    m_initialized = false;
}

void PbgTexture::SetSamplingMode(GLint minFilter, GLint magFilter,
                                 GLint wrapS, GLint wrapT)
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glBindTexture(GL_TEXTURE_2D, 0);
}



void PbgTexture::BindAsRenderTarget(int attachment) {
    if (!m_isRenderTarget || !m_framebuffer) {
        wxLogMessage("PbgTexture::BindAsRenderTarget - Not a render target!");
        return;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_width, m_height);
}

void PbgTexture::BindAsTexture(int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

void PbgTexture::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PbgTexture::Clear() {
    if (!m_isRenderTarget) return;
    
    GLint oldFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
}

void PbgTexture::SetSamplerState(GLenum minFilter, GLenum magFilter) {
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glBindTexture(GL_TEXTURE_2D, 0);
}