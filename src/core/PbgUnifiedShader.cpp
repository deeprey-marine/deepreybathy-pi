/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgUnifiedShader.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/log.h>
#include "stb_image.h"
#include "DpPbg.h"

#ifdef STANDALONE_BUILD
    #include "core/Plugin_ViewPort.h"  
#else
    #include "ocpn_plugin.h"        
#endif

const char* UNIFIED_VERTEX_SHADER = R"(
#version 120
attribute vec2 aPosition;
attribute vec2 aTexCoord;
uniform mat4 uTransform;
varying vec2 vTexCoord;

void main() {
    gl_Position = uTransform * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* UNIFIED_FRAGMENT_SHADER = R"(
#version 120
uniform sampler2D uElevationTexture;
uniform sampler2D uGradientTexture;
uniform vec2 uDepthRange;
uniform float uGradientRow;
uniform float uNoData;
uniform bool uShowTileFrame;
uniform float uTextureSize;

uniform bool uEnableIsobaths;
uniform float uIsolineStep;
uniform vec3 uIsolineColor;
uniform float uContourLineLuminance;

uniform float uBorderPadding;

// Phase 1: New uniforms
uniform float uBathymetryOpacity;     // 0.0-1.0 (1.0 = fully opaque)
uniform bool uContourRangeLimited;    // Limit contours to depth range

varying vec2 vTexCoord;

vec3 getGradientColor(float normalizedDepth) {
    vec2 gradientCoord = vec2(normalizedDepth, uGradientRow);
    return texture2D(uGradientTexture, gradientCoord).rgb;
}

bool IsRealCloseTo(float x, float y) {
    return abs(x - y) < 0.99;
}

void main() {
    vec2 texCoordToUse;
    if (uBorderPadding > 0.0) {
        float paddingRatio = uBorderPadding / uTextureSize;
        texCoordToUse = vTexCoord * (1.0 - 2.0 * paddingRatio) + paddingRatio;
    } else {
        texCoordToUse = vTexCoord;
    }
    
    if (uShowTileFrame) {
        float border = 1.0 / uTextureSize;
        if (vTexCoord.x < border || vTexCoord.x > 1.0 - border ||
            vTexCoord.y < border || vTexCoord.y > 1.0 - border) {
            gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    
    float depth = texture2D(uElevationTexture, texCoordToUse).r;
    
    if (depth >= 0.0 || depth >= uNoData) {
        discard;
    }
    
    float normalizedDepth = (depth - uDepthRange.x) / (uDepthRange.y - uDepthRange.x);
    normalizedDepth = clamp(normalizedDepth, 0.0, 1.0);
    
    // Only one palette ships, so the colour always comes from the gradient atlas.
    vec3 color = getGradientColor(normalizedDepth);
    

    // Sample textures for contour detection (needed for both regular isobaths and user contours)
    float decalage = 0.75 / uTextureSize;

    float valeurTextureElevation_Right = texture2D(uElevationTexture, texCoordToUse + vec2(decalage, 0.0)).r;
    float valeurTextureElevation_Bottom = texture2D(uElevationTexture, texCoordToUse + vec2(0.0, decalage)).r;
    float valeurTextureElevation_BottomRight = texture2D(uElevationTexture, texCoordToUse + vec2(decalage, decalage)).r;

    float factor = 1.0;
    vec4 couleurIsoligne = vec4(0.0);

    // Apply contour range limiting: if enabled, only draw contours within depth range
    bool contourInRange = true;
    if (uContourRangeLimited) {
        contourInRange = (depth >= uDepthRange.x && depth <= uDepthRange.y);
    }

    // Fix 3: Remove redundant depth check - let uContourRangeLimited toggle control range limiting
    if (uEnableIsobaths && contourInRange) {
        if (uIsolineStep > 0.0) {
            int seuilValue = int(-depth / uIsolineStep);
            int seuilValueRight = int(-valeurTextureElevation_Right / uIsolineStep);
            int seuilValueBottom = int(-valeurTextureElevation_Bottom / uIsolineStep);
            int seuilValueBottomRight = int(-valeurTextureElevation_BottomRight / uIsolineStep);
            
            bool isContourLine = !IsRealCloseTo(float(seuilValue), float(seuilValueRight)) ||
                                !IsRealCloseTo(float(seuilValue), float(seuilValueBottom)) ||
                                !IsRealCloseTo(float(seuilValue), float(seuilValueBottomRight));

            if (isContourLine) {
                factor = 1.1;
                couleurIsoligne = vec4(uIsolineColor, 1.0);
            }
        }
    }

    // The depth-shaded fill is always drawn, so every surviving pixel gets a
    // colour; contour lines are blended over it and stay fully opaque, which is
    // why opacity is applied to the fill only.
    vec4 shadingColor = vec4(color, uBathymetryOpacity);
    vec4 finalColor;

    if (factor > 1.0) {
        factor *= uContourLineLuminance;
        float factorForLerp = min((factor - 1.0) * couleurIsoligne.a * 6.0, 1.0);
        vec3 blendedColor = mix(shadingColor.rgb, couleurIsoligne.rgb, factorForLerp);
        finalColor = vec4(blendedColor, max(shadingColor.a, factorForLerp));
    } else {
        finalColor = shadingColor;
    }

    gl_FragColor = finalColor;
}
)";

PbgUnifiedShader::PbgUnifiedShader()
    : m_program(0)
    , m_gradientTexture(0)
    , m_gradientTextureWidth(0)
    , m_gradientTextureHeight(0)
    , m_initialized(false) {
}

PbgUnifiedShader::~PbgUnifiedShader() {
    Cleanup();
}

bool PbgUnifiedShader::Initialize() {
    if (m_initialized) return true;
    
    m_program = CreateProgram(UNIFIED_VERTEX_SHADER, UNIFIED_FRAGMENT_SHADER);
    if (!m_program) {
        return false;
    }
    
    m_transformLoc = glGetUniformLocation(m_program, "uTransform");
    m_textureSizeLoc = glGetUniformLocation(m_program, "uTextureSize");
    m_elevationTexLoc = glGetUniformLocation(m_program, "uElevationTexture");
    m_depthRangeLoc = glGetUniformLocation(m_program, "uDepthRange");
    m_noDataLoc = glGetUniformLocation(m_program, "uNoData");
    m_gradientTexLoc = glGetUniformLocation(m_program, "uGradientTexture");
    m_gradientRowLoc = glGetUniformLocation(m_program, "uGradientRow");
    m_showTileFrameLoc = glGetUniformLocation(m_program, "uShowTileFrame");
    m_enableIsobathsLoc = glGetUniformLocation(m_program, "uEnableIsobaths");
    m_isolineStepLoc = glGetUniformLocation(m_program, "uIsolineStep");
    m_isolineColorLoc = glGetUniformLocation(m_program, "uIsolineColor");
    m_contourLineLuminanceLoc = glGetUniformLocation(m_program, "uContourLineLuminance");
    m_borderPaddingLoc = glGetUniformLocation(m_program, "uBorderPadding");

    // Phase 1: New uniform locations
    m_bathymetryOpacityLoc = glGetUniformLocation(m_program, "uBathymetryOpacity");
    m_contourRangeLimitedLoc = glGetUniformLocation(m_program, "uContourRangeLimited");

    if (!LoadGradientTexture()) {
        wxLogWarning("Failed to load gradient texture");
    }
    
    m_initialized = true;
    return true;
}

bool PbgUnifiedShader::LoadGradientTexture() {

        wxFileName fn;
#ifndef STANDALONE_BUILD
    wxString tmp_path = GetPluginDataDir("deepreybathy_pi"); 
    fn.SetPath(tmp_path);
    fn.AppendDir("data");
#else
    fn = wxFileName(wxStandardPaths::Get().GetExecutablePath());
    fn.SetFullName("");
    fn.AppendDir("data");
#endif

    fn.SetFullName("DepthShading.png");

    wxString fullPath = fn.GetFullPath();
   
    if (!wxFileExists(fullPath)) {
        return false;
    } 
    
    int width, height, channels;
    unsigned char* data = stbi_load(fullPath.ToStdString().c_str(), &width, &height, &channels, 0);
    
    if (!data) {
        return false;
    }
    
    m_gradientTextureWidth = width;
    m_gradientTextureHeight = height;
    
    glGenTextures(1, &m_gradientTexture);
    glBindTexture(GL_TEXTURE_2D, m_gradientTexture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    if (channels == 3) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    } else if (channels == 4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    } else {
        stbi_image_free(data);
        return false;
    }
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        stbi_image_free(data);
        return false;
    }
    
    stbi_image_free(data);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return true;
}

void PbgUnifiedShader::Cleanup() {
    if (m_gradientTexture) {
        glDeleteTextures(1, &m_gradientTexture);
        m_gradientTexture = 0;
    }
    
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_initialized = false;
}

GLuint PbgUnifiedShader::CompileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
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

GLuint PbgUnifiedShader::CreateProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) return 0;
    
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        wxLogError("Program linking failed: %s", infoLog);
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return program;
}

