/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "BathymetryDataManager.h"
#include "PbgSeedsReaderWriter.h"
#include "RclExtensions.h"
#include "DirtyTileTracker.h"
#include "AsyncPersistenceManager.h"
#include <algorithm>
#include <wx/log.h>
#include <unordered_set>
#include "RclWorld.h"

BathymetryDataManager::BathymetryDataManager()
    : m_gridLoaderShouldStop(false)
    , m_maxCachedChildGrids(DpPbg::DEFAULT_MAX_CACHED_TILES)
    , m_maxParentGridCacheSize(DpPbg::DEFAULT_MAX_CACHED_TILES_PARENT)
    , m_parentGridLevel(DpPbg::DEFAULT_IMPORT_LEVEL)
    , m_initialized(false)
    , m_globalDepthRange(DpPbg::MIN_DEPTH, DpPbg::MAX_DEPTH)
{
}

BathymetryDataManager::~BathymetryDataManager() {
    Shutdown();
}

bool BathymetryDataManager::Initialize() {
    try {
        if (!m_database.Initialize()) {
            return false;
        }
        
        double minLon, maxLon, minLat, maxLat, minDepth, maxDepth;
        if (m_database.GetGlobalBounds(minLon, maxLon, minLat, maxLat, minDepth, maxDepth)) {
            m_globalDepthRange = MinMax(minDepth, maxDepth);
        }
        
        
        m_gridLoaderShouldStop = false;
        m_gridLoaderThread = std::thread(&BathymetryDataManager::GridLoaderThreadFunc, this);

        m_extendedGridCalculatorShouldStop = false;
        m_extendedGridCalculatorThread = std::thread(&BathymetryDataManager::ExtendedGridCalculatorThreadFunc, this);
        
        m_initialized = true;
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void BathymetryDataManager::Shutdown() {

     {
        std::lock_guard<std::mutex> lock(m_extendedGridQueueMutex);
        m_extendedGridCalculatorShouldStop = true;
    }
    m_extendedGridQueueCV.notify_all();
    
    if (m_extendedGridCalculatorThread.joinable()) {
        m_extendedGridCalculatorThread.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
        m_extendedGridCache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_childGridLoadQueueMutex);
        m_gridLoaderShouldStop = true;
    }
    m_childGridLoadQueueCV.notify_all();
    
    if (m_gridLoaderThread.joinable()) {
        m_gridLoaderThread.join();
    }
    
    ClearCache();

    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        m_visibleGridKeys.clear();
    }

    m_initialized = false;
}

void BathymetryDataManager::ClearCache() {
    {
        std::lock_guard<std::mutex> lock(m_parentGridCacheMutex);
        m_parentGridCache.clear();
        m_parentLRUList.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        m_childGridCache.clear();
    }
}

bool BathymetryDataManager::IsChildGridAvailable(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
    
    std::string key = GetGridKey(tile);
    auto it = m_childGridCache.find(key);

    return it != m_childGridCache.end() && it->second.grid && !it->second.isLoading;
}

std::shared_ptr<FloatGrid> BathymetryDataManager::GetChildGrid(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
    std::string key = GetGridKey(tile);
    auto it = m_childGridCache.find(key);
    if (it != m_childGridCache.end() && it->second.grid && !it->second.isLoading)
        return it->second.grid;
    return nullptr;
}

void BathymetryDataManager::GridLoaderThreadFunc() {
    while (!m_gridLoaderShouldStop) {
        ChildGridLoadRequest request;
        bool hasRequest = false;
        
        {
            std::unique_lock<std::mutex> lock(m_childGridLoadQueueMutex);
            m_childGridLoadQueueCV.wait(lock, [this] { return !m_childGridLoadQueue.empty() || m_gridLoaderShouldStop; });
            
            if (m_gridLoaderShouldStop) break;
            
            if (!m_childGridLoadQueue.empty()) {
                request = m_childGridLoadQueue.top();
                m_childGridLoadQueue.pop();
                hasRequest = true;
            }
        }
        
        if (!hasRequest) continue;
        
        uint64_t currentGen = m_currentGeneration.load();
        if (request.generation != currentGen) {
            continue;  // Stale request from a superseded viewport
        }
        
        {
            std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
            std::string key = GetGridKey(request.tile);
            auto it = m_childGridCache.find(key);
            
            if (it != m_childGridCache.end() && it->second.grid) {
                continue;
            }
            
            m_childGridCache[key].isLoading = true;
        }
        
        static int loadCount = 0;
        if (++loadCount % 5 == 0) {
            const char* typeStr = request.type == GridLoadRequest::VISIBLE ? "VISIBLE" :
                                 request.type == GridLoadRequest::ADJACENT ? "ADJACENT" : "PRELOAD";
        }
        
        LoadGrid(request.tile, request.priority);
    }
}

