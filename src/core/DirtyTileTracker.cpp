/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "DirtyTileTracker.h"
#include "DpPbg.h"

void DirtyTileTracker::MarkDataDirty(const Rcl& tile16) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = RclExtensions::RclToKey(tile16);
    m_dirtyLevel16.insert(key);
}

bool DirtyTileTracker::IsDataDirty(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = RclExtensions::RclToKey(tile);
    return m_dirtyLevel16.find(key) != m_dirtyLevel16.end();
}

std::vector<Rcl> DirtyTileTracker::GetAndClearDirtyLevel16() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<Rcl> result;
    result.reserve(m_dirtyLevel16.size());

    for (const auto& key : m_dirtyLevel16) {
        result.push_back(RclExtensions::KeyToRcl(key));
    }

    m_dirtyLevel16.clear();
    return result;
}

bool DirtyTileTracker::HasDirtyDataTiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_dirtyLevel16.empty();
}

size_t DirtyTileTracker::GetDirtyDataCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dirtyLevel16.size();
}

void DirtyTileTracker::MarkPyramidDirty(const Rcl& parentTile) {
    if (parentTile.Level() >= DpPbg::DEFAULT_IMPORT_LEVEL) {
        return; // Only levels 0-15 are pyramid dirty
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = RclExtensions::RclToKey(parentTile);
    m_pyramidDirty.insert(key);
}

bool DirtyTileTracker::IsPyramidDirty(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = RclExtensions::RclToKey(tile);
    return m_pyramidDirty.find(key) != m_pyramidDirty.end();
}

void DirtyTileTracker::ClearPyramidDirty(const Rcl& tile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = RclExtensions::RclToKey(tile);
    m_pyramidDirty.erase(key);
}

size_t DirtyTileTracker::GetPyramidDirtyCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pyramidDirty.size();
}

void DirtyTileTracker::MarkAllParentsPyramidDirty(const Rcl& tile16) {
    // Start from the parent of L16 (which is L15) and go up to L0
    Rcl current = tile16;

    std::lock_guard<std::mutex> lock(m_mutex);

    while (current.Level() > 0) {
        current = current.Parent();
        std::string key = RclExtensions::RclToKey(current);
        m_pyramidDirty.insert(key);
    }
}

void DirtyTileTracker::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirtyLevel16.clear();
    m_pyramidDirty.clear();
}
