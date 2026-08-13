/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef ASYNC_PERSISTENCE_MANAGER_H
#define ASYNC_PERSISTENCE_MANAGER_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include "Rcl.h"

class DirtyTileTracker;
class BathymetryDataManager;
class PbgTileDatabase;

/**
 * AsyncPersistenceManager - Handles async DB writes for real-time point injection
 *
 * Runs a background thread that periodically flushes dirty tiles to the database.
 * Also provides synchronous flush for eviction scenarios.
 *
 * Design:
 * - Periodic flush every FLUSH_INTERVAL_MS (5 seconds default)
 * - Batch writes for performance (single transaction)
 * - Marks parent levels as pyramid-dirty after L16 flush
 * - Synchronous flush available for cache eviction scenarios
 */
class AsyncPersistenceManager {
public:
    AsyncPersistenceManager(DirtyTileTracker& dirtyTracker,
                           BathymetryDataManager& dataManager,
                           PbgTileDatabase& database);
    ~AsyncPersistenceManager();

    // Start/stop the flush thread
    void Start();
    void Stop();  // Blocks until final flush complete

    // Request immediate flush (wakes up flush thread)
    void RequestImmediateFlush();

    // Pause/resume flush loop (for import concurrency)
    void FlushAndPause();    // Flush dirty tiles then pause flush loop
    void FlushNow();         // Immediate flush (bypasses pause, for saving data before cache clear)
    void ResumeFlushing();   // Resume flush loop + trigger immediate flush

    // Synchronous flush of a single tile (for eviction scenarios)
    // Returns true if tile was flushed, false if not dirty or not in cache
    bool FlushSingleTile(const Rcl& tile);

    // Statistics
    struct Statistics {
        size_t totalTilesFlushed = 0;
        size_t totalFlushCycles = 0;
        double avgFlushTimeMs = 0.0;
        size_t lastFlushTileCount = 0;
    };
    Statistics GetStatistics() const;

    // Configuration
    static constexpr int DEFAULT_FLUSH_INTERVAL_MS = 5000;  // 5 seconds
    static constexpr int BATCH_SIZE = 50;

    void SetFlushIntervalMs(int intervalMs) { m_flushIntervalMs = intervalMs; }

private:
    void FlushThreadFunc();
    void FlushDirtyTiles();
    void FlushDirtyTilesInternal();
    bool SerializeAndWriteTile(const Rcl& tile);

    DirtyTileTracker& m_dirtyTracker;
    BathymetryDataManager& m_dataManager;
    PbgTileDatabase& m_database;

    std::thread m_flushThread;
    std::mutex m_mutex;
    std::condition_variable m_flushCV;
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_immediateFlushRequested{false};
    std::atomic<bool> m_paused{false};
    std::mutex m_flushActiveMutex;

    int m_flushIntervalMs = DEFAULT_FLUSH_INTERVAL_MS;

    // Statistics
    mutable std::mutex m_statsMutex;
    Statistics m_stats;
};

#endif // ASYNC_PERSISTENCE_MANAGER_H
