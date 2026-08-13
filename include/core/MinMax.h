/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef MINMAX_H
#define MINMAX_H

#include <algorithm>
#include <limits>

struct MinMax {
    float min;
    float max;
    
    MinMax() 
        : min(std::numeric_limits<float>::max())
        , max(std::numeric_limits<float>::lowest()) {
    }
    
    MinMax(float minVal, float maxVal) 
        : min(minVal)
        , max(maxVal) {
    }
    
    void ExtendTo(float value) {
        min = std::min(min, value);
        max = std::max(max, value);
    }
    
    void Union(const MinMax& other) {
        min = std::min(min, other.min);
        max = std::max(max, other.max);
    }
    
    bool IsValid() const {
        return min <= max;
    }
    
    void Reset() {
        min = std::numeric_limits<float>::max();
        max = std::numeric_limits<float>::lowest();
    }
};

#endif