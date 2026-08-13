/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgPointRenderer.h"
#include "MinMax.h"
#include <chrono>

PbgPointRenderer::PbgPointRenderer()
    : m_shader(nullptr)
    , m_initialized(false)
    , m_rcl(10, 10, 10)
{
}

PbgPointRenderer::~PbgPointRenderer() {
    Cleanup();
}

bool PbgPointRenderer::Initialize() {
    if (m_initialized) return true;
    
    m_shader = new PbgPointShader();
    if (!m_shader->Initialize()) {
        return false;
    }
    
    m_initialized = true;
    return true;
}

void PbgPointRenderer::Cleanup() {
    if (m_shader) {
        m_shader->Cleanup();
        delete m_shader;
        m_shader = nullptr;
    }
    m_initialized = false;
}

void PbgPointRenderer::RenderPointsWithMatrix(const std::vector<Position3D>& positions,
                                             PbgTexture* elevationTexture,
                                             PbgTexture* weightTexture,
                                             const float* customMatrix,
                                             float weightTrack) {
    auto totalStart = std::chrono::high_resolution_clock::now();
    
    struct {
        double fboCreation = 0;
        double fboSetup = 0;
        double minMaxCalc = 0;
        double vboCreation = 0;
        double vboUpload = 0;
        double shaderSetup = 0;
        double drawCall = 0;
        double cleanup = 0;
        int pointCount = 0;
    } stats;
    
    stats.pointCount = positions.size();
    
    if (!m_initialized || positions.empty()) return;
 
    GLint oldFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    
    // --- FBO Creation ---
    auto sectionStart = std::chrono::high_resolution_clock::now();
    
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    stats.fboCreation = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- FBO Setup ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 
                          elevationTexture->GetTextureId(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 
                          weightTexture->GetTextureId(), 0);
    
    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
        return;
    }
    
    glViewport(0, 0, elevationTexture->GetWidth(), elevationTexture->GetHeight());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    stats.fboSetup = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- Shader Setup ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    m_shader->Use();
    m_shader->SetPointMatrix(customMatrix);
    m_shader->SetTextureSize(static_cast<float>(elevationTexture->GetWidth()));
    m_shader->SetWeightTrack(weightTrack);
    
    stats.shaderSetup = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- MinMax Calculation ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    MinMax minMax(1e10f, -1e10f);
    for (const auto& pos : positions) {
        minMax.ExtendTo(pos.z);
    }
    
    stats.minMaxCalc = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    m_shader->SetAltitudeRange(minMax.min, minMax.max);
    
    // --- VBO Creation ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    stats.vboCreation = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    sectionStart = std::chrono::high_resolution_clock::now();
    
    glBufferData(GL_ARRAY_BUFFER, 
                positions.size() * sizeof(Position3D), 
                positions.data(),  // DIRECT ! Plus de copie !
                GL_STATIC_DRAW);
    
    stats.vboUpload = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- Draw Call ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Position3D), (void*)0);
    
    glEnable(GL_POINT_SPRITE);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    
    glDisable(GL_DEPTH_TEST);
    
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(positions.size()));
    
    glFinish();
    
    glEnable(GL_DEPTH_TEST);
    
    glDisableVertexAttribArray(0);
    
    stats.drawCall = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- Cleanup ---
    sectionStart = std::chrono::high_resolution_clock::now();
    
    glDeleteBuffers(1, &vbo);
    
    glUseProgram(0);
    
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    
    stats.cleanup = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sectionStart).count();
    
    // --- Logging ---
    double totalTime = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - totalStart).count();
    
    if (totalTime > 10.0) {
        wxLogMessage("[PERF] PointRenderer %.1fms (%d points): FBO:%.1f+%.1f MinMax:%.1f "
                    "VBO:%.1f+%.1f Draw:%.1f Clean:%.1f",
                    totalTime, stats.pointCount,
                    stats.fboCreation, stats.fboSetup, stats.minMaxCalc,
                    stats.vboCreation, stats.vboUpload, stats.drawCall, stats.cleanup);
        
        if (stats.vboUpload > 10.0) {
            wxLogMessage("[PERF] VBO upload still slow: %.1fms for %.1f MB", 
                        stats.vboUpload, (stats.pointCount * sizeof(Position3D)) / (1024.0 * 1024.0));
        }
        if (stats.drawCall > 20.0) {
            wxLogMessage("[PERF] GPU draw still slow: %.1fms for %d points", 
                        stats.drawCall, stats.pointCount);
        }
    }
}

 