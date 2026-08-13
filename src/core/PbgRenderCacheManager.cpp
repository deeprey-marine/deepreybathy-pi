/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgRenderCacheManager.h"
#include "DpPbg.h"
#include <algorithm>
#include <unordered_set>
#include <wx/log.h>
#include "BathymetryDataManager.h"

PbgRenderCacheManager::PbgRenderCacheManager()
    : m_maxCacheSize(DpPbg::DEFAULT_MAX_CACHED_TEXTURE)
    , m_maxWorkPerFrame(DpPbg::MAX_TILES_PER_FRAME)
    , m_averageWorkTime(0.0f)
    , m_totalWorkProcessed(0)
    , m_viewportCenter(Position::FromMercator(0.0, 0.0))
    , m_lastFrameWorkItemsProcessed(0)
    , m_visibleDepthRange(DpPbg::MIN_DEPTH, DpPbg::MAX_DEPTH)
{
    m_lastFrameTime = std::chrono::steady_clock::now();
}

PbgRenderCacheManager::~PbgRenderCacheManager() {
    Shutdown();
}

void PbgRenderCacheManager::Initialize(size_t maxCacheSize, int maxWorkPerFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxCacheSize = maxCacheSize;
    m_maxWorkPerFrame = maxWorkPerFrame;
    m_cache.clear();

}

void PbgRenderCacheManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

PbgTexture* PbgRenderCacheManager::RequestGridTexture(const Rcl& tile, const RenderParameters& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    
    if (it == m_cache.end()) {
        return nullptr;
    }
    
    TileData& tileData = it->second;
    tileData.lastAccess = std::chrono::steady_clock::now();
    
    if (!(tileData.lastParameters == params)) {
        if (tileData.state != TileState::OBSOLETE && tileData.state != TileState::EN_PREPARATION) {
            tileData.state = TileState::OBSOLETE;
        }
    }
    
    if (tileData.state == TileState::PARTIELLE || 
        tileData.state == TileState::COMPLETE ||
        tileData.state == TileState::OBSOLETE) {
        return tileData.elevationTexture.get();
    }
    
    return nullptr;
}

void PbgRenderCacheManager::NotifyViewportChange(const std::vector<Rcl>& visibleTiles, 
                                                const std::vector<Rcl>& adjacentTiles) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_currentVisibleTiles = visibleTiles;
    m_currentAdjacentTiles = adjacentTiles;
    
    RecalculateVisibleMinMax();
}

void PbgRenderCacheManager::NotifyParameterChange(const RenderParameters& newParams) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (newParams == m_currentParameters) {
        return;
    }
    
    m_currentParameters = newParams;
        
    for (auto& pair : m_cache) {
        if (pair.second.state != TileState::EN_PREPARATION) {
            pair.second.state = TileState::OBSOLETE;
        }
    }
}

int PbgRenderCacheManager::ProcessFrame(GLint defaultFbo) {
    if (!m_dataManager) return 0;

    int rendered = 0;
    int tilesChecked = 0;
    
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        for (const auto& tile : m_currentVisibleTiles) {

            if (rendered >= m_maxWorkPerFrame) {
                break;
            }

            std::string key = GetGridKey(tile);
            auto cacheIt = m_cache.find(key);

            
            
            if (cacheIt != m_cache.end() && 
                cacheIt->second.state == TileState::COMPLETE &&
                cacheIt->second.lastParameters == m_currentParameters) {
                continue;
            }

            if (!m_dataManager->HasExtendedGridReady(tile)) {
                continue;
            }

            auto seeds = m_dataManager->GetExtendedGrid(tile);
            if (seeds.points.empty()) {
                continue;  // Pas de seeds, on passe
            }
            
            PbgTexture* texture = nullptr;
            
            if (cacheIt == m_cache.end()) {
                TileData& tileData = m_cache[key];
                tileData.elevationTexture = std::make_unique<PbgTexture>(
                    DpPbg::PADDED_TILE_SIZE, DpPbg::PADDED_TILE_SIZE, true);
                
                if (!tileData.elevationTexture->Initialize()) {
                    m_cache.erase(key);
                    continue;
                }
                
                tileData.tile = tile;
                tileData.state = TileState::EN_PREPARATION;
                tileData.seedsVersion = 0;
                tileData.creationTime = std::chrono::steady_clock::now();
                tileData.lastAccess = tileData.creationTime;
                tileData.lastParameters = m_currentParameters;
                texture = tileData.elevationTexture.get();
            } else {
                if (!(cacheIt->second.lastParameters == m_currentParameters) ||
                    seeds.version > cacheIt->second.seedsVersion ||
                    cacheIt->second.state == TileState::OBSOLETE) {
                    cacheIt->second.state = TileState::EN_PREPARATION;
                    cacheIt->second.lastParameters = m_currentParameters;
                    texture = cacheIt->second.elevationTexture.get();
                } else {
                    continue;
                }
            }
            
            bool success = false;
            if (m_renderFunction && texture) {
                lock.unlock();
                success = RenderWithStateProtection(tile, texture, seeds, defaultFbo);
                lock.lock();
            }
            
            if (success) {
                auto it = m_cache.find(key);
                if (it != m_cache.end()) {
                    it->second.state = (seeds.version == 2) ? 
                        TileState::COMPLETE : TileState::PARTIELLE;
                    it->second.seedsVersion = seeds.version;
                    it->second.depthRange = seeds.depthRange;
                    it->second.renderCount++;
                    if (IsTileVisible(tile)) {
                        RecalculateVisibleMinMax();
                    }
                    it->second.lastAccess = std::chrono::steady_clock::now();
                    rendered++;
                }
                
                if (seeds.version == 2) {
                    lock.unlock();
                    m_dataManager->DeleteExtendedGrid(tile);
                    lock.lock();
                }
            }
            
            tilesChecked++;
            if (tilesChecked > 20) {
                break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cache.size() > m_maxCacheSize) {
            EvictOldTiles();
        }
    }

    return rendered;
}

float PbgRenderCacheManager::CalculateTileDistanceScore(const Rcl& tile) const {
    Position tileCenter = tile.GetPositionCenter();
    
    double dx = tileCenter.X() - m_viewportCenter.X();
    double dy = tileCenter.Y() - m_viewportCenter.Y();
    double distance = std::sqrt(dx * dx + dy * dy);
    
    double tileSize = tile.GetPositionBottomRight().X() - tile.GetPositionTopLeft().X();
    double normalizedDistance = distance / tileSize;
    
    return std::max(0.0f, 10.0f - static_cast<float>(normalizedDistance));
}

void PbgRenderCacheManager::EvictOldTiles() {
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> candidates;
    
    for (const auto& pair : m_cache) {
        if (pair.second.state == TileState::EN_PREPARATION) {
            continue;
        }
        
        if (!IsTileVisible(pair.second.tile) &&
            !IsTileAdjacent(pair.second.tile)) {
            candidates.push_back({pair.first, pair.second.lastAccess});
        }
    }
    
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    
    size_t toRemove = m_cache.size() > m_maxCacheSize ? 
                      m_cache.size() - m_maxCacheSize : 0;
    
    for (size_t i = 0; i < toRemove && i < candidates.size(); i++) {
        m_cache.erase(candidates[i].first);
    }
}

std::string PbgRenderCacheManager::GetGridKey(const Rcl& tile) const {
    return RclExtensions::RclToKey(tile);
}

bool PbgRenderCacheManager::IsTileVisible(const Rcl& tile) const {
    return std::find(m_currentVisibleTiles.begin(),
                    m_currentVisibleTiles.end(),
                    tile) != m_currentVisibleTiles.end();
}

bool PbgRenderCacheManager::IsTileAdjacent(const Rcl& tile) const {
    return std::find(m_currentAdjacentTiles.begin(), 
                    m_currentAdjacentTiles.end(), 
                    tile) != m_currentAdjacentTiles.end();
}

void PbgRenderCacheManager::UpdateTileState(const std::string& key, TileState newState) {
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->second.state = newState;
    }
}

size_t PbgRenderCacheManager::EstimateCacheMemoryUsage() const {
    size_t totalBytes = 0;
    
    for (const auto& pair : m_cache) {
        if (pair.second.elevationTexture) {
            totalBytes += DpPbg::PADDED_TILE_SIZE * DpPbg::PADDED_TILE_SIZE * sizeof(float);
        }
    }
    
    return totalBytes / (1024 * 1024);
}

PbgRenderCacheManager::Statistics PbgRenderCacheManager::GetStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    Statistics stats;
    stats.totalTilesInCache = m_cache.size();
    stats.tilesPartial = 0;
    stats.tilesComplete = 0;
    
    for (const auto& pair : m_cache) {
        if (pair.second.state == TileState::PARTIELLE) {
            stats.tilesPartial++;
        } else if (pair.second.state == TileState::COMPLETE) {
            stats.tilesComplete++;
        }
    }
    
    stats.workItemsProcessedLastFrame = 0;
    stats.averageWorkTimeMs = m_averageWorkTime;
    stats.cacheMemoryUsageMB = EstimateCacheMemoryUsage();
    
    return stats;
}

void PbgRenderCacheManager::ClearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

MinMax PbgRenderCacheManager::GetTileDepthRange(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    
    if (it != m_cache.end() && 
        (it->second.state == TileState::COMPLETE || 
         it->second.state == TileState::PARTIELLE)) {
        return it->second.depthRange;
    }
    
    return MinMax(DpPbg::NO_DATA_VALUE, DpPbg::NO_DATA_VALUE);
}

int PbgRenderCacheManager::WatchdogCheck() {
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int obsoleteCount = 0;
    
    for (const auto& tile : m_currentVisibleTiles) {
        std::string key = GetGridKey(tile);
        auto it = m_cache.find(key);
        
        if (it != m_cache.end() && it->second.state == TileState::OBSOLETE) {
            obsoleteCount++;
        }
    }
    
    return obsoleteCount;
}

bool PbgRenderCacheManager::IsTileComplete(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    
    return it != m_cache.end() && 
           it->second.state == TileState::COMPLETE &&
           it->second.seedsVersion == 2 &&
           it->second.lastParameters == m_currentParameters;
}

MinMax PbgRenderCacheManager::ComputeDepthRangeForTiles(const std::vector<Rcl>& tiles) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    MinMax result;          // default-constructed => invalid (min > max)
    bool hasValidTile = false;

    for (const Rcl& tile : tiles) {
        std::string key = GetGridKey(tile);
        auto it = m_cache.find(key);

        if (it != m_cache.end() &&
            it->second.depthRange.min != DpPbg::NO_DATA_VALUE &&
            it->second.depthRange.max != DpPbg::NO_DATA_VALUE) {

            if (!hasValidTile) {
                result = it->second.depthRange;
                hasValidTile = true;
            } else {
                result.Union(it->second.depthRange);
            }
        }
    }

    return result;  // invalid (IsValid()==false) if no valid tile found
}

void PbgRenderCacheManager::RecalculateVisibleMinMax() {
    
    MinMax newMinMax(DpPbg::MAX_DEPTH, DpPbg::MIN_DEPTH);
    bool hasValidTile = false;
    
    for (const Rcl& tile : m_currentVisibleTiles) {
        std::string key = GetGridKey(tile);
        auto it = m_cache.find(key);
        
        if (it != m_cache.end() && 
            it->second.depthRange.min != DpPbg::NO_DATA_VALUE &&
            it->second.depthRange.max != DpPbg::NO_DATA_VALUE) {
            
            if (!hasValidTile) {
                newMinMax = it->second.depthRange;
                hasValidTile = true;
            } else {
                newMinMax.Union(it->second.depthRange);
            }
        }
    }
    
    if (hasValidTile) {
        m_visibleDepthRange = newMinMax;
    }
}

uint32_t PbgRenderCacheManager::GetTileRenderCount(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second.renderCount;
    return 0;
}

void PbgRenderCacheManager::InvalidateAffectedTiles(const std::vector<Rcl>& l16Tiles) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build set of affected keys at L0-L16
    std::unordered_set<std::string> affectedKeys;
    std::unordered_set<std::string> l16Keys;
    for (const auto& tile : l16Tiles) {
        std::string k = GetGridKey(tile);
        affectedKeys.insert(k);
        l16Keys.insert(k);
        Rcl current = tile.Parent();
        while (current.Level() >= 0) {
            affectedKeys.insert(GetGridKey(current));
            if (current.Level() == 0) break;
            current = current.Parent();
        }
    }

    // Remove only affected tiles from cache
    int removed = 0;
    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        bool affected = false;
        if (it->second.tile.Level() <= 16) {
            affected = affectedKeys.count(it->first) > 0;
        } else {
            // For L17+: walk up to L16 ancestor
            Rcl parent = it->second.tile;
            while (parent.Level() > 16) parent = parent.Parent();
            affected = l16Keys.count(GetGridKey(parent)) > 0;
        }
        if (affected) {
            it = m_cache.erase(it);
            removed++;
        } else {
            ++it;
        }
    }

    wxLogMessage("InvalidateAffectedTiles: removed %d textures for %zu affected L16 tiles",
                 removed, l16Tiles.size());
}

void PbgRenderCacheManager::MarkTextureObsolete(const Rcl& tile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    
    if (it != m_cache.end()) {
        if (it->second.state != TileState::EN_PREPARATION) {
            it->second.state = TileState::OBSOLETE;
        }
    }
}

bool PbgRenderCacheManager::RenderWithStateProtection(const Rcl& tile, PbgTexture* texture, 
                                  const CachedExtendedGrid& seeds,
                                  GLint defaultFbo) {
    GLint savedActiveTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTexture);
    GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLfloat savedClearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, savedClearColor);
    GLint savedViewport[4];
    glGetIntegerv(GL_VIEWPORT, savedViewport);
        
    bool success = m_renderFunction(tile, texture, seeds, defaultFbo);
        
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
    glActiveTexture(savedActiveTexture);
    if (savedDepthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glClearColor(savedClearColor[0], savedClearColor[1], 
                savedClearColor[2], savedClearColor[3]);
        
    return success;
}

wxString PbgRenderCacheManager::GetTileDebugInfo(const Rcl& tile) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    wxString info;
    
    std::string key = GetGridKey(tile);
    auto it = m_cache.find(key);
    
    info << "=== TEXTURE CACHE INFO ===\n";
    
    if (it == m_cache.end()) {
        info << "Status: NOT IN CACHE\n";
    } else {
        const TileData& data = it->second;
        info << "Status: IN CACHE\n";
        
        wxString stateStr;
        switch(data.state) {
            case TileState::EN_PREPARATION: stateStr = "EN_PREPARATION"; break;
            case TileState::PARTIELLE: stateStr = "PARTIELLE"; break;
            case TileState::COMPLETE: stateStr = "COMPLETE"; break;
            case TileState::OBSOLETE: stateStr = "OBSOLETE"; break;
        }
        info << "State: " << stateStr << "\n";
        info << "Seeds Version: " << data.seedsVersion << "\n";
        info << "Depth Range: [" << data.depthRange.min << ", " << data.depthRange.max << "]\n";
        
        if (data.elevationTexture) {
            info << "Texture: " << data.elevationTexture->GetWidth() << "x" 
                 << data.elevationTexture->GetHeight() << "\n";
        } else {
            info << "Texture: NULL\n";
        }
        
        auto now = std::chrono::steady_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - data.creationTime).count();
        info << "Age: " << age << " seconds\n";
    }
    
    return info;
}