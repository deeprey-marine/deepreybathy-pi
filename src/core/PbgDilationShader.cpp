/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgDilationShader.h"
#include <wx/log.h>
#include "DpPbg.h"

const char* DILATION_VERTEX_SHADER = R"(
#version 120
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
uniform float uQuadScale;
uniform mat4 uTextureMatrix;
uniform float uTextureSize;

void main() {
    vec2 pos = aPosition * uQuadScale;
    gl_Position = vec4(pos, 0.0, 1.0);
    
    float halfPixelOffset = 0.5 / uTextureSize;
    vec2 uvOffset = aTexCoord + halfPixelOffset;
    vec4 uv = uTextureMatrix * vec4(uvOffset, 0.0, 1.0);
    vTexCoord = uv.xy;
}
)";

const char* DILATION_FRAGMENT_SHADER = R"(
#version 120
uniform sampler2D uElevationTexture;
uniform sampler2D uWeightTexture;
uniform vec2 uDirection;
uniform int uPixelEnlarge;
uniform float uTabWeight[30];
uniform float uTextureSize;
varying vec2 vTexCoord;

void main() {
    float weightedElevationSum = 0.0;
    float weightSum = 0.0;
    
    for (int i = -uPixelEnlarge; i <= uPixelEnlarge; i++) {
        vec2 samplePos = vTexCoord + uDirection * float(i);
        samplePos = clamp(samplePos, 0.0, 1.0);
        
        float elevation = texture2D(uElevationTexture, samplePos).r;
        float weight = texture2D(uWeightTexture, samplePos).r;
        
        if (weight > 0.0) {
            int idx = int(abs(float(i)));
            if (idx < 30) {
                float distanceWeight = uTabWeight[idx];
                weightedElevationSum += elevation * distanceWeight;
                weightSum += weight * distanceWeight;
            }
        }
    }
    
    gl_FragData[0] = vec4(weightedElevationSum, 0.0, 0.0, 1.0);
    gl_FragData[1] = vec4(weightSum, 0.0, 0.0, 1.0);
}
)";

PbgDilationShader::PbgDilationShader()
    : m_program(0)
    , m_elevationTexLoc(-1)
    , m_weightTexLoc(-1)
    , m_directionLoc(-1)
    , m_pixelEnlargeLoc(-1)
    , m_tabWeightLoc(-1)
    , m_quadScaleLoc(-1)
    , m_textureMatrixLoc(-1)
    , m_textureSizeLoc(-1)
    , m_initialized(false)
    , m_currentTextureSize(DpPbg::PIXEL_TILE_SIZE)
{
}

PbgDilationShader::~PbgDilationShader() {
    Cleanup();
}

bool PbgDilationShader::Initialize() {
    if (m_initialized) return true;
    
    if (!CompileShaders()) {
        return false;
    }
    
    m_elevationTexLoc = glGetUniformLocation(m_program, "uElevationTexture");
    m_weightTexLoc = glGetUniformLocation(m_program, "uWeightTexture");
    m_directionLoc = glGetUniformLocation(m_program, "uDirection");
    m_pixelEnlargeLoc = glGetUniformLocation(m_program, "uPixelEnlarge");
    m_tabWeightLoc = glGetUniformLocation(m_program, "uTabWeight");
    m_quadScaleLoc = glGetUniformLocation(m_program, "uQuadScale");
    m_textureMatrixLoc = glGetUniformLocation(m_program, "uTextureMatrix");
    m_textureSizeLoc = glGetUniformLocation(m_program, "uTextureSize");
    
    wxLogMessage("Dilation shader initialized: elev=%d, weight=%d, dir=%d, enlarge=%d, tab=%d, size=%d",
                m_elevationTexLoc, m_weightTexLoc, m_directionLoc, m_pixelEnlargeLoc, m_tabWeightLoc, m_textureSizeLoc);
    
    m_initialized = true;
    return true;
}

void PbgDilationShader::Cleanup() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_initialized = false;
}

void PbgDilationShader::Use() {
    glUseProgram(m_program);
}

void PbgDilationShader::SetQuadScale(float s) {
    glUniform1f(m_quadScaleLoc, s);
}

void PbgDilationShader::SetTextureMatrix(const float* m) {
    glUniformMatrix4fv(m_textureMatrixLoc, 1, GL_FALSE, m);
}

void PbgDilationShader::SetTextureSize(float size) {
    m_currentTextureSize = size;
    glUniform1f(m_textureSizeLoc, size);
}

void PbgDilationShader::SetElevationTexture(int unit) {
    glUniform1i(m_elevationTexLoc, unit);
}

void PbgDilationShader::SetWeightTexture(int unit) {
    glUniform1i(m_weightTexLoc, unit);
}

void PbgDilationShader::SetDirection(Direction dir) {
    float dirX = 0.0f, dirY = 0.0f;
    float texelSize = 1.0f / m_currentTextureSize;
    
    switch (dir) {
        case HORIZONTAL:
            dirX = texelSize;
            dirY = 0.0f;
            break;
        case VERTICAL:
            dirX = 0.0f;
            dirY = -texelSize;
            break;
        case DIAGONAL:
            dirX = texelSize * 0.707f;
            dirY = -texelSize * 0.707f;
            break;
    }
    
    glUniform2f(m_directionLoc, dirX, dirY);
}

void PbgDilationShader::SetPixelEnlarge(int pixels) {
    glUniform1i(m_pixelEnlargeLoc, pixels);
}

void PbgDilationShader::SetWeightTable(const float* weights, int count) {
    glUniform1fv(m_tabWeightLoc, count, weights);
}

bool PbgDilationShader::CompileShaders() {
    GLuint vertexShader = CompileShader(DILATION_VERTEX_SHADER, GL_VERTEX_SHADER);
    if (!vertexShader) return false;
    
    GLuint fragmentShader = CompileShader(DILATION_FRAGMENT_SHADER, GL_FRAGMENT_SHADER);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return false;
    }
    
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);
    
    glBindAttribLocation(m_program, 0, "aPosition");
    glBindAttribLocation(m_program, 1, "aTexCoord");
    
    glLinkProgram(m_program);
    
    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_program, 512, nullptr, infoLog);
        wxLogError("Dilation shader linking failed: %s", infoLog);
        glDeleteProgram(m_program);
        m_program = 0;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return true;
}

GLuint PbgDilationShader::CompileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        wxLogError("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}