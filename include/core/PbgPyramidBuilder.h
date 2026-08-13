/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_PYRAMID_BUILDER_H
#define PBG_PYRAMID_BUILDER_H

#include <vector>
#include <memory>
#include <array>
#include <functional>
#include <wx/string.h>
#include <wx/log.h>
#include <wx/progdlg.h>
#include "Rcl.h"
#include "FloatGrid.h"
#include "PbgTileDatabase.h"

class PbgPyramidBuilder {
public:
    // Progress reporting: percent is 0-100 over the whole pyramid build.
    // Return false to request cancellation.
    using ProgressFn = std::function<bool(int percent, const wxString& status)>;

    PbgPyramidBuilder(PbgTileDatabase* database);
    ~PbgPyramidBuilder();

    bool BuildPyramid(const std::vector<Rcl>& tilesAtLevel16, wxProgressDialog* progressDlg = nullptr);

    // Same, but reports progress through a callback (usable off the UI thread,
    // e.g. the async import worker which cannot own a wxProgressDialog).
    bool BuildPyramid(const std::vector<Rcl>& tilesAtLevel16, const ProgressFn& progressCb);

    // Build pyramid from L16 down to minLevel (for incremental live recording)
    bool BuildPyramidToLevel(const std::vector<Rcl>& tilesAtLevel16, int minLevel);
    
    bool RebuildFromDatabase(wxProgressDialog* progressDlg = nullptr);
    
    bool GeneralizeSingleLevel(int fromLevel, int toLevel, wxProgressDialog* progressDlg = nullptr);
    
    
private:
    PbgTileDatabase* m_database;

    
    struct GridCache {
        Rcl tile;
        std::unique_ptr<FloatGrid> grid;
    };
    
    bool GeneralizeLevel(const std::vector<Rcl>& tilesAtCurrentLevel, int targetLevel, wxProgressDialog* progressDlg = nullptr);
    // Callback variant; the wxProgressDialog overload delegates to this so the
    // generalization body lives in one place.
    bool GeneralizeLevel(const std::vector<Rcl>& tilesAtCurrentLevel, int targetLevel, const ProgressFn& progressCb);
    
    bool CreateParentTileFromChildren(const Rcl& parentTile, const std::vector<Rcl>& childTiles);
    
   
};

#endif