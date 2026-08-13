/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef RCL_EXTENSIONS_H
#define RCL_EXTENSIONS_H

#include "Rcl.h"
#include "Position.h"
#include <string>
#include <sstream>

class RclExtensions {
public:
    static void GetReducedRclPosition(const Rcl& rcl, int textureSize, 
                                      double overlappingPixels,
                                      Position& outTopLeft, Position& outBottomRight) {

       
        double minX, maxX, minY, maxY;
      

        double left, top, right, bottom;
        rcl.GetMercatorBounds(left, top, right, bottom);  // top > bottom (Nord)

        const double tileWidth  = right  - left;   // X (E-O)
        const double tileHeight = top    - bottom; // Y (N-S)

<<<<<<< HEAD
=======
        // Pixels utiles 
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
        const double usablePixels     = static_cast<double>(textureSize);
        const double usableIntervals  = usablePixels -1;              // ex. 255

        const double pixelSizeX = tileWidth  / usableIntervals;
        const double pixelSizeY = tileHeight / usableIntervals;

        const double extendX = overlappingPixels * pixelSizeX;
        const double extendY = overlappingPixels * pixelSizeY;

        minX = left   + extendX;
        maxX = right  - extendX;
        minY = bottom + extendY;
        maxY = top    - extendY;

        outTopLeft      = Position::FromMercator(minX, maxY); // NW
        outBottomRight  = Position::FromMercator(maxX, minY); // SE
    }

    static void GetExtendedRclPosition(const Rcl& rcl, Position& outTopLeft, Position& outBottomRight) {
   
        double left, top, right, bottom;
        rcl.GetMercatorBounds(left, top, right, bottom);
    
        outTopLeft     = Position::FromMercator(left, top);
        outBottomRight = Position::FromMercator(right, bottom);
    }
    
    static std::string RclToString(const Rcl& rcl) {
        std::stringstream ss;
        ss << "Rcl(" << rcl.Row() << "," << rcl.Column() << "," << rcl.Level() << ")";
        return ss.str();
    }
    
    static std::string RclToKey(const Rcl& rcl) {
        std::stringstream ss;
        ss << rcl.Level() << "_" << rcl.Row() << "_" << rcl.Column();
        return ss.str();
    }

    static Rcl KeyToRcl(const std::string& key) {
        std::stringstream ss(key);
        int level, row, col;
        char underscore;
        ss >> level >> underscore >> row >> underscore >> col;
        return Rcl(row, col, level);
    }
    
    static void ProjectPositionToTexture(const Position& pos, const Rcl& rcl,
                                       int textureSize, int textureOverlap,
                                       float& texX, float& texY) {
        Position extTopLeft, extBottomRight;
        GetReducedRclPosition(rcl, textureSize, textureOverlap, extTopLeft, extBottomRight);
        
        double width = extBottomRight.X() - extTopLeft.X();
        double height = extTopLeft.Y() - extBottomRight.Y();
        
        double normX = (pos.X() - extTopLeft.X()) / width;
        double normY = (pos.Y() - extBottomRight.Y()) / height;
        
        texX = static_cast<float>(normX * textureSize);
        texY = static_cast<float>(normY * textureSize);
    }
    
    static bool IsValidTextureCoordinate(float x, float y, int textureSize) {
        return x >= 0 && x < textureSize && y >= 0 && y < textureSize;
    }
};

#endif