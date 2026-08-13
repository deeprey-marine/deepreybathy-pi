/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>
#include <queue>
#include <string>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>
#include "RclExtensions.h"
#include "PbgTexture.h"
#include "MinMax.h"

class BathymetryDataManager;

struct CachedExtendedGrid;

class PbgRenderCacheManager {
public:
    enum class TileState {
        INEXISTANTE,     
        EN_PREPARATION,  
        PARTIELLE,       
        COMPLETE,        
        OBSOLETE         
    };

   

    struct RenderParameters {
        float brushSizeFactor;

        bool operator==(const RenderParameters& other) const {
            return brushSizeFactor == other.brushSizeFactor;
        }
    };

    struct Statistics {
        size_t totalTilesInCache;
        size_t tilesPartial;
        size_t tilesComplete;
        size_t workItemsProcessedLastFrame;
        float averageWorkTimeMs;
        size_t cacheMemoryUsageMB;
    };

    using RenderFunction = std::function<bool(const Rcl& tile,
                                                PbgTexture* texture,
                                                const CachedExtendedGrid& seeds,  // NOUVEAU
                                                GLint defaultFbo
                                            )>;

    using DataAvailabilityCheck = std::function<bool(const Rcl& tile)>;
    
    PbgRenderCacheManager();
    ~PbgRenderCacheManager();

    void Initialize(size_t maxCacheSize, int maxWorkPerFrame);
    void Shutdown();

    void SetRenderFunction(RenderFunction func) { m_renderFunction = func; }
    void SetDataAvailabilityCheck(DataAvailabilityCheck func) { m_dataAvailabilityCheck = func; }

    PbgTexture* RequestGridTexture(const Rcl& tile, const RenderParameters& params);

    void NotifyViewportChange(const std::vector<Rcl>& visibleTiles,
                            const std::vector<Rcl>& adjacentTiles);

    void NotifyParameterChange(const RenderParameters& newParams);
    
    int ProcessFrame(GLint defaultFbo);

    Statistics GetStatistics() const;

    void SetMaxWorkPerFrame(int maxWork) { m_maxWorkPerFrame = maxWork; }
    int GetMaxWorkPerFrame() const { return m_maxWorkPerFrame; }

    void ClearCache();

    MinMax GetTileDepthRange(const Rcl& tile) const;

    int WatchdogCheck();

    void SetDataManager(BathymetryDataManager* dm) { m_dataManager = dm; }

    bool IsTileComplete(const Rcl& tile) const;

    void MarkTextureObsolete(const Rcl& tile);
    void InvalidateAffectedTiles(const std::vector<Rcl>& l16Tiles);

    uint32_t GetTileRenderCount(const Rcl& tile) const;

    MinMax GetVisibleDepthRange() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_visibleDepthRange;
    }

    // Compute the unioned depth range for an arbitrary tile set from cached
    // per-tile depthRange, WITHOUT mutating m_currentVisibleTiles/m_visibleDepthRange.
    // Used for per-canvas auto-range in dual-chart mode (each canvas must derive its
    // own range from its own visible tiles). Returns an invalid MinMax if no tile in
    // the set has valid cached depth data.
    MinMax ComputeDepthRangeForTiles(const std::vector<Rcl>& tiles) const;

     wxString GetTileDebugInfo(const Rcl& tile) const;

    size_t GetCacheSize() const { std::lock_guard<std::mutex> lock(m_mutex); return m_cache.size(); }

private:

    bool RenderWithStateProtection(const Rcl& tile, PbgTexture* texture, 
                                  const CachedExtendedGrid& seeds,
                                  GLint defaultFbo) ;

    BathymetryDataManager* m_dataManager;

    struct TileData {
        std::unique_ptr<PbgTexture> elevationTexture;
        Rcl tile;
        TileState state;
        RenderParameters lastParameters;
        int seedsVersion;
        std::chrono::steady_clock::time_point lastAccess;
        std::chrono::steady_clock::time_point creationTime;
        bool wasEverVisible;
        MinMax depthRange;
        uint32_t renderCount = 0;
    };

    std::unordered_map<std::string, TileData> m_cache;

    
    std::vector<Rcl> m_currentVisibleTiles;
    std::vector<Rcl> m_currentAdjacentTiles;
    Position m_viewportCenter;
    
    RenderParameters m_currentParameters;
    
    
    size_t m_maxCacheSize;
    size_t m_maxWorkPerFrame;
    
    mutable std::mutex m_mutex;
    
    std::chrono::steady_clock::time_point m_lastFrameTime;
    float m_averageWorkTime;
    size_t m_totalWorkProcessed;

    
    float CalculateTileDistanceScore(const Rcl& tile) const;
    

    
    
    void EvictOldTiles();
    
    std::string GetGridKey(const Rcl& tile) const;
    
    bool IsTileVisible(const Rcl& tile) const;
    bool IsTileAdjacent(const Rcl& tile) const;
    
    void UpdateTileState(const std::string& key, TileState newState);
    
    
    size_t EstimateCacheMemoryUsage() const;

    size_t m_lastFrameWorkItemsProcessed;
    

    RenderFunction m_renderFunction;
    DataAvailabilityCheck m_dataAvailabilityCheck;

    MinMax m_visibleDepthRange;
    void RecalculateVisibleMinMax();

};