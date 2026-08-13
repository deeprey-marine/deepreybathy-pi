/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgImportFilter.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include "DpPbg.h"

std::vector<Position3D> PbgImportFilter::ApplyFilter(const std::vector<Position3D>& data, 
                                                     const PbgImportFilterConfig& config) {
    if (!config.applyRangeFilter && !config.applyStandardDeviationFilter) {
        return data;
    }
    
    Range rangeFilter = CreateRangeFilter(config.applyRangeFilter, 
                                         config.minDepth, config.maxDepth);
    
    Range stdDevFilter = CreateStandardDeviationFilter(config.applyStandardDeviationFilter,
                                                      data, 
                                                      config.applyRangeFilter ? &rangeFilter : nullptr,
                                                      config.standardDeviationThreshold);
    
    Range finalRange = MergeRanges(
        config.applyRangeFilter ? &rangeFilter : nullptr,
        config.applyStandardDeviationFilter ? &stdDevFilter : nullptr
    );
    
    return FilterPoints(data, finalRange);
}

PbgImportFilter::Range PbgImportFilter::CreateRangeFilter(bool apply, double minDepth, double maxDepth) {
    if (!apply) {
        return {-std::numeric_limits<double>::infinity(), 0.0};
    }
    
    double min = std::min(-std::abs(minDepth), -std::abs(maxDepth));
    double max = std::max(-std::abs(minDepth), -std::abs(maxDepth));
    
    if (maxDepth == 0.0) {
        max = 0.0;
    }
    
    return {min, max};
}

PbgImportFilter::Range PbgImportFilter::CreateStandardDeviationFilter(
    bool apply, 
    const std::vector<Position3D>& data,
    const Range* rangeFilter,
    double threshold) {
    
    if (!apply || data.empty()) {
        return {-std::numeric_limits<double>::infinity(), 0.0};
    }
    
    // Clamp the threshold to a sane band so a bad caller cannot disable the
    // filter (too low) or reject everything (too high).
    threshold = std::max(DpPbg::MIN_STDDEV_THRESHOLD, 
                        std::min(DpPbg::MAX_STDDEV_THRESHOLD, threshold));
    
    double sum = 0.0;
    double sumSquared = 0.0;
    size_t count = 0;
    
    for (const auto& point : data) {
        double depth = point.z;
        
        if (rangeFilter == nullptr || 
            (depth >= rangeFilter->minDepth && depth <= rangeFilter->maxDepth)) {
            sum += depth;
            sumSquared += depth * depth;
            count++;
        }
    }
    
    if (count == 0) {
        return {-std::numeric_limits<double>::infinity(), 0.0};
    }
    
    double mean = sum / count;
    double variance = (sumSquared / count) - (mean * mean);
    double stdDev = std::sqrt(variance) * threshold;
    
    return {mean - stdDev, mean + stdDev};
}

PbgImportFilter::Range PbgImportFilter::MergeRanges(const Range* range1, const Range* range2) {
    if (range1 == nullptr) return *range2;
    if (range2 == nullptr) return *range1;
    
    return {
        std::max(range1->minDepth, range2->minDepth),
        std::min(range1->maxDepth, range2->maxDepth)
    };
}

std::vector<Position3D> PbgImportFilter::FilterPoints(const std::vector<Position3D>& data, 
                                                      const Range& range) {
    // Clamp to the absolute physical bounds of the format: depths are stored
    // negative, so the shallow end can never be above sea level and the deep
    // end can never exceed the deepest representable value.
    double finalMin = std::min(range.minDepth, DpPbg::MAX_DEPTH);
    double finalMax = std::max(range.maxDepth, DpPbg::MIN_DEPTH);
    
    if (finalMin > finalMax) {
        return std::vector<Position3D>();
    }
    
    std::vector<Position3D> filtered;
    filtered.reserve(data.size());
    
    size_t rejectedCount = 0;
    
    for (const auto& point : data) {
        if (point.z >= finalMin && point.z <= finalMax) {
            filtered.push_back(point);
        } else {
            rejectedCount++;
        }
    }
    
    if (rejectedCount > 0) {
        //             rejectedCount, finalMin, finalMax);
    }
    
    return filtered;
}