void PbgUnifiedShader::Use() {
    glUseProgram(m_program);
    glUniform1f(m_noDataLoc, DpPbg::NO_DATA_VALUE);
}

void PbgUnifiedShader::SetTransform(const float* matrix) {
    glUniformMatrix4fv(m_transformLoc, 1, GL_FALSE, matrix);
}

void PbgUnifiedShader::SetTextureSize(float size) {
    glUniform1f(m_textureSizeLoc, size);
}

void PbgUnifiedShader::SetElevationTexture(int unit) {
    glUniform1i(m_elevationTexLoc, unit);
}

void PbgUnifiedShader::SetDepthRange(float minDepth, float maxDepth) {
    glUniform2f(m_depthRangeLoc, minDepth, maxDepth);
}

void PbgUnifiedShader::SetNoData(float value) {
    glUniform1f(m_noDataLoc, value);
}

void PbgUnifiedShader::SetGradientTexture(int unit) {
    glUniform1i(m_gradientTexLoc, unit);
}

void PbgUnifiedShader::SetGradientRow(float row) {
    glUniform1f(m_gradientRowLoc, row);
}

void PbgUnifiedShader::SetShowTileFrame(bool show) {
    glUniform1i(m_showTileFrameLoc, show ? 1 : 0);
}

void PbgUnifiedShader::SetIsobathsEnabled(bool enabled) {
    glUniform1i(m_enableIsobathsLoc, enabled ? 1 : 0);
}

void PbgUnifiedShader::SetIsolineStep(float step) {
    glUniform1f(m_isolineStepLoc, step);
}

void PbgUnifiedShader::SetIsolineColor(float r, float g, float b) {
    glUniform3f(m_isolineColorLoc, r, g, b);
}

void PbgUnifiedShader::SetContourLineLuminance(float luminance) {
    float adjustedLuminance = luminance / 2.0f + 0.5f;
    glUniform1f(m_contourLineLuminanceLoc, adjustedLuminance);
}

void PbgUnifiedShader::SetBorderPadding(float padding) {
    glUniform1f(m_borderPaddingLoc, padding);
}

// Phase 1: New setters

void PbgUnifiedShader::SetBathymetryOpacity(float opacity) {
    glUniform1f(m_bathymetryOpacityLoc, opacity);
}

void PbgUnifiedShader::SetContourRangeLimited(bool limited) {
    glUniform1i(m_contourRangeLimitedLoc, limited ? 1 : 0);
}