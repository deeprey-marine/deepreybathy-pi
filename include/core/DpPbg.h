/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once
#include <wx/wx.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DpPbg {

    constexpr double MERCATOR_WORLD_SIZE = 40075016.68557849;
    constexpr double EARTH_RADIUS = 6378137.0;
    constexpr double MIN_LATITUDE = -85.0511287798;
    constexpr double MAX_LATITUDE = 85.0511287798;
    
    constexpr double PI = 3.14159265358979323846;
    constexpr float NO_DATA_VALUE = std::numeric_limits<float>::max(); // 3.4028235e38

    constexpr double INCHES_PER_METER = 39.37;
    constexpr double MAX_SCALE_DENOMINATOR = 120.0;
    constexpr double MIN_SCALE_DENOMINATOR = 40000000.0;

    constexpr int WORLD_TILE_SIZE = 512;

    constexpr int PIXEL_TILE_SIZE = 256;
    constexpr int PADDED_TILE_SIZE = PIXEL_TILE_SIZE + 2;  // 258: 256 + 1px ghost border each side

    constexpr double LOD_SWITCH_BIAS = 0.6;
    
    

    constexpr int MAX_WORK_TEXTURE_SIZE = 512; 

    constexpr int TEXTURE_OVERLAP = 2;

    constexpr int DATA_OVERLAP = 2;

    constexpr int FIXED_BORDER_WIDTH  = (MAX_WORK_TEXTURE_SIZE - PIXEL_TILE_SIZE)/2;

    constexpr int MAX_PIXEL_PER_PASS = 29;

    constexpr float ZOOM_WHEEL_FACTOR = 1.05f;

    constexpr uint8_t PBG_FORMAT_VERSION = 1;
    constexpr size_t PBG_MIN_HEADER_SIZE = 18;
    constexpr size_t PBG_POINT_DATA_SIZE = 6;

    constexpr int MIN_LEVEL = 0;
    constexpr int MAX_LEVEL = 22;
    constexpr int DEFAULT_IMPORT_LEVEL = 16;
    constexpr int DEFAULT_EXPORT_LEVEL = 16;
    constexpr int MERCATOR_SCALE_REFERENCE_LEVEL = 20;
    constexpr int MIN_LEVEL_FOR_NEIGHBOR_SEARCH = 6;

    constexpr int TOTAL_NEIGHBOURS_COUNT = 8;

    constexpr int WATCHDOG_RENDER_TIMER = 2000; 
    

    constexpr double MIN_DEPTH = -28000.0;
    constexpr double MAX_DEPTH = 0.0;

    constexpr float  SEED_DISCRIMINENT_OFFSET = 20000.0f;
    constexpr float  INTERPOLATED_OFFSET      = 100000.0f;

    constexpr double MIN_STDDEV_THRESHOLD = 0.001;
    constexpr double MAX_STDDEV_THRESHOLD = 10.0;
    constexpr double DEVIATION_THRESHOLD = 3.0;

    

    constexpr size_t DEFAULT_MAX_CACHED_TILES = 100;
    constexpr size_t DEFAULT_MAX_CACHED_TILES_PARENT = 400;
    constexpr size_t DEFAULT_MAX_CACHED_TEXTURE = 100;
    constexpr int MAX_TILES_PER_FRAME = 5;
    constexpr int MAX_CONTOUR_TRACES_PER_FRAME = 2;

    constexpr int DEFAULT_PRELOAD_RADIUS = 1;
    constexpr bool ENABLE_ADJACENT_TILE_PREFETCH = true;
    constexpr int MAX_PARENT_SEARCH_LEVELS = 6;

    constexpr float DEFAULT_ZOOM = 1.0f;
    constexpr float DEFAULT_BRUSH_SIZE_FACTOR = 0.5f;  // 50% - standard default
    constexpr int DEFAULT_VIEWPORT_LEVEL = 15;
    constexpr double VIEWPORT_EPSILON = 1e-6;
    constexpr double MIN_METERS_PER_PIXEL = 0.01;
    constexpr float MAX_DELTA_TIME = 0.1f;
    constexpr float DEPTH_RANGE_EPSILON = 0.1f;

    constexpr float ISOLINE_STEP = 1.0f;

    constexpr float MIN_SCALE_RATIO = 0.1f;
    constexpr float MAX_SCALE_RATIO = 10.0f;

    //shader
    constexpr float DILATION_WEIGHT_FORMULA_FACTOR = 10.0f;
    constexpr float DEFAULT_POINT_SIZE_PX = 4.0f;

    //isobath
    // Height of one palette band in data/DepthShading.png. The shipped atlas
    // contains exactly one band, so the shader always samples its middle.
    // (A stale GRADIENT_TEXTURE_HEIGHT = 1024.0f used to live here; it never
    // matched the real image and nothing read it.)
    constexpr float GRADIENT_PALETTE_HEIGHT = 4.0f;
    constexpr int DEFAULT_GRADIENT_TEXTURE_UNIT = 2;

    

    
 

    
    constexpr double WEB_MERCATOR_PIXEL_SCALE_LEVEL0 = 156543.03392;
    constexpr double DEG_TO_RAD = PI / 180.0;
    constexpr double RAD_TO_DEG = 180.0 / PI;
    constexpr double PI_OVER_4 = PI / 4.0;
    constexpr double PI_OVER_360 = PI / 360.0;
    
 

struct BrushSizeConfig
{
    // =========================================================
    // Lookup table for level-dependent brush sizes.
    // Zoomed OUT (low level) = large brush, to fill gaps between survey tracks
    // Zoomed IN (high level) = small brush, to preserve survey accuracy
    // =========================================================
    struct LevelRange {
        int minSize;  // 0% slider
        int maxSize;  // 100% slider
    };

    static const LevelRange& GetLevelRange(int level) {
        // Smooth exponential decay curve: maxSize = 128 * 0.82^level
        // 0% slider   = detail (small brush at all levels)
        // 100% slider = gap filling (level-dependent, smooth transitions)
        static const LevelRange ranges[] = {
            {1, 128},    // 0  - extreme zoom out (harbor/port scale)
            {1, 108},    // 1  - smoother decay
            {1, 92},     // 2
            {1, 78},     // 3
            {1, 68},     // 4
            {1, 60},     // 5  - medium-far zoom
            {1, 54},     // 6  - continuing smooth transition
            {1, 48},     // 7
            {2, 44},     // 8  - start increasing min for better coverage
            {2, 40},     // 9
            {2, 36},     // 10 - medium zoom
            {2, 32},     // 11
            {3, 30},     // 12 - approaching default import level
            {3, 28},     // 13
            {3, 26},     // 14
            {3, 24},     // 15
            {4, 22},     // 16 <- DEFAULT_IMPORT_LEVEL
            {4, 20},     // 17 - zoom in (detailed features)
            {5, 18},     // 18
            {5, 16},     // 19
            {6, 14},     // 20 - high detail (seamount features visible)
            {6, 12},     // 21
            {7, 10},     // 22 - extreme zoom in (maximum accuracy)
        };

        if (level < 0) return ranges[0];
        if (level > 22) return ranges[22];
        return ranges[level];
    }

    // Dynamic mode only. The fixed / physical-radius ("Surveyor") mode was
    // removed for the open-source build.
    static int CalculateBrushSize(int level,
                                  float userFactor01,
                                  int textureWidth = PIXEL_TILE_SIZE,
                                  int pbgInsertionLevel = DEFAULT_IMPORT_LEVEL)
    {
        if (userFactor01 < 0.f) userFactor01 = 0.f;
        if (userFactor01 > 1.f) userFactor01 = 1.f;

        // Zoomed OUT = bigger brush (fills gaps between tracks)
        // Zoomed IN  = smaller brush (accuracy / detail)
        const LevelRange& range = GetLevelRange(level);
        int brushSize = range.minSize +
                        static_cast<int>(userFactor01 * (range.maxSize - range.minSize));

        // Safety clamp for GPU performance
        int maxAllowed = textureWidth / 2;
        if (brushSize > maxAllowed) brushSize = maxAllowed;

        return brushSize;
    }

    static int GetBaseBrushSize(int level) {
        return CalculateBrushSize(level, 1.0f);
    }
};

    constexpr int SUGGESTED_SCALES[] = {
        50000000,  // Level 0
        25000000,  // Level 1
        10000000,  // Level 2
        5000000,   // Level 3
        2500000,   // Level 4
        1000000,   // Level 5
        500000,    // Level 6
        250000,    // Level 7
        100000,    // Level 8
        50000,     // Level 9
        25000,     // Level 10
        10000,     // Level 11
        5000,      // Level 12
        2500,      // Level 13
        1000,      // Level 14
        500,       // Level 15
        250,       // Level 16
        100,       // Level 17
        50,        // Level 18
        25,        // Level 19
        10,        // Level 20
        5,         // Level 21
        2          // Level 22
    };

}