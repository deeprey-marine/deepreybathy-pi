/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgPointShader.h"
#include <wx/log.h>

namespace {
const char* POINT_VERTEX_SHADER = R"(
#version 120
attribute vec3 aPosition;
uniform mat4 uPointMatrix;
uniform float uTextureSize;
uniform vec2 uAltitudeRange;
varying float vAltitude;
void main() {
    vec4 projected = uPointMatrix * vec4(aPosition.xy, 0.0, 1.0);
    gl_Position = projected;
    gl_PointSize = 4.0;
    vAltitude = aPosition.z;
}
)";

const char* POINT_FRAGMENT_SHADER = R"(
#version 120
uniform float uWeightTrack;
varying float vAltitude;
void main() {
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) discard;
    
    gl_FragData[0] = vec4(vAltitude, 0.0, 0.0, 1.0);
    gl_FragData[1] = vec4(uWeightTrack, 0.0, 0.0, 1.0);
}
)";
}

PbgPointShader::PbgPointShader()
    : m_program(0)
    , m_pointMatrixLoc(-1)
    , m_textureSizeLoc(-1)
    , m_weightTrackLoc(-1)
    , m_altitudeRangeLoc(-1)
    , m_initialized(false)
{
}

PbgPointShader::~PbgPointShader() {
    Cleanup();
}

bool PbgPointShader::Initialize() {
    if (m_initialized) return true;
    
    if (!CompileShaders()) {
        return false;
    }
    
    m_pointMatrixLoc = glGetUniformLocation(m_program, "uPointMatrix");
    m_textureSizeLoc = glGetUniformLocation(m_program, "uTextureSize");
    m_weightTrackLoc = glGetUniformLocation(m_program, "uWeightTrack");
    m_altitudeRangeLoc = glGetUniformLocation(m_program, "uAltitudeRange");
    
    wxLogMessage("Point shader initialized: matrix=%d, size=%d, weight=%d, range=%d",
                m_pointMatrixLoc, m_textureSizeLoc, m_weightTrackLoc, m_altitudeRangeLoc);
    
    m_initialized = true;
    return true;
}

void PbgPointShader::Cleanup() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_initialized = false;
}

void PbgPointShader::Use() {
    glUseProgram(m_program);
}

void PbgPointShader::SetPointMatrix(const float* matrix) {
    glUniformMatrix4fv(m_pointMatrixLoc, 1, GL_FALSE, matrix);
}

void PbgPointShader::SetTextureSize(float size) {
    glUniform1f(m_textureSizeLoc, size);
}

void PbgPointShader::SetWeightTrack(float weight) {
    glUniform1f(m_weightTrackLoc, weight);
}

void PbgPointShader::SetAltitudeRange(float minAlt, float maxAlt) {
    glUniform2f(m_altitudeRangeLoc, minAlt, maxAlt);
}

bool PbgPointShader::CompileShaders() {
    GLuint vertexShader = CompileShader(POINT_VERTEX_SHADER, GL_VERTEX_SHADER);
    if (!vertexShader) return false;
    
    GLuint fragmentShader = CompileShader(POINT_FRAGMENT_SHADER, GL_FRAGMENT_SHADER);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return false;
    }
    
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);
    
    glBindAttribLocation(m_program, 0, "aPosition");
    
    glLinkProgram(m_program);
    
    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_program, 512, nullptr, infoLog);
        wxLogError("Point shader linking failed: %s", infoLog);
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

GLuint PbgPointShader::CompileShader(const char* source, GLenum type) {
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