void BathymetryDataManager::LoadGrid(const Rcl& tile, float priority) {
    std::string childKey = GetGridKey(tile);
    
    Rcl parentTile = GetParentGridAtLevelH(tile);
    std::string parentKey = GetGridKey(parentTile);
    
    std::shared_ptr<FloatGrid> parentGrid;
    bool shouldLoadParent = false;
    
    {
        std::unique_lock<std::mutex> parentLock(m_parentGridCacheMutex);
        
        auto parentIt = m_parentGridCache.find(parentKey);
        
        if (parentIt != m_parentGridCache.end()) {
            if (parentIt->second.isLoading) {
                m_statSingleFlightCoalesced++;
                parentIt->second.loadCondition.wait(parentLock, [this, &parentKey] {
                    auto it = m_parentGridCache.find(parentKey);
                    return it == m_parentGridCache.end() || !it->second.isLoading;
                });
                
                parentIt = m_parentGridCache.find(parentKey);
                if (parentIt != m_parentGridCache.end() && parentIt->second.grid) {
                    parentGrid = parentIt->second.grid;
                    UpdateParentGridLRU(parentKey);
                    m_statParentCacheHits++;
                }
            } else if (parentIt->second.grid) {
                parentGrid = parentIt->second.grid;
                UpdateParentGridLRU(parentKey);
                m_statParentCacheHits++;
            }
        } else {
            shouldLoadParent = true;
            m_statParentCacheMisses++;
            
            auto& parentData = m_parentGridCache[parentKey];
            parentData.isLoading = true;
        }
    }
    
    if (shouldLoadParent) {
        LoadParentGridFromDatabase(parentTile, parentGrid);
        
        std::unique_lock<std::mutex> parentLock(m_parentGridCacheMutex);
        
        auto it = m_parentGridCache.find(parentKey);
        
        if (it != m_parentGridCache.end()) {
            it->second.isLoading = false;
            it->second.grid = parentGrid;
            it->second.lastAccess = std::chrono::steady_clock::now();
            
            m_parentLRUList.push_back(parentKey);
            it->second.lruIterator = std::prev(m_parentLRUList.end());
            
            if (m_parentGridCache.size() > m_maxParentGridCacheSize) {
                EvictLRUParentGrid();
            }
            
            m_statParentDbLoads++;
            
            it->second.loadCondition.notify_all();
        }
    }
    
    
    std::shared_ptr<FloatGrid> childGrid;
    
    if (tile == parentTile) {
        childGrid = parentGrid;
    } else {
        auto extractStart = std::chrono::high_resolution_clock::now();
        
        childGrid = ExtractChildGridFromParentGrid(parentGrid, parentTile, tile);
        
        auto extractEnd = std::chrono::high_resolution_clock::now();
        double extractMs = std::chrono::duration<double, std::milli>(extractEnd - extractStart).count();
        
        {
            static std::mutex statMutex;
            std::lock_guard<std::mutex> lock(statMutex);
            m_statTotalExtractTimeMs = m_statTotalExtractTimeMs.load() + extractMs;
        }
        m_statExtractCount++;
    }
    
    if (!childGrid) {
        childGrid = std::make_shared<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
        childGrid->FillWithNoData();
    }
    
    MinMax depthRange(childGrid->GetMin(), childGrid->GetMax());
    
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        
        CachedChildGrid& data = m_childGridCache[childKey];
        data.grid = childGrid;
        data.depthRange = depthRange;
        data.lastAccess = std::chrono::steady_clock::now();
        data.isLoading = false;
    }

    bool isVisible = false;
    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        isVisible = IsTileInVisibleSet(childKey);
    }

    if (isVisible) {
        EnqueueExtendedGridCalculation(tile, priority);
    }
    
    CheckForExtendedGridUpgrade(tile);

}

Rcl BathymetryDataManager::GetParentGridAtLevelH(const Rcl& tile) const {
    int level = tile.level();
    
    if (level <= m_parentGridLevel) {
        return tile;
    }
    
    int shifts = level - m_parentGridLevel;
    int parentRow = tile.row() >> shifts;
    int parentCol = tile.column() >> shifts;
    
    return Rcl(parentRow, parentCol, m_parentGridLevel);
}

bool BathymetryDataManager::LoadParentGridFromDatabase(const Rcl& parentTile, 
                                                   std::shared_ptr<FloatGrid>& grid) {
    std::vector<uint8_t> blob;
    
    if (!m_database.GetPbgBlob(parentTile, blob)) {
        auto emptyGrid = std::make_shared<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
        emptyGrid->FillWithNoData();
        grid = emptyGrid;
        return true;
    }
    
    auto newGrid = std::make_shared<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    
    if (!PbgSeedsReaderWriter::Deserialize(blob, *newGrid)) {
        newGrid->FillWithNoData();
        grid = newGrid;
        return true;
    }
    
    grid = newGrid;
    return true;
}

std::shared_ptr<FloatGrid> BathymetryDataManager::ExtractChildGridFromParentGrid(
    const std::shared_ptr<FloatGrid>& parentGrid,
    const Rcl& parentTile,
    const Rcl& childTile) {
    
    if (!parentGrid) return nullptr;
    
    return FloatGrid::ExtractFromParent(parentGrid, parentTile, childTile);
}

void BathymetryDataManager::UpdateParentGridLRU(const std::string& key) {
    auto it = m_parentGridCache.find(key);
    if (it != m_parentGridCache.end()) {
        m_parentLRUList.erase(it->second.lruIterator);
        m_parentLRUList.push_back(key);
        it->second.lruIterator = std::prev(m_parentLRUList.end());
    }
}

std::shared_ptr<FloatGrid> BathymetryDataManager::GetParentGridForFlush(const Rcl& tile16) const {
    std::lock_guard<std::mutex> lock(m_parentGridCacheMutex);

    std::string key = GetGridKey(tile16);
    auto it = m_parentGridCache.find(key);

    if (it != m_parentGridCache.end() && it->second.grid && !it->second.isLoading) {
        return it->second.grid;
    }

    return nullptr;
}

void BathymetryDataManager::EvictLRUParentGrid() {
    if (m_parentLRUList.empty()) return;

    auto it = m_parentLRUList.begin();
    while (it != m_parentLRUList.end()) {
        auto cacheIt = m_parentGridCache.find(*it);
        if (cacheIt != m_parentGridCache.end() && !cacheIt->second.isLoading) {
            std::string keyToEvict = *it;
            Rcl tileToEvict = RclExtensions::KeyToRcl(keyToEvict);

            // Check if tile is dirty and flush before evicting
            if (m_dirtyTracker && m_persistenceManager && m_dirtyTracker->IsDataDirty(tileToEvict)) {
                // Release lock while flushing to avoid deadlock
                // Note: This is safe because we haven't modified the cache yet
                // and we'll re-check the eviction condition after
                m_persistenceManager->FlushSingleTile(tileToEvict);
            }

            m_parentLRUList.erase(it);
            m_parentGridCache.erase(keyToEvict);
            return;
        }
        ++it;
    }

}

void BathymetryDataManager::EnqueueGridLoading(const Rcl& tile) {
 
    uint64_t currentGen = m_currentGeneration.load();
    float defaultPriority = 2000.0f + tile.level();
    
    std::string key = GetGridKey(tile);
    
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        
        auto it = m_childGridCache.find(key);
        if (it != m_childGridCache.end()) {
            if (it->second.isLoading || it->second.grid) {
                return;
            }
        }
        
        m_childGridCache[key].isLoading = true;
        m_childGridCache[key].lastAccess = std::chrono::steady_clock::now();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_childGridLoadQueueMutex);
        m_childGridLoadQueue.push({tile, currentGen, defaultPriority, GridLoadRequest::PRELOAD});
    }
    
    m_childGridLoadQueueCV.notify_one();
}

void BathymetryDataManager::EvictOldestChildGrids(const std::unordered_set<std::string>& visibleKeys) {

    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> tileTimes;

    for (const auto& pair : m_childGridCache) {
        if (!pair.second.isLoading) {
            bool isCurrentlyVisible = (visibleKeys.find(pair.first) != visibleKeys.end());

            if (!isCurrentlyVisible) {
                tileTimes.push_back({pair.first, pair.second.lastAccess});
            }
        }
    }
    
    std::sort(tileTimes.begin(), tileTimes.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    
    size_t tilesToRemove = m_childGridCache.size() > m_maxCachedChildGrids ? 
                          m_childGridCache.size() - m_maxCachedChildGrids : 0;
    
    for (size_t i = 0; i < tilesToRemove && i < tileTimes.size(); i++) {
        m_childGridCache.erase(tileTimes[i].first);
    }
}

void BathymetryDataManager::InvalidateParentGrid(const Rcl& parentTile) {
    Rcl parentAtH = GetParentGridAtLevelH(parentTile);
    std::string parentKey = GetGridKey(parentAtH);
    
    {
        std::lock_guard<std::mutex> lock(m_parentGridCacheMutex);
        auto it = m_parentGridCache.find(parentKey);
        if (it != m_parentGridCache.end()) {
            if (!it->second.isLoading) {
                m_parentLRUList.erase(it->second.lruIterator);
            }
            m_parentGridCache.erase(it);
        }
    }

    
    std::vector<Rcl> tilesToNotify;
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        
        auto it = m_childGridCache.begin();
        while (it != m_childGridCache.end()) {
            Rcl tile = Rcl::FromKey(it->first);
            Rcl tileParent = GetParentGridAtLevelH(tile);
            
            if (tileParent == parentAtH) {
                tilesToNotify.push_back(tile);
                it = m_childGridCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
        auto it = m_extendedGridCache.begin();
        while (it != m_extendedGridCache.end()) {
            Rcl tile = Rcl::FromKey(it->first);
            Rcl tileParent = GetParentGridAtLevelH(tile);
            
            if (tileParent == parentAtH) {
                it = m_extendedGridCache.erase(it);
            } else {
                ++it;
            }
        }
    }
    

}

void BathymetryDataManager::InvalidateCachesForPyramidBuild(const std::vector<Rcl>& l16Tiles, int minLevel) {
    // Collect all affected pyramid tile keys (levels 15 down to minLevel)
    std::unordered_set<std::string> affectedKeys;

    for (const auto& tile : l16Tiles) {
        Rcl current = tile.Parent();  // Start at L15
        while (current.Level() >= minLevel) {
            affectedKeys.insert(GetGridKey(current));
            if (current.Level() == 0) break;
            current = current.Parent();
        }
    }

    if (affectedKeys.empty()) return;

    // Lock ordering: parentCacheMutex -> childCacheMutex -> extendedGridCacheMutex
    {
        std::lock_guard<std::mutex> parentLock(m_parentGridCacheMutex);
        for (const auto& key : affectedKeys) {
            auto it = m_parentGridCache.find(key);
            if (it != m_parentGridCache.end()) {
                if (!it->second.isLoading) {
                    m_parentLRUList.erase(it->second.lruIterator);
                }
                m_parentGridCache.erase(it);
            }
        }
    }

    {
        std::lock_guard<std::mutex> childLock(m_childGridCacheMutex);
        auto it = m_childGridCache.begin();
        while (it != m_childGridCache.end()) {
            if (affectedKeys.find(it->first) != affectedKeys.end()) {
                it = m_childGridCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> extLock(m_extendedGridCacheMutex);
        auto it = m_extendedGridCache.begin();
        while (it != m_extendedGridCache.end()) {
            if (affectedKeys.find(it->first) != affectedKeys.end()) {
                it = m_extendedGridCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    wxLogMessage("InvalidateCachesForPyramidBuild: Invalidated %zu pyramid tile entries", affectedKeys.size());
}

std::string BathymetryDataManager::GetGridKey(const Rcl& tile) const {
    return RclExtensions::RclToKey(tile);
    
    // return tile.ToKey();
}

void BathymetryDataManager::GetRclNeighbors(const Rcl& rcl, std::vector<Rcl>& neighbours) {
    neighbours.clear();
    
    int row = rcl.row();
    int col = rcl.column();
    int level = rcl.level();
    
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            
            Rcl neighbour(row + dr, col + dc, level);
            neighbours.push_back(neighbour.ResolveAntiMeridian());
        }
    }
}

bool BathymetryDataManager::AreNeighbors(const Rcl& tile1, const Rcl& tile2) {
    if (tile1.level() != tile2.level()) return false;
    
    int dr = std::abs(tile1.row() - tile2.row());
    int dc = std::abs(tile1.column() - tile2.column());
    
    int numCols = Rcl::GetNumberColumnsAtLevel(tile1.level());
    if (dc > numCols / 2) {
        dc = numCols - dc;
    }
    
    return (dr <= 1 && dc <= 1) && !(dr == 0 && dc == 0);
}

int BathymetryDataManager::GetExtendedGridForTile(const Rcl& tile, 
                                                    std::vector<Position3D>& allSeeds,
                                                    MinMax& depthRange,
                                                    int borderWidth) {
    allSeeds.clear();
    
    Position tileTL = tile.GetPositionTopLeft();
    Position tileBR = tile.GetPositionBottomRight();
    double pixelSize = (tileBR.X() - tileTL.X()) / DpPbg::PIXEL_TILE_SIZE;
    double borderSizeMercator = borderWidth * pixelSize;
    
    double extendedLeft   = tileTL.X() - borderSizeMercator;
    double extendedRight  = tileBR.X() + borderSizeMercator;
    double extendedTop    = tileTL.Y() + borderSizeMercator;
    double extendedBottom = tileBR.Y() - borderSizeMercator;
    
    std::vector<Rcl> tilesToCheck;
    tilesToCheck.push_back(tile);
    
    std::vector<Rcl> neighbors;
    GetRclNeighbors(tile, neighbors);
    tilesToCheck.insert(tilesToCheck.end(), neighbors.begin(), neighbors.end());
    
    struct TileGridPair {
        Rcl tile;
        std::shared_ptr<FloatGrid> grid;
    };
    std::vector<TileGridPair> gridsToProcess;
    std::vector<Rcl> neighborsToEnqueue;
    
    int actualNeighborsUsed = 0;
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        
        std::string mainTileKey = GetGridKey(tile);
        auto mainIt = m_childGridCache.find(mainTileKey);
        if (mainIt != m_childGridCache.end() && mainIt->second.grid) {
            depthRange = mainIt->second.depthRange;
        } else {
            depthRange = MinMax(DpPbg::NO_DATA_VALUE, DpPbg::NO_DATA_VALUE);
        }
        
        for (const auto& currentTile : tilesToCheck) {
            std::string key = GetGridKey(currentTile);
            auto it = m_childGridCache.find(key);
            
            if (it != m_childGridCache.end() && it->second.grid && !it->second.isLoading) {

                gridsToProcess.push_back({currentTile, it->second.grid});
                actualNeighborsUsed++;            
            } else if (std::find(neighbors.begin(), neighbors.end(), currentTile) != neighbors.end()) {
                if (it == m_childGridCache.end() || !it->second.isLoading) {
                    neighborsToEnqueue.push_back(currentTile);
                }
            }
        }
    }
    
    for (const auto& pair : gridsToProcess) {
        std::vector<Position3D> tilePoints;
        pair.grid->ExtractPoints3D(pair.tile, tilePoints);
        
        for (const auto& point : tilePoints) {
            if (point.x >= extendedLeft && point.x <= extendedRight &&
                point.y >= extendedBottom && point.y <= extendedTop) {
                allSeeds.push_back(point);
            }
        }
    }
    
    for (const auto& neighborRcl : neighborsToEnqueue) {
        EnqueueGridLoading(neighborRcl);
    }
    
    return actualNeighborsUsed;
}

BathymetryDataManager::CacheStats BathymetryDataManager::GetStats() const {
    CacheStats stats;
    
    stats.parentCacheHits = m_statParentCacheHits.load();
    stats.parentCacheMisses = m_statParentCacheMisses.load();
    stats.parentDbLoads = m_statParentDbLoads.load();
    stats.singleFlightCoalesced = m_statSingleFlightCoalesced.load();
    
    size_t extractCount = m_statExtractCount.load();
    if (extractCount > 0) {
        stats.avgChildFromParentExtractMs = m_statTotalExtractTimeMs.load() / extractCount;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        stats.childCacheSize = m_childGridCache.size();
    }
    {
        std::lock_guard<std::mutex> lock(m_parentGridCacheMutex);
        stats.parentCacheSize = m_parentGridCache.size();
    }
    
    return stats;
}

void BathymetryDataManager::LoadGridsWithPriority(const std::vector<GridLoadRequest>& requests) {
    m_currentGeneration++;
    uint64_t currentGen = m_currentGeneration.load();
    
    ClearChildGridLoadQueue();

    std::unordered_set<std::string> newVisibleKeys;
    
    for (const auto& request : requests) {
        if (request.type == GridLoadRequest::VISIBLE) {
            newVisibleKeys.insert(GetGridKey(request.tile));
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        m_visibleGridKeys = newVisibleKeys;
    }

    MarkObsoleteExtendedGridsForDeletion();

    int visibleCount = 0;
    int adjacentCount = 0;
    
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);

       
        
        for (const auto& request : requests) {

            std::string key = GetGridKey(request.tile);
    
            auto it = m_childGridCache.find(key);

            if (it != m_childGridCache.end()) {

                if (it->second.isLoading && it->second.grid == nullptr) {
                    it->second.isLoading = false;
                }

                if (it->second.grid && !it->second.isLoading) {
                    it->second.lastAccess = std::chrono::steady_clock::now();
            
                    if (request.type == GridLoadRequest::VISIBLE) {

                        bool isComplete = false;

                        {
                            std::lock_guard<std::mutex> lock(m_callbackMutex);
                            if (m_gridCompleteChecker) {
                                isComplete = m_gridCompleteChecker(request.tile);
                            }
                        }
    
                        if (isComplete) {
                        
                            continue;
                        }

                        bool hasSeedsAlready = false;
                        {
                            std::lock_guard<std::mutex> seedLock(m_extendedGridCacheMutex);
                            hasSeedsAlready = (m_extendedGridCache.find(key) != m_extendedGridCache.end());
                        }
                
                        if (!hasSeedsAlready) {
                            EnqueueExtendedGridCalculation(request.tile, request.priority);
                        }
                    }
            
                    continue;  // On a le grid, pas besoin de le recharger

                } else if (it->second.isLoading) {

                    continue;
                }
            }
            
            CachedChildGrid& tileData = m_childGridCache[key];
            tileData.lastAccess = std::chrono::steady_clock::now();
            // tileData.isCurrentlyVisible = (request.type == GridLoadRequest::VISIBLE);
            
            {
                std::lock_guard<std::mutex> queueLock(m_childGridLoadQueueMutex);
                m_childGridLoadQueue.push({request.tile, currentGen, request.priority, request.type});
            }
            
            if (request.type == GridLoadRequest::VISIBLE) visibleCount++;
            else if (request.type == GridLoadRequest::ADJACENT) adjacentCount++;
        }
        
        if (m_childGridCache.size() > m_maxCachedChildGrids) {
            EvictOldestChildGrids(newVisibleKeys);
        }
    }
    
    m_childGridLoadQueueCV.notify_one();
   
}

void BathymetryDataManager::ClearChildGridLoadQueue() {
    std::lock_guard<std::mutex> lock(m_childGridLoadQueueMutex);

    std::priority_queue<ChildGridLoadRequest> empty;
    m_childGridLoadQueue.swap(empty);
}

void BathymetryDataManager::ExtendedGridCalculatorThreadFunc() {
    int cleanupCounter = 0;  // NOUVEAU
    
    while (!m_extendedGridCalculatorShouldStop) {
        ExtendedGridRequest request;
        bool hasRequest = false;
        
        {
            std::unique_lock<std::mutex> lock(m_extendedGridQueueMutex);
            m_extendedGridQueueCV.wait_for(lock, std::chrono::milliseconds(100), [this] { 
                return !m_extendedGridQueue.empty() || m_extendedGridCalculatorShouldStop; 
            });
            
            if (m_extendedGridCalculatorShouldStop) break;
            
            if (!m_extendedGridQueue.empty()) {
                request = m_extendedGridQueue.top();
                m_extendedGridQueue.pop();
                hasRequest = true;
            }
        }
        
        if (hasRequest) {
            CalculateExtendedGridForTile(request.tile, request.isRecalculation);
        }
        
        cleanupCounter++;
        if (cleanupCounter >= 10) {
            CleanupMarkedExtendedGrids();
            cleanupCounter = 0;
        }
    }
}

void BathymetryDataManager::CalculateExtendedGridForTile(const Rcl& tile, bool forceRecalc) {
    std::string key = GetGridKey(tile);

    
    {
        std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
        auto it = m_extendedGridCache.find(key);
        if (it != m_extendedGridCache.end() && it->second.version == 2 && !forceRecalc) {
            return;
        }
    }
    
    std::vector<Position3D> allSeeds;
    MinMax depthRange;
    
    int totalGridsUsed = GetExtendedGridForTile(tile, allSeeds, depthRange, DpPbg::FIXED_BORDER_WIDTH);
        
    CachedExtendedGrid seeds;
    seeds.points = std::move(allSeeds);
    seeds.depthRange = depthRange;
    seeds.usedNeighborCount = totalGridsUsed-1 ;
    seeds.version = (totalGridsUsed  == 9) ? 2 : 1;
    seeds.timestamp = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
        m_extendedGridCache[key] = std::move(seeds);
    }

    if (seeds.version == 1) {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        if (IsTileInVisibleSet(key)) {
            int currentNeighbors = CountAvailableNeighbors(tile);
            if (currentNeighbors == 8) {
                EnqueueExtendedGridCalculation(tile, 0.1f, true);
            }
        }
    }

     
    NotifyExtendedGridReady(tile);
}

int BathymetryDataManager::CountAvailableNeighbors(const Rcl& tile) const {
    std::vector<Rcl> neighbors;
    GetRclNeighbors(tile, neighbors);
    
    int count = 0;
    std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
    
    for (const auto& neighbor : neighbors) {
        std::string nKey = GetGridKey(neighbor);
        auto it = m_childGridCache.find(nKey);
        if (it != m_childGridCache.end() && it->second.grid && !it->second.isLoading) {
            count++;
        }
    }
    
    return count;
}

void BathymetryDataManager::EnqueueExtendedGridCalculation(const Rcl& tile, float priority, bool isRecalc) {
    ExtendedGridRequest request;
    request.tile = tile;
    request.priority = priority;
    request.isRecalculation = isRecalc;
    
    {
        std::lock_guard<std::mutex> lock(m_extendedGridQueueMutex);
        m_extendedGridQueue.push(request);
    }
    m_extendedGridQueueCV.notify_one();
}

void BathymetryDataManager::CheckForExtendedGridUpgrade(const Rcl& loadedTile) {
    std::vector<Rcl> toUpgrade;

    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);

        auto checkKey = [&](const std::string& visibleKey) {
            Rcl visibleTile = Rcl::FromKey(visibleKey);
            if (AreNeighbors(visibleTile, loadedTile)) {
                if (CountAvailableNeighbors(visibleTile) == 8) {
                    std::lock_guard<std::mutex> seedLock(m_extendedGridCacheMutex);
                    auto it = m_extendedGridCache.find(visibleKey);
                    if (it != m_extendedGridCache.end() && it->second.version == 1) {
                        toUpgrade.push_back(visibleTile);
                    }
                }
            }
        };

        for (const auto& visibleKey : m_visibleGridKeys) {
            checkKey(visibleKey);
        }
    }
    
    for (const auto& tile : toUpgrade) {
        EnqueueExtendedGridCalculation(tile, 0.5f, true);
    }
}

bool BathymetryDataManager::HasExtendedGridReady(const Rcl& tile) const {
    std::unique_lock<std::mutex> lock(m_extendedGridCacheMutex, std::defer_lock);
    
    if (!lock.try_lock()) {
        return false;
    }
    
    std::string key = GetGridKey(tile);
    return m_extendedGridCache.find(key) != m_extendedGridCache.end();
}

CachedExtendedGrid BathymetryDataManager::GetExtendedGrid(const Rcl& tile) const {
    std::unique_lock<std::mutex> lock(m_extendedGridCacheMutex, std::defer_lock);
    
    if (!lock.try_lock()) {
        return CachedExtendedGrid();  // Retourne vide si pas dispo
    }
    
    std::string key = GetGridKey(tile);
    auto it = m_extendedGridCache.find(key);
    if (it != m_extendedGridCache.end() && !it->second.markedForDeletion) {
        return it->second;
    }
    return CachedExtendedGrid();
}

void BathymetryDataManager::NotifyExtendedGridReady(const Rcl& tile) {
    ExtendedGridReadyCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_extendedGridReadyCallback;
    }
    
    if (callback) {
        callback(tile);
    }
}

void BathymetryDataManager::MarkObsoleteExtendedGridsForDeletion() {
    std::unordered_set<std::string> activeKeys;

    // Step 1: copy the visible set
    std::unordered_set<std::string> visibleKeysCopy;
    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        visibleKeysCopy = m_visibleGridKeys;
        activeKeys = m_visibleGridKeys;
    }
    
    for (const auto& visibleKey : visibleKeysCopy) {
        Rcl visibleTile = Rcl::FromKey(visibleKey);
        std::vector<Rcl> neighbors;
        GetRclNeighbors(visibleTile, neighbors);
        
        for (const auto& neighbor : neighbors) {
            activeKeys.insert(GetGridKey(neighbor));
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
        for (auto& pair : m_extendedGridCache) {
            if (activeKeys.find(pair.first) == activeKeys.end()) {
                pair.second.markedForDeletion = true;
            } else {
                pair.second.markedForDeletion = false;
            }
        }
    }

}

void BathymetryDataManager::CleanupMarkedExtendedGrids() {
    std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
    
    size_t sizeBefore = m_extendedGridCache.size();
    int deletedCount = 0;
    int markedCount = 0;  // NOUVEAU
    
    for (const auto& pair : m_extendedGridCache) {
        if (pair.second.markedForDeletion) {
            markedCount++;
        }
    }
    
    auto it = m_extendedGridCache.begin();
    while (it != m_extendedGridCache.end()) {
        if (it->second.markedForDeletion) {
            it = m_extendedGridCache.erase(it);
            deletedCount++;
        } else {
            ++it;
        }
    }
}

void BathymetryDataManager::DeleteExtendedGrid(const Rcl& tile) {
    std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
    std::string key = GetGridKey(tile);
    
    auto it = m_extendedGridCache.find(key);
    if (it != m_extendedGridCache.end()) {
        m_extendedGridCache.erase(it);  
    }
}

bool BathymetryDataManager::UpdateGridWithPoint(const Rcl& tile16, Position3D& point) {
    if (tile16.level() != m_parentGridLevel) {
        wxLogError("UpdateGridWithPoint: Expected level %d, got %d", 
                   m_parentGridLevel, tile16.level());
        return false;
    }
    
    std::string parentKey = GetGridKey(tile16);
    
    std::unordered_set<std::string> visibleTilesCopy;
    {
        std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
        visibleTilesCopy = m_visibleGridKeys;
    }
    
    bool gridUpdated = false;
    std::shared_ptr<FloatGrid> parentGrid;
    
    {
        std::lock_guard<std::mutex> lock(m_parentGridCacheMutex);
        auto it = m_parentGridCache.find(parentKey);
        
        if (it != m_parentGridCache.end() && it->second.grid && !it->second.isLoading) {
            parentGrid = it->second.grid;
            
            Position tileTL = tile16.GetPositionTopLeft();
            Position tileBR = tile16.GetPositionBottomRight();
            
            double tileWidth = tileBR.X() - tileTL.X();
            double tileHeight = tileTL.Y() - tileBR.Y();
            
            double relX = (point.x - tileTL.X()) / tileWidth;
            double relY = (point.y - tileBR.Y()) / tileHeight;
            
            if (relX >= 0.0 && relX <= 1.0 && relY >= 0.0 && relY <= 1.0) {
                int centerGridX = static_cast<int>(std::round(relX * (DpPbg::PIXEL_TILE_SIZE - 1)));
                int centerGridY = static_cast<int>(std::round(relY * (DpPbg::PIXEL_TILE_SIZE - 1)));
                
                centerGridX = std::max(0, std::min(DpPbg::PIXEL_TILE_SIZE - 1, centerGridX));
                centerGridY = std::max(0, std::min(DpPbg::PIXEL_TILE_SIZE - 1, centerGridY));
                
              
                float currentValue = parentGrid->GetValue(centerGridX, centerGridY);
                bool needsMinMaxRecompute = false;

                if (currentValue != DpPbg::NO_DATA_VALUE) {
                    float currentMin = parentGrid->GetMin();
                    float currentMax = parentGrid->GetMax();
                    if (currentValue == currentMin || currentValue == currentMax) {
                        needsMinMaxRecompute = true;
                    }
                }

                parentGrid->SetValue(centerGridX, centerGridY, point.z);

                if (needsMinMaxRecompute) {
                    parentGrid->RecomputeMinMax();
}

                
                gridUpdated = true;
                
                UpdateParentGridLRU(parentKey);
            } else {
                wxLogError("  Point outside tile bounds!");
            }
        } else {
            // Parent not in cache - try to load from database or create new
            wxLogMessage("UpdateGridWithPoint: Creating/loading parent grid for %s", parentKey.c_str());

            // Try to load from database first
            std::vector<uint8_t> blob;
            parentGrid = std::make_shared<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);

            if (m_database.GetPbgBlob(tile16, blob) && !blob.empty()) {
                // Found in database - deserialize
                if (!PbgSeedsReaderWriter::Deserialize(blob, *parentGrid)) {
                    parentGrid->FillWithNoData();
                    wxLogMessage("UpdateGridWithPoint: Deserialization failed, using empty grid for %s", parentKey.c_str());
                }
            } else {
                // Not in database - create new empty grid
                parentGrid->FillWithNoData();
                wxLogMessage("UpdateGridWithPoint: Created new empty grid for %s", parentKey.c_str());
            }

            // Add to cache using emplace to avoid copy issues with condition_variable
            auto& cacheEntry = m_parentGridCache[parentKey];
            cacheEntry.grid = parentGrid;
            cacheEntry.lastAccess = std::chrono::steady_clock::now();
            cacheEntry.isLoading = false;
            // Add to LRU list and store iterator (CRITICAL: must be done for UpdateParentGridLRU to work)
            m_parentLRUList.push_back(parentKey);
            cacheEntry.lruIterator = std::prev(m_parentLRUList.end());

            // Evict if cache is full
            if (m_parentGridCache.size() > m_maxParentGridCacheSize) {
                EvictLRUParentGrid();
            }
            // Now update the grid with the point
            Position tileTL = tile16.GetPositionTopLeft();
            Position tileBR = tile16.GetPositionBottomRight();

            double tileWidth = tileBR.X() - tileTL.X();
            double tileHeight = tileTL.Y() - tileBR.Y();

            double relX = (point.x - tileTL.X()) / tileWidth;
            double relY = (point.y - tileBR.Y()) / tileHeight;

            if (relX >= 0.0 && relX <= 1.0 && relY >= 0.0 && relY <= 1.0) {
                int centerGridX = static_cast<int>(std::round(relX * (DpPbg::PIXEL_TILE_SIZE - 1)));
                int centerGridY = static_cast<int>(std::round(relY * (DpPbg::PIXEL_TILE_SIZE - 1)));

                centerGridX = std::max(0, std::min(DpPbg::PIXEL_TILE_SIZE - 1, centerGridX));
                centerGridY = std::max(0, std::min(DpPbg::PIXEL_TILE_SIZE - 1, centerGridY));

                parentGrid->SetValue(centerGridX, centerGridY, point.z);
                gridUpdated = true;
            }
        }
    }
    
    int invalidatedChildren = 0;
    int updatedChildren = 0;
    {
        std::lock_guard<std::mutex> childLock(m_childGridCacheMutex);
        
        for (int level = 17; level <= DpPbg::MAX_LEVEL; level++) {
            Position pos = Position::FromMercator(point.x, point.y);
            Rcl childTile = RclWorld::GetRcl(pos, level);
            std::string childKey = GetGridKey(childTile);
            
            auto it = m_childGridCache.find(childKey);
            if (it != m_childGridCache.end()) {
                if (visibleTilesCopy.find(childKey) != visibleTilesCopy.end() && parentGrid) {
                    auto newChildGrid = ExtractChildGridFromParentGrid(parentGrid, tile16, childTile);
                    if (newChildGrid) {
                        it->second.grid = newChildGrid;
                        it->second.depthRange = MinMax(newChildGrid->GetMin(), newChildGrid->GetMax());
                        it->second.lastAccess = std::chrono::steady_clock::now();
                        updatedChildren++;
                    }
                } else {
                    m_childGridCache.erase(it);
                    invalidatedChildren++;
                }
            }
        }
    }
    
    int invalidatedSeeds = 0;
    std::vector<Rcl> tilesToRecalculate;
    {
        std::lock_guard<std::mutex> seedLock(m_extendedGridCacheMutex);
        
        for (int level = DpPbg::MIN_LEVEL; level <= DpPbg::MAX_LEVEL; level++) {
            Position pos = Position::FromMercator(point.x, point.y);
            Rcl tile = RclWorld::GetRcl(pos, level);
            
            std::string key = GetGridKey(tile);
            auto it = m_extendedGridCache.find(key);
            if (it != m_extendedGridCache.end()) {
                m_extendedGridCache.erase(it);
                invalidatedSeeds++;
            }
            
            if (visibleTilesCopy.find(key) != visibleTilesCopy.end()) {
                tilesToRecalculate.push_back(tile);
            }
            
            std::vector<Rcl> neighbors;
            GetRclNeighbors(tile, neighbors);
            for (const auto& neighbor : neighbors) {
                std::string nKey = GetGridKey(neighbor);
                auto nIt = m_extendedGridCache.find(nKey);
                if (nIt != m_extendedGridCache.end()) {
                    m_extendedGridCache.erase(nIt);
                    invalidatedSeeds++;
                }
                
                if (visibleTilesCopy.find(nKey) != visibleTilesCopy.end()) {
                    tilesToRecalculate.push_back(neighbor);
                }
            }
        }
    }
    
    for (const auto& tile : tilesToRecalculate) {
        EnqueueExtendedGridCalculation(tile, 0.1f);
    }
    
  
    
    return true;
}

wxString BathymetryDataManager::GetExtendedGridDebugInfo(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_extendedGridCacheMutex);
    wxString info;
    
    std::string key = GetGridKey(tile);
    auto it = m_extendedGridCache.find(key);
    
    info << "\n=== EXTENDED GRID INFO ===\n";
    
    if (it == m_extendedGridCache.end()) {
        info << "Status: NOT IN CACHE\n";
    } else {
        const CachedExtendedGrid& eg = it->second;
        info << "Status: IN CACHE\n";
        info << "Version: " << eg.version << "\n";
        info << "Points Count: " << eg.points.size() << "\n";
        info << "Used Neighbor Count: " << eg.usedNeighborCount << "\n";
        info << "Depth Range: [" << eg.depthRange.min << ", " << eg.depthRange.max << "]\n";
        info << "Marked for Deletion: " << (eg.markedForDeletion ? "YES" : "NO") << "\n";
        
        auto now = std::chrono::steady_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - eg.timestamp).count();
        info << "Age: " << age << " seconds\n";
    }
    
    return info;
}

wxString BathymetryDataManager::GetChildGridDebugInfo(const Rcl& tile) const {
    wxString info;
    info << "\n=== CHILD GRID INFO ===\n";
    
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        std::string key = GetGridKey(tile);
        auto it = m_childGridCache.find(key);
        
        if (it == m_childGridCache.end()) {
            info << "Main Grid: NOT IN CACHE\n";
        } else {
            info << "Main Grid: IN CACHE\n";
            info << "  Has Grid: " << (it->second.grid ? "YES" : "NO") << "\n";
            info << "  Is Loading: " << (it->second.isLoading ? "YES" : "NO") << "\n";
            if (it->second.grid) {
                info << "  Grid Dimension: " << it->second.grid->GetDimension() << "x" 
                     << it->second.grid->GetDimension() << "\n";
                
                int validCount = 0;
                const std::vector<float>& data = it->second.grid->GetData();
                for (float val : data) {
                    if (val != DpPbg::NO_DATA_VALUE) {
                        validCount++;
                    }
                }
                info << "  Valid Points: " << validCount << "/" << data.size() << "\n";
                info << "  Min/Max: [" << it->second.grid->GetMin() << ", " 
                     << it->second.grid->GetMax() << "]\n";
                info << "  Is Empty: " << (it->second.grid->IsEmpty() ? "YES" : "NO") << "\n";
            }
        }
    }
    
    info << "\nNeighbors Status:\n";
    std::vector<Rcl> neighbors;
    GetRclNeighbors(tile, neighbors);
    
    int availableCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_childGridCacheMutex);
        for (int i = 0; i < 8; i++) {
            std::string nKey = GetGridKey(neighbors[i]);
            auto it = m_childGridCache.find(nKey);
            
            wxString status;
            if (it == m_childGridCache.end()) {
                status = "NOT_CACHED";
            } else if (it->second.isLoading) {
                status = "LOADING";
            } else if (it->second.grid) {
                status = "AVAILABLE";
                availableCount++;
            } else {
                status = "ERROR";
            }
            
            info << "  Neighbor " << i << " (" << nKey << "): " << status << "\n";
        }
    }
    
    info << "Total Available Neighbors: " << availableCount << "/8\n";
    
    return info;
}

bool BathymetryDataManager::IsTileMarkedVisible(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_visibleGridKeysMutex);
    std::string key = GetGridKey(tile);
    return IsTileInVisibleSet(key);
}