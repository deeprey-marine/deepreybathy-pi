/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgComputeShader.h"
#include <wx/log.h>

const char* COMPUTE_VERTEX_SHADER = R"(
#version 120
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* COMPUTE_FRAGMENT_SHADER = R"(
#version 120
uniform sampler2D uWeightedElevation;
uniform sampler2D uWeight;
varying vec2 vTexCoord;

void main() {
    vec4 weightedElev = texture2D(uWeightedElevation, vTexCoord);
    float weight = texture2D(uWeight, vTexCoord).r;
    
    if (weight > 0.0) {
        float finalElevation = weightedElev.r / weight;
        gl_FragData[0] = vec4(finalElevation, 0.0, 0.0, 1.0);
        gl_FragData[1] = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
        gl_FragData[0] = vec4(0.0);
        gl_FragData[1] = vec4(0.0);
    }
}
)";

PbgComputeShader::PbgComputeShader()
    : m_program(0)
    , m_weightedElevationLoc(-1)
    , m_weightLoc(-1)
    , m_initialized(false)
{
}

PbgComputeShader::~PbgComputeShader() {
    Cleanup();
}

bool PbgComputeShader::Initialize() {
    if (m_initialized) return true;
    
    if (!CompileShaders()) {
        return false;
    }
    
    m_weightedElevationLoc = glGetUniformLocation(m_program, "uWeightedElevation");
    m_weightLoc = glGetUniformLocation(m_program, "uWeight");
    
    wxLogMessage("Compute shader initialized: weightedElev=%d, weight=%d",
                m_weightedElevationLoc, m_weightLoc);
    
    m_initialized = true;
    return true;
}

void PbgComputeShader::Cleanup() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_initialized = false;
}

void PbgComputeShader::Use() {
    glUseProgram(m_program);
}

void PbgComputeShader::SetWeightedElevationTexture(int unit) {
    glUniform1i(m_weightedElevationLoc, unit);
}

void PbgComputeShader::SetWeightTexture(int unit) {
    glUniform1i(m_weightLoc, unit);
}

bool PbgComputeShader::CompileShaders() {
    GLuint vertexShader = CompileShader(COMPUTE_VERTEX_SHADER, GL_VERTEX_SHADER);
    if (!vertexShader) return false;
    
    GLuint fragmentShader = CompileShader(COMPUTE_FRAGMENT_SHADER, GL_FRAGMENT_SHADER);
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
        wxLogError("Compute shader linking failed: %s", infoLog);
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

GLuint PbgComputeShader::CompileShader(const char* source, GLenum type) {
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