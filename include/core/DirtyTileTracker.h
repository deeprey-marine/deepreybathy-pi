/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef DIRTY_TILE_TRACKER_H
#define DIRTY_TILE_TRACKER_H

#include "Rcl.h"
#include "RclExtensions.h"
#include <unordered_set>
#include <vector>
#include <mutex>

/**
 * DirtyTileTracker - Tracks tiles with unsaved changes
 *
 * Two types of dirty tracking:
 * 1. Data dirty (Level 16): Tiles that have been modified with new points
 *    but not yet persisted to the database
 * 2. Pyramid dirty (Levels 0-15): Parent tiles whose children have changed
 *    and need aggregation rebuild
 *
 * Thread-safe: All methods are protected by internal mutex.
 */
class DirtyTileTracker {
public:
    DirtyTileTracker() = default;
    ~DirtyTileTracker() = default;

    // Data dirty tracking (Level 16 tiles)
    void MarkDataDirty(const Rcl& tile16);
    bool IsDataDirty(const Rcl& tile) const;
    std::vector<Rcl> GetAndClearDirtyLevel16();
    bool HasDirtyDataTiles() const;
    size_t GetDirtyDataCount() const;

    // Pyramid dirty tracking (Levels 0-15)
    void MarkPyramidDirty(const Rcl& parentTile);
    bool IsPyramidDirty(const Rcl& tile) const;
    void ClearPyramidDirty(const Rcl& tile);
    size_t GetPyramidDirtyCount() const;

    // Mark all parent levels as pyramid-dirty (called after L16 flush)
    void MarkAllParentsPyramidDirty(const Rcl& tile16);

    // Clear all tracking (for reset/shutdown)
    void Clear();

private:
    mutable std::mutex m_mutex;
    std::unordered_set<std::string> m_dirtyLevel16;   // L16 tiles with unsaved points
    std::unordered_set<std::string> m_pyramidDirty;   // L0-15 tiles needing rebuild
};

#endif // DIRTY_TILE_TRACKER_H
