/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef RCL_WORLD_H
#define RCL_WORLD_H

#include "Rcl.h"
#include "Position.h"
#include <vector>
#include <cmath>

class RclWorld {
public:

    static Rcl GetRcl(const Position& position, int level) {
        double worldSize = Position::MercatorEquatorialSizeInMeter();
        double x = position.X() + worldSize * 0.5;
        double y = position.Y() + worldSize * 0.5;
    
        int numTiles = Rcl::GetNumberColumnsAtLevel(level);
        double tileSize = worldSize / numTiles;
    
        int column = static_cast<int>(x / tileSize);
        int row = static_cast<int>(y / tileSize);
    
        if (column < 0) column = 0;
        if (column >= numTiles) column = numTiles - 1;
        if (row < 0) row = 0;
        if (row >= numTiles) row = numTiles - 1;
    
        return Rcl(row, column, level);
    }
    
    static Rcl GetRcl(double lat, double lon, int level) {
        Position pos = Position::FromLatLon(lat, lon);
        return GetRcl(pos, level);
    }
    
    static std::vector<Rcl> GetIntersectedRcl(const Position& pos, int level, double radiusMeters) {
        std::vector<Rcl> result;
        
        double worldSize = Position::MercatorEquatorialSizeInMeter();
        double xMin = pos.X() - radiusMeters;
        double xMax = pos.X() + radiusMeters;
        double yMin = pos.Y() - radiusMeters;
        double yMax = pos.Y() + radiusMeters;
        
        double translationX = 0.0;
        if (xMin < -worldSize * 0.5) {
            translationX = worldSize;
        }
        
        Position topLeft = Position::FromMercator(xMin + translationX, yMax);
        Position bottomRight = Position::FromMercator(xMax + translationX, yMin);
        
        Rcl rclTL = GetRcl(topLeft, level);
        Rcl rclBR = GetRcl(bottomRight, level);
        
        for (int col = rclTL.Column(); col <= rclBR.Column(); col++) {
            for (int row = rclTL.Row(); row <= rclBR.Row(); row++) {
                result.push_back(Rcl(row, col, level).ResolveAntiMeridian());
            }
        }
        
        return result;
    }
    
    static int GetLevelFromPixelScaleMercator(const Position& position, double pixelScaleMercator) {
        if (pixelScaleMercator <= 0) return DpPbg::MAX_LEVEL;
        
<<<<<<< HEAD
=======
        // Standard Web Mercator : utilise directement WEB_MERCATOR_PIXEL_SCALE_LEVEL0
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
        double level = log2(DpPbg::WEB_MERCATOR_PIXEL_SCALE_LEVEL0 / pixelScaleMercator);
        
        int roundedLevel = static_cast<int>(round(level));
        
        if (roundedLevel < DpPbg::MIN_LEVEL) roundedLevel = DpPbg::MIN_LEVEL;
        if (roundedLevel > DpPbg::MAX_LEVEL) roundedLevel = DpPbg::MAX_LEVEL;
        
        return roundedLevel;
    }
    
    static double GetWorldSizeX() {
        return Position::MercatorEquatorialSizeInMeter();
    }
    
    static double GetTranslationForAntiMeridian(double viewX, double objectX) {
        double worldSize = GetWorldSizeX();
        double halfWorld = worldSize * 0.5;
        
        if (viewX - objectX > halfWorld) {
            return worldSize;
        } else if (viewX - objectX < -halfWorld) {
            return -worldSize;
        }
        
        return 0.0;
    }
};

#endif