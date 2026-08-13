/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once

#include <vector>
#include <queue>
#include <unordered_set>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <atomic>
#include "Position.h"
#include "Rcl.h"
#include "DpPbg.h"
#include "RealTimePointFilter.h"

class BathymetryDataManager;
class PbgRenderCacheManager;
class DirtyTileTracker;

class RealtimePointInjector {
public:
    struct InjectionResult {
        int affectedTilesCount;
        int updatedFloatGridsCount;
        int rejectedPointsCount;
        bool success;
        std::string errorMessage;
    };

    using RefreshCallback = std::function<void()>;
    using InjectionCompleteCallback = std::function<void(const InjectionResult&)>;

    RealtimePointInjector();
    ~RealtimePointInjector();

    bool Initialize(BathymetryDataManager* dataManager, PbgRenderCacheManager* renderCache);
    void Shutdown();

    void InjectPoint(const Position3D& point);
    void InjectPoints(const std::vector<Position3D>& points);

    // Configuration
    void SetBatchInterval(int milliseconds) { m_batchIntervalMs = milliseconds; }
    void SetMaxPointsPerBatch(size_t max) { m_maxPointsPerBatch = max; }
    void SetRefreshCallback(RefreshCallback callback) { m_refreshCallback = callback; }
    void SetInjectionCompleteCallback(InjectionCompleteCallback callback) { m_injectionCompleteCallback = callback; }
    void SetDirtyTileTracker(DirtyTileTracker* tracker) { m_dirtyTracker = tracker; }

    // Recording control (when disabled, InjectPoint/InjectPoints are no-ops)
    void SetRecordingEnabled(bool enabled) { m_recordingEnabled.store(enabled); }
    bool IsRecordingEnabled() const { return m_recordingEnabled.load(); }

    // Set current vessel speed from NMEA SOG (used by speed filter instead of GPS position calculation)
    void SetCurrentSpeedKnots(double speedKnots);
    double GetCurrentSpeedKnots() const;

    void FlushPendingPoints();

    struct Statistics {
        size_t totalPointsInjected;
        size_t totalBatchesProcessed;
        double averageBatchProcessingTimeMs;
        size_t pendingPointsCount;
        std::chrono::steady_clock::time_point lastBatchTime;
        RealTimePointFilter::Statistics filterStats;
    };
    Statistics GetStatistics() const;

     RealTimePointFilter* GetFilter() { return m_filter.get(); }
    const RealTimePointFilter* GetFilter() const { return m_filter.get(); }

    RealTimePointFilter::Statistics GetFilterStatistics() const {
        return m_filter ? m_filter->GetStatistics() : RealTimePointFilter::Statistics();
    }

private:
    struct TileUpdate {
        Rcl tile;
        std::vector<Position3D> points;
        std::chrono::steady_clock::time_point timestamp;
    };

    void ProcessingThreadFunc();
    
    InjectionResult ProcessBatch(const std::vector<Position3D>& points);

    // Processing steps
    std::unordered_set<Rcl, RclHash> IdentifyAffectedTiles(const std::vector<Position3D>& points);
    void UpdateFloatGridHierarchy(Position3D& point, InjectionResult& result);

    // Utilities
    Rcl GetTileForPoint(const Position3D& point, int level) const;
    std::vector<Rcl> GetChildTilesAtLevel(const Rcl& parentTile, const Position3D& point, int targetLevel) const;

<<<<<<< HEAD
=======
    // Membres
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
    BathymetryDataManager* m_dataManager;
    PbgRenderCacheManager* m_renderCache;
    DirtyTileTracker* m_dirtyTracker;

    std::queue<Position3D> m_pendingPoints;
    mutable std::mutex m_pendingMutex;
    std::condition_variable m_pendingCV;

    std::thread m_processingThread;
    std::atomic<bool> m_shouldStop;

    // Configuration
    int m_batchIntervalMs;
    size_t m_maxPointsPerBatch;

    // Callbacks
    RefreshCallback m_refreshCallback;
    InjectionCompleteCallback m_injectionCompleteCallback;

    mutable std::mutex m_statsMutex;
    size_t m_totalPointsInjected;
    size_t m_totalBatchesProcessed;
    double m_totalProcessingTimeMs;
    std::chrono::steady_clock::time_point m_lastBatchTime;

    std::unique_ptr<RealTimePointFilter> m_filter;

    // Current vessel speed from NMEA SOG (protected by mutex)
    mutable std::mutex m_speedMutex;
    double m_currentSpeedKnots{-1.0};  // -1.0 means no valid speed, filter will calculate from GPS

    std::atomic<bool> m_recordingEnabled{false};
};