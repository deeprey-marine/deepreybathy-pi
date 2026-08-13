/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <functional>
#include <list>
#include <atomic>
#include "Rcl.h"
#include "FloatGrid.h"
#include "Position.h"
#include "PbgTileDatabase.h"
#include "DpPbg.h"

class DirtyTileTracker;
class AsyncPersistenceManager;

struct GridLoadRequest {
    Rcl tile;
    float priority;      // 0 = centre, valeurs hautes = loin
    
    enum Type {
        VISIBLE,         // Grid visible
        ADJACENT,
        PRELOAD
    } type;
    
    bool operator<(const GridLoadRequest& other) const {
        return priority < other.priority;
    }
};

 struct CachedExtendedGrid {
        std::vector<Position3D> points;
        MinMax depthRange;
        int usedNeighborCount;
        int version;
        std::chrono::steady_clock::time_point timestamp;
        bool markedForDeletion = false;
    };

class BathymetryDataManager {
public:
    BathymetryDataManager();
    ~BathymetryDataManager();
    
    bool Initialize();
    void Shutdown();
    
   
    void LoadGridsWithPriority(const std::vector<GridLoadRequest>& requests);
  
 
    bool IsChildGridAvailable(const Rcl& tile) const;
    std::shared_ptr<FloatGrid> GetChildGrid(const Rcl& tile) const;
    void ClearCache();
    
    void SetMaxCachedChildGrids(size_t max) { m_maxCachedChildGrids = max; }

    void SetMaxParentGridCacheSize(size_t max) { m_maxParentGridCacheSize = max; }
    
    bool IsInitialized() const { return m_initialized; }
    MinMax GetGlobalDepthRange() const { return m_globalDepthRange; }
        
    void InvalidateParentGrid(const Rcl& parentTile);

    // Invalidate caches for pyramid tiles after incremental pyramid build (L15 down to minLevel)
    void InvalidateCachesForPyramidBuild(const std::vector<Rcl>& l16Tiles, int minLevel);
    
    
    static void GetRclNeighbors(const Rcl& rcl, std::vector<Rcl>& neighbours);
    static bool AreNeighbors(const Rcl& tile1, const Rcl& tile2);
    
    bool UpdateGridWithPoint(const Rcl& tile16, Position3D& point);

    // For AsyncPersistenceManager: get parent grid for flushing to DB
    std::shared_ptr<FloatGrid> GetParentGridForFlush(const Rcl& tile16) const;

    // Access to database for persistence
    PbgTileDatabase& GetDatabase() { return m_database; }

    // For eviction hook - flush dirty tiles before eviction
    void SetDirtyTileTracker(DirtyTileTracker* tracker) { m_dirtyTracker = tracker; }
    void SetAsyncPersistenceManager(AsyncPersistenceManager* manager) { m_persistenceManager = manager; }

    struct CacheStats {
        size_t parentCacheHits = 0;
        size_t parentCacheMisses = 0;
        size_t parentDbLoads = 0;
        size_t singleFlightCoalesced = 0;
        double avgChildFromParentExtractMs = 0.0;
        size_t childCacheSize = 0;
        size_t parentCacheSize = 0;
    };
    CacheStats GetStats() const;

    wxString GetExtendedGridDebugInfo(const Rcl& tile) const;
    wxString GetChildGridDebugInfo(const Rcl& tile) const;
    bool IsTileMarkedVisible(const Rcl& tile) const;
    
private:

    int GetExtendedGridForTile(const Rcl& tile, 
                                std::vector<Position3D>& allPoints,
                                MinMax& depthRange,
                                int borderWidth);

    struct ChildGridLoadRequest {
        Rcl tile;
        uint64_t generation;
        float priority;
        GridLoadRequest::Type type;
        
        bool operator<(const ChildGridLoadRequest& other) const {
            if (generation != other.generation)
                return generation < other.generation;
            return priority > other.priority;
        }
    };

    std::priority_queue<ChildGridLoadRequest> m_childGridLoadQueue;
    std::atomic<uint64_t> m_currentGeneration{0};
    void ClearChildGridLoadQueue();

    struct CachedChildGrid {
        std::shared_ptr<FloatGrid> grid;
        MinMax depthRange;
        std::chrono::steady_clock::time_point lastAccess;
        bool isLoading = false;
        bool isCurrentlyVisible = false;
    };
    
    struct CachedParentGrid {
        std::shared_ptr<FloatGrid> grid;
        std::chrono::steady_clock::time_point lastAccess;
        bool isLoading = false;
        std::condition_variable loadCondition;
        
        std::list<std::string>::iterator lruIterator;
    };
    
    void GridLoaderThreadFunc();
    void LoadGrid(const Rcl& tile, float priority);
    void EnqueueGridLoading(const Rcl& tile);  // Prend m_childGridCacheMutex
    void EvictOldestChildGrids(const std::unordered_set<std::string>& visibleKeys);
    void EvictLRUParentGrid();

    
    std::string GetGridKey(const Rcl& tile) const;
    Rcl GetParentGridAtLevelH(const Rcl& tile) const;
    bool LoadParentGridFromDatabase(const Rcl& parentTile, std::shared_ptr<FloatGrid>& grid);
    std::shared_ptr<FloatGrid> ExtractChildGridFromParentGrid(
        const std::shared_ptr<FloatGrid>& parentGrid,
        const Rcl& parentTile,
        const Rcl& childTile);
    
    void UpdateParentGridLRU(const std::string& key);
    
    // Caches
    mutable std::mutex m_childGridCacheMutex;
    std::unordered_map<std::string, CachedChildGrid> m_childGridCache;
    
    mutable std::mutex m_parentGridCacheMutex;
    std::unordered_map<std::string, CachedParentGrid> m_parentGridCache;
    std::list<std::string> m_parentLRUList;
    
    std::mutex m_childGridLoadQueueMutex;
    std::condition_variable m_childGridLoadQueueCV;

    
    std::thread m_gridLoaderThread;
    std::atomic<bool> m_gridLoaderShouldStop;
    
       
    // Configuration
    size_t m_maxCachedChildGrids;
    size_t m_maxParentGridCacheSize;

    int m_parentGridLevel;
    
    PbgTileDatabase m_database;
    bool m_initialized;
    MinMax m_globalDepthRange;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    
    mutable std::atomic<size_t> m_statParentCacheHits{0};
    mutable std::atomic<size_t> m_statParentCacheMisses{0};
    mutable std::atomic<size_t> m_statParentDbLoads{0};
    mutable std::atomic<size_t> m_statSingleFlightCoalesced{0};
    mutable std::atomic<double> m_statTotalExtractTimeMs{0.0};
    mutable std::atomic<size_t> m_statExtractCount{0};

    std::unordered_set<std::string> m_visibleGridKeys;      // Visible tile keys
    mutable std::mutex m_visibleGridKeysMutex;

    // Must be called with m_visibleGridKeysMutex held
    bool IsTileInVisibleSet(const std::string& key) const {
        return m_visibleGridKeys.count(key) > 0;
    }

    public:
   
    
    bool HasExtendedGridReady(const Rcl& tile) const;
    CachedExtendedGrid GetExtendedGrid(const Rcl& tile) const;
    
    using ExtendedGridReadyCallback = std::function<void(const Rcl&)>;
    void SetExtendedGridReadyCallback(ExtendedGridReadyCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_extendedGridReadyCallback = callback;
    }

    void DeleteExtendedGrid(const Rcl& tile);

   

    using GridCompleteChecker = std::function<bool(const Rcl&)>;

    void SetGridCompleteChecker(GridCompleteChecker checker) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_gridCompleteChecker = checker;
        }

private:

    GridCompleteChecker m_gridCompleteChecker;

    // For eviction hook - flush dirty tiles before eviction
    DirtyTileTracker* m_dirtyTracker = nullptr;
    AsyncPersistenceManager* m_persistenceManager = nullptr;

    mutable std::mutex m_extendedGridCacheMutex;
    std::unordered_map<std::string, CachedExtendedGrid> m_extendedGridCache;
    
    struct ExtendedGridRequest {
        Rcl tile;
        float priority;
        bool isRecalculation;
    
        bool operator<(const ExtendedGridRequest& other) const {
            return priority > other.priority;
        }
    };

    std::priority_queue<ExtendedGridRequest> m_extendedGridQueue;
    std::mutex m_extendedGridQueueMutex;
    std::condition_variable m_extendedGridQueueCV;
    
    std::thread m_extendedGridCalculatorThread;
    std::atomic<bool> m_extendedGridCalculatorShouldStop{false};
    
    // Callback
    ExtendedGridReadyCallback m_extendedGridReadyCallback;
    
    void ExtendedGridCalculatorThreadFunc();
    void CalculateExtendedGridForTile(const Rcl& tile, bool forceRecalc);
    void EnqueueExtendedGridCalculation(const Rcl& tile, float priority, bool isRecalc = false);
    void NotifyExtendedGridReady(const Rcl& tile);
    int CountAvailableNeighbors(const Rcl& tile) const;
    void CheckForExtendedGridUpgrade(const Rcl& loadedTile);
    void MarkObsoleteExtendedGridsForDeletion();
    void CleanupMarkedExtendedGrids();
    
  
};