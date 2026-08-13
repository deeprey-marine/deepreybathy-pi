/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "ShaderUtils.h"
#include <wx/log.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <fstream>
#include <sstream>

std::string ShaderUtils::GetShaderPath(const std::string& filename) {
<<<<<<< HEAD
=======
    // Essayer plusieurs chemins possibles
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
    wxArrayString possiblePaths;
    
    wxFileName fn1(wxStandardPaths::Get().GetExecutablePath());
    fn1.SetFullName("");
    fn1.AppendDir("data");
    fn1.AppendDir("shaders");
    fn1.SetFullName(filename);
    possiblePaths.Add(fn1.GetFullPath());
    
    wxFileName fn2(wxStandardPaths::Get().GetExecutablePath());
    fn2.RemoveLastDir();
    fn2.AppendDir("data");
    fn2.AppendDir("shaders");
    fn2.SetFullName(filename);
    possiblePaths.Add(fn2.GetFullPath());
    
    wxFileName fn3 = wxFileName::DirName(wxGetCwd());
    fn3.RemoveLastDir();
    fn3.AppendDir("data");
    fn3.AppendDir("shaders");
    fn3.SetFullName(filename);
    possiblePaths.Add(fn3.GetFullPath());
    
    for (size_t i = 0; i < possiblePaths.GetCount(); i++) {
        if (wxFileExists(possiblePaths[i])) {
            return possiblePaths[i].ToStdString();
        }
    }
    
    wxLogError("Shader file not found: %s", filename.c_str());
    return "";
}

std::string ShaderUtils::LoadShaderFromFile(const std::string& filename) {
    std::string fullPath = GetShaderPath(filename);
    if (fullPath.empty()) {
        return "";
    }
    
    wxLogMessage("Loading shader from: %s", fullPath.c_str());
    
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        wxLogError("Failed to open shader file: %s", fullPath.c_str());
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return buffer.str();
}

GLuint ShaderUtils::CompileShader(const std::string& source, GLenum type) {
    if (source.empty()) {
        wxLogError("Empty shader source");
        return 0;
    }
    
    GLuint shader = glCreateShader(type);
    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        CheckCompileErrors(shader, type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT");
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

GLuint ShaderUtils::CreateProgram(const std::string& vertexPath, const std::string& fragmentPath) {
    wxLogMessage("Creating shader program: %s + %s", vertexPath.c_str(), fragmentPath.c_str());
    
    std::string vertexSource = LoadShaderFromFile(vertexPath);
    std::string fragmentSource = LoadShaderFromFile(fragmentPath);
    
    if (vertexSource.empty() || fragmentSource.empty()) {
        wxLogError("Failed to load shader sources");
        return 0;
    }
    
    GLuint vertexShader = CompileShader(vertexSource, GL_VERTEX_SHADER);
    if (!vertexShader) return 0;
    
    GLuint fragmentShader = CompileShader(fragmentSource, GL_FRAGMENT_SHADER);
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
        CheckLinkErrors(program);
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    if (program) {
        wxLogMessage("Shader program created successfully (ID=%d)", program);
    }
    
    return program;
}

void ShaderUtils::CheckCompileErrors(GLuint shader, const std::string& type) {
    GLint success;
    GLchar infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        wxLogError("Shader compilation error (%s):\n%s", type.c_str(), infoLog);
    }
}

void ShaderUtils::CheckLinkErrors(GLuint program) {
    GLint success;
    GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        wxLogError("Shader linking error:\n%s", infoLog);
    }
}