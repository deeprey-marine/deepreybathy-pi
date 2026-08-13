/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef TILE_INFO_H
#define TILE_INFO_H

#include "Rcl.h"
#include "Position3D.h"
#include <wx/datetime.h>

struct TileInfo {
    Rcl tile;
    int pointCount;
    MinMax depthRange;
    wxDateTime lastUpdate;
    
    TileInfo() : tile(0, 0, 0), pointCount(0), depthRange(0, 0) {}
};

#endif