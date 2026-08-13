/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgDilationRenderer.h"
#include <cmath>
#include "DpPbg.h"

PbgDilationRenderer::PbgDilationRenderer()
    : m_quadVAO(0)
    , m_quadVBO(0)
    , m_initialized(false)
{
}

PbgDilationRenderer::~PbgDilationRenderer() {
    Cleanup();
}

bool PbgDilationRenderer::Initialize() {
    if (m_initialized) return true;
    
    m_dilationShader = std::make_unique<PbgDilationShader>();
    if (!m_dilationShader->Initialize()) {
        return false;
    }
    
    m_computeShader = std::make_unique<PbgComputeShader>();
    if (!m_computeShader->Initialize()) {
        return false;
    }
    
    m_tempTexture1 = std::make_unique<PbgTexture>(DpPbg::PIXEL_TILE_SIZE, DpPbg::PIXEL_TILE_SIZE, true);
    m_tempTexture2 = std::make_unique<PbgTexture>(DpPbg::PIXEL_TILE_SIZE, DpPbg::PIXEL_TILE_SIZE, true);
    m_tempTexture3 = std::make_unique<PbgTexture>(DpPbg::PIXEL_TILE_SIZE, DpPbg::PIXEL_TILE_SIZE, true);
    m_tempTexture4 = std::make_unique<PbgTexture>(DpPbg::PIXEL_TILE_SIZE, DpPbg::PIXEL_TILE_SIZE, true);
    
    if (!m_tempTexture1->Initialize() || !m_tempTexture2->Initialize() ||
        !m_tempTexture3->Initialize() || !m_tempTexture4->Initialize()) {
        return false;
    }
    
    CreateQuadVAO();
    
    m_initialized = true;
    return true;
}

void PbgDilationRenderer::Cleanup() {
    if (m_quadVAO) {
        glDeleteVertexArrays(1, &m_quadVAO);
        m_quadVAO = 0;
    }
    if (m_quadVBO) {
        glDeleteBuffers(1, &m_quadVBO);
        m_quadVBO = 0;
    }
    
    m_dilationShader.reset();
    m_computeShader.reset();
    m_tempTexture1.reset();
    m_tempTexture2.reset();
    m_tempTexture3.reset();
    m_tempTexture4.reset();
    
    m_initialized = false;
}

void PbgDilationRenderer::CreateQuadVAO() {
    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glBindVertexArray(0);
}

void PbgDilationRenderer::DrawFullscreenQuad() {
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void PbgDilationRenderer::CalculateWeights(float* weights, int count, int brushSize, int pixelEnlarge) {
    // Inverse-square falloff: weight = 1 / (num + distance^2), where num is
    // derived from the full brush size so the curve scales with the brush.
    float num = 1.0f + powf(DpPbg::DILATION_WEIGHT_FORMULA_FACTOR / static_cast<float>(brushSize), 2.0f);
    
    for (int i = 0; i < count; i++) {
        if (i < pixelEnlarge) {
            weights[i] = 1.0f / powf(num, static_cast<float>(i * i));
        } else {
            weights[i] = 0.0f;
        }
    }
}

void PbgDilationRenderer::DrawPbgPointEnlarge(PbgTexture* sourceElevation, PbgTexture* sourceWeight,
                                             PbgTexture* destElevation, PbgTexture* destWeight,
                                             int pixelEnlarge, PbgDilationShader::Direction direction,
                                             int totalBrushSize) {
    GLint oldFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    
    int textureSize = destElevation->GetWidth();
    
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 
                          destElevation->GetTextureId(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 
                          destWeight->GetTextureId(), 0);
    
    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
    
    glViewport(0, 0, textureSize, textureSize);
    
    m_dilationShader->Use();
    
    m_dilationShader->SetTextureSize(static_cast<float>(textureSize));
    
    m_dilationShader->SetQuadScale(1.0f);
    
    float texMat[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    m_dilationShader->SetTextureMatrix(texMat);
    
    sourceElevation->BindAsTexture(0);
    m_dilationShader->SetElevationTexture(0);
    
    sourceWeight->BindAsTexture(1);
    m_dilationShader->SetWeightTexture(1);
    
    m_dilationShader->SetDirection(direction);
    m_dilationShader->SetPixelEnlarge(pixelEnlarge);
    
    float weights[30];
    CalculateWeights(weights, 30, totalBrushSize, pixelEnlarge);
    m_dilationShader->SetWeightTable(weights, 30);
    
    DrawFullscreenQuad();
    
    glUseProgram(0);
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

void PbgDilationRenderer::RenderPbgData(PbgTexture* elevationTexture, 
                                       PbgTexture* weightTexture,
                                       int brushSize) {
    int textureSize = elevationTexture->GetWidth();
    
    if (!m_tempTexture1 || m_tempTexture1->GetWidth() != textureSize) {
        m_tempTexture1 = std::make_unique<PbgTexture>(textureSize, textureSize, true);
        m_tempTexture2 = std::make_unique<PbgTexture>(textureSize, textureSize, true);
        
        m_tempTexture1->Initialize();
        m_tempTexture2->Initialize();
    }
    
    const int MAX_PIXELS_PER_PASS = DpPbg::MAX_PIXEL_PER_PASS;
    int remainingPixels = brushSize;
    
    PbgTexture* currentElevSrc = elevationTexture;
    PbgTexture* currentWeightSrc = weightTexture;
    PbgTexture* currentElevDst = m_tempTexture1.get();
    PbgTexture* currentWeightDst = m_tempTexture2.get();
    
    bool resultInOriginal = true;
    
    while (remainingPixels > 0) {
        int currentPixels = std::min(remainingPixels, MAX_PIXELS_PER_PASS);
        
        // Pass vertical
        DrawPbgPointEnlarge(currentElevSrc, currentWeightSrc,
                           currentElevDst, currentWeightDst,
                           currentPixels, PbgDilationShader::VERTICAL, brushSize);
        
        std::swap(currentElevSrc, currentElevDst);
        std::swap(currentWeightSrc, currentWeightDst);
        resultInOriginal = !resultInOriginal;
        
        // Pass horizontal
        DrawPbgPointEnlarge(currentElevSrc, currentWeightSrc,
                           currentElevDst, currentWeightDst,
                           currentPixels, PbgDilationShader::HORIZONTAL, brushSize);
        
        std::swap(currentElevSrc, currentElevDst);
        std::swap(currentWeightSrc, currentWeightDst);
        resultInOriginal = !resultInOriginal;
        
        remainingPixels -= currentPixels;
    }
    
    if (!resultInOriginal) {
        GLuint copyFbo;
        glGenFramebuffers(1, &copyFbo);
        
        glBindFramebuffer(GL_READ_FRAMEBUFFER, currentElevSrc->GetFramebuffer());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, elevationTexture->GetFramebuffer());
        glBlitFramebuffer(0, 0, textureSize, textureSize, 
                          0, 0, textureSize, textureSize, 
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        
        glBindFramebuffer(GL_READ_FRAMEBUFFER, currentWeightSrc->GetFramebuffer());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, weightTexture->GetFramebuffer());
        glBlitFramebuffer(0, 0, textureSize, textureSize, 
                          0, 0, textureSize, textureSize, 
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        
        glDeleteFramebuffers(1, &copyFbo);
    }
    
    ComputeElevationOnly(elevationTexture, weightTexture, elevationTexture);
}

void PbgDilationRenderer::ComputeElevationOnly(PbgTexture* weightedElevation, 
                                               PbgTexture* weight,
                                               PbgTexture* finalElevation) {
    GLint oldFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 
                          finalElevation->GetTextureId(), 0);
    
    GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        wxLogError("ComputeElevationOnly: Framebuffer incomplete, status: 0x%x", status);
        glDeleteFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
        return;
    }
    
    int textureSize = finalElevation->GetWidth();
    glViewport(0, 0, textureSize, textureSize);
    
    m_computeShader->Use();
    
    weightedElevation->BindAsTexture(0);
    m_computeShader->SetWeightedElevationTexture(0);
    
    weight->BindAsTexture(1);
    m_computeShader->SetWeightTexture(1);
    
    DrawFullscreenQuad();
    
    glUseProgram(0);
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}