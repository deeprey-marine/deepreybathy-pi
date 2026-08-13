/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "RealtimePointInjector.h"
#include "BathymetryDataManager.h"
#include "PbgRenderCacheManager.h"
#include "DirtyTileTracker.h"
#include "RclExtensions.h"
#include "RclWorld.h"
#include <wx/log.h>
#include <algorithm>

RealtimePointInjector::RealtimePointInjector()
    : m_dataManager(nullptr)
    , m_renderCache(nullptr)
    , m_dirtyTracker(nullptr)
    , m_shouldStop(false)
    , m_batchIntervalMs(100)
    , m_maxPointsPerBatch(100)
    , m_totalPointsInjected(0)
    , m_totalBatchesProcessed(0)
    , m_totalProcessingTimeMs(0.0)
{
    m_lastBatchTime = std::chrono::steady_clock::now();

    m_filter = std::make_unique<RealTimePointFilter>();
}

RealtimePointInjector::~RealtimePointInjector() {
    Shutdown();
}

bool RealtimePointInjector::Initialize(BathymetryDataManager* dataManager,
                                      PbgRenderCacheManager* renderCache) {
    if (!dataManager || !renderCache) {
        wxLogError("RealtimePointInjector: Invalid managers");
        return false;
    }

    m_dataManager = dataManager;
    m_renderCache = renderCache;

    m_shouldStop = false;
    m_processingThread = std::thread(&RealtimePointInjector::ProcessingThreadFunc, this);

    wxLogMessage("RealtimePointInjector: Initialized successfully, processing thread started");

    return true;
}

void RealtimePointInjector::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_shouldStop = true;
    }
    m_pendingCV.notify_all();
    
    if (m_processingThread.joinable()) {
        m_processingThread.join();
    }
    
    m_dataManager = nullptr;
    m_renderCache = nullptr;
}

void RealtimePointInjector::InjectPoint(const Position3D& point) {
    if (!m_recordingEnabled.load()) return;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingPoints.push(point);

        size_t queueSize = m_pendingPoints.size();

        // Log every 50th point to avoid spam
        if (queueSize % 50 == 1) {
            wxLogMessage("RealtimePointInjector::InjectPoint: queued point, queue size=%zu", queueSize);
        }

        if (queueSize >= m_maxPointsPerBatch) {
            m_pendingCV.notify_one();
        }
    }
}

void RealtimePointInjector::InjectPoints(const std::vector<Position3D>& points) {
    if (!m_recordingEnabled.load()) return;
    {
		
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        for (const auto& point : points) {
            m_pendingPoints.push(point);
        }
    }
    m_pendingCV.notify_one();
}

void RealtimePointInjector::FlushPendingPoints() {
    m_pendingCV.notify_one();
}

void RealtimePointInjector::ProcessingThreadFunc() {
    wxLogMessage("RealtimePointInjector: Processing thread started");

    while (!m_shouldStop) {
        std::vector<Position3D> pointsToProcess;

        {
            std::unique_lock<std::mutex> lock(m_pendingMutex);

            m_pendingCV.wait_for(lock, std::chrono::milliseconds(m_batchIntervalMs),
                [this] {
                    return !m_pendingPoints.empty() || m_shouldStop;
                });

            if (m_shouldStop) break;

            size_t count = std::min(m_pendingPoints.size(), m_maxPointsPerBatch);
            pointsToProcess.reserve(count);

            for (size_t i = 0; i < count; i++) {
                pointsToProcess.push_back(m_pendingPoints.front());
                m_pendingPoints.pop();
            }
        }

        if (!pointsToProcess.empty()) {
            wxLogMessage("RealtimePointInjector: Processing batch of %zu points", pointsToProcess.size());
            auto startTime = std::chrono::high_resolution_clock::now();
            
            InjectionResult result = ProcessBatch(pointsToProcess);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            double processingMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_totalPointsInjected += pointsToProcess.size();
                m_totalBatchesProcessed++;
                m_totalProcessingTimeMs += processingMs;
                m_lastBatchTime = std::chrono::steady_clock::now();
            }
            
            if (m_injectionCompleteCallback) {
                m_injectionCompleteCallback(result);
            }
            
            if (result.success && m_refreshCallback) {
                wxTheApp->CallAfter([this]() {
                    if (m_refreshCallback) {
                        m_refreshCallback();
                    }
                });
            }
        }
    }
}

std::unordered_set<Rcl, RclHash> RealtimePointInjector::IdentifyAffectedTiles(
    const std::vector<Position3D>& points) {
    
    std::unordered_set<Rcl, RclHash> affectedTiles;
    
    for (const auto& point : points) {
        for (int level = 0; level <= DpPbg::MAX_LEVEL; level++) {
            Rcl tile = GetTileForPoint(point, level);
            affectedTiles.insert(tile);
        }
    }
    
    return affectedTiles;
}

Rcl RealtimePointInjector::GetTileForPoint(const Position3D& point, int level) const {
    Position pos = Position::FromMercator(point.x, point.y);
    return RclWorld::GetRcl(pos, level);
}

std::vector<Rcl> RealtimePointInjector::GetChildTilesAtLevel(
    const Rcl& parentTile, const Position3D& point, int targetLevel) const {
    
    std::vector<Rcl> result;
    
    if (targetLevel <= parentTile.level()) {
        return result;
    }
    
    int levelDiff = targetLevel - parentTile.level();
    int factor = 1 << levelDiff;
    
    Position pos = Position::FromMercator(point.x, point.y);
    Rcl targetTile = RclWorld::GetRcl(pos, targetLevel);
    
    result.push_back(targetTile);
    return result;
}

RealtimePointInjector::Statistics RealtimePointInjector::GetStatistics() const {
    Statistics stats;
    
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        stats.totalPointsInjected = m_totalPointsInjected;
        stats.totalBatchesProcessed = m_totalBatchesProcessed;
        stats.averageBatchProcessingTimeMs = m_totalBatchesProcessed > 0 ? 
            m_totalProcessingTimeMs / m_totalBatchesProcessed : 0.0;
        stats.lastBatchTime = m_lastBatchTime;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        stats.pendingPointsCount = m_pendingPoints.size();
    }
    
    stats.filterStats = m_filter->GetStatistics();
    
    return stats;
}

RealtimePointInjector::InjectionResult RealtimePointInjector::ProcessBatch(
    const std::vector<Position3D>& points) {

    wxLogMessage("RealtimePointInjector::ProcessBatch: Starting with %zu points", points.size());

    InjectionResult result;
    result.success = true;
    result.rejectedPointsCount = 0;

    try {
        std::vector<Position3D> acceptedPoints;
        acceptedPoints.reserve(points.size());

        // Get current NMEA SOG for speed filter (instead of calculating from GPS positions)
        double currentSpeed = GetCurrentSpeedKnots();

        // Filter points
        for (const auto& point : points) {
            auto filterResult = m_filter->ShouldAcceptPoint(point, currentSpeed);

            if (!filterResult.accepted) {
                result.rejectedPointsCount++;
                wxLogMessage("RealtimePointInjector::ProcessBatch: Point REJECTED - reason: %d, point(%.2f, %.2f, %.2f)",
                    static_cast<int>(filterResult.reason), point.x, point.y, point.z);
                continue;
            }

            acceptedPoints.push_back(point);
        }

        wxLogMessage("RealtimePointInjector::ProcessBatch: %zu accepted, %zu rejected",
            acceptedPoints.size(), result.rejectedPointsCount);

        if (acceptedPoints.empty()) {
            wxLogMessage("RealtimePointInjector::ProcessBatch: All points rejected, returning");
            return result;
        }
        
        auto affectedTiles = IdentifyAffectedTiles(acceptedPoints);
        result.affectedTilesCount = affectedTiles.size();
        
        for (const auto& point : acceptedPoints) {
            Position3D mutablePoint = point;
            UpdateFloatGridHierarchy(mutablePoint, result);
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        wxLogError("RealtimePointInjector::ProcessBatch error: %s", e.what());
    }
    
    return result;
}

void RealtimePointInjector::SetCurrentSpeedKnots(double speedKnots) {
    std::lock_guard<std::mutex> lock(m_speedMutex);
    m_currentSpeedKnots = speedKnots;
}

double RealtimePointInjector::GetCurrentSpeedKnots() const {
    std::lock_guard<std::mutex> lock(m_speedMutex);
    return m_currentSpeedKnots;
}

void RealtimePointInjector::UpdateFloatGridHierarchy(Position3D& point, InjectionResult& result) {
    Rcl tile16 = GetTileForPoint(point, DpPbg::DEFAULT_IMPORT_LEVEL);

    wxLogMessage("RealtimePointInjector::UpdateFloatGridHierarchy: point(%.6f, %.6f, %.2f) -> tile(%d,%d,%d)",
        point.x, point.y, point.z, tile16.row(), tile16.column(), tile16.level());

    bool updated = m_dataManager->UpdateGridWithPoint(tile16, point);

    wxLogMessage("RealtimePointInjector::UpdateFloatGridHierarchy: UpdateGridWithPoint returned %s",
        updated ? "true" : "false");

    if (updated) {
        result.updatedFloatGridsCount++;

        // Mark tile as dirty for async persistence
        if (m_dirtyTracker) {
            m_dirtyTracker->MarkDataDirty(tile16);
        }

        for (int level = DpPbg::MIN_LEVEL; level <= DpPbg::MAX_LEVEL; level++) {
            Rcl tile = GetTileForPoint(point, level);
            m_renderCache->MarkTextureObsolete(tile);

            std::vector<Rcl> neighbors;
            BathymetryDataManager::GetRclNeighbors(tile, neighbors);
            for (const auto& neighbor : neighbors) {
                m_renderCache->MarkTextureObsolete(neighbor);
            }
        }
    }
}