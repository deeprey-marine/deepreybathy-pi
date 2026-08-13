/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_IMPORT_FILTER_H
#define PBG_IMPORT_FILTER_H

#include <vector>
#include "Position.h"
#include "DpPbg.h"

struct PbgImportFilterConfig {
    bool applyRangeFilter = false;
    double minDepth = DpPbg::MIN_DEPTH;
    double maxDepth =  DpPbg::MAX_DEPTH;
    
    bool applyStandardDeviationFilter = false;
    double standardDeviationThreshold = DpPbg::DEVIATION_THRESHOLD;
};

class PbgImportFilter {
public:
    static std::vector<Position3D> ApplyFilter(const std::vector<Position3D>& data, 
                                               const PbgImportFilterConfig& config);
    
private:
    struct Range {
        double minDepth;
        double maxDepth;
    };
    
    static Range CreateRangeFilter(bool apply, double minDepth, double maxDepth);
    static Range CreateStandardDeviationFilter(bool apply, const std::vector<Position3D>& data, 
                                              const Range* rangeFilter, double threshold);
    static Range MergeRanges(const Range* range1, const Range* range2);
    static std::vector<Position3D> FilterPoints(const std::vector<Position3D>& data, const Range& range);
};

#endif