/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef SHADER_UTILS_H
#define SHADER_UTILS_H

#include <GL/glew.h>
#include <string>

class ShaderUtils {
public:
    static std::string LoadShaderFromFile(const std::string& filename);
    static GLuint CompileShader(const std::string& source, GLenum type);
    static GLuint CreateProgram(const std::string& vertexPath, const std::string& fragmentPath);
    static void CheckCompileErrors(GLuint shader, const std::string& type);
    static void CheckLinkErrors(GLuint program);
    
private:
    static std::string GetShaderPath(const std::string& filename);
};

#endif