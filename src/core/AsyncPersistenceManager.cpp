/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "AsyncPersistenceManager.h"
#include "DirtyTileTracker.h"
#include "BathymetryDataManager.h"
#include "PbgTileDatabase.h"
#include "PbgSeedsReaderWriter.h"
#include "PbgPyramidBuilder.h"
#include "FloatGrid.h"
#include "DpPbg.h"
#include <wx/log.h>
#include <chrono>

AsyncPersistenceManager::AsyncPersistenceManager(
    DirtyTileTracker& dirtyTracker,
    BathymetryDataManager& dataManager,
    PbgTileDatabase& database)
    : m_dirtyTracker(dirtyTracker)
    , m_dataManager(dataManager)
    , m_database(database)
{
}

AsyncPersistenceManager::~AsyncPersistenceManager() {
    Stop();
}

void AsyncPersistenceManager::Start() {
    if (m_flushThread.joinable()) {
        return;  // Already running
    }

    m_shouldStop = false;
    m_flushThread = std::thread(&AsyncPersistenceManager::FlushThreadFunc, this);
}

void AsyncPersistenceManager::Stop() {
    if (!m_flushThread.joinable()) {
        return;
    }

    // Signal stop and wake up thread
    m_shouldStop = true;
    m_flushCV.notify_one();

    // Wait for thread to finish (includes final flush)
    m_flushThread.join();
}

void AsyncPersistenceManager::RequestImmediateFlush() {
    m_immediateFlushRequested = true;
    m_flushCV.notify_one();
}

void AsyncPersistenceManager::FlushThreadFunc() {
    while (!m_shouldStop) {
        // Wait for flush interval or immediate flush request
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_flushCV.wait_for(lock, std::chrono::milliseconds(m_flushIntervalMs), [this]() {
                return m_shouldStop.load() || m_immediateFlushRequested.load();
            });
        }

        m_immediateFlushRequested = false;

        // Flush dirty tiles
        FlushDirtyTiles();
    }

    // Final flush on shutdown
    FlushDirtyTiles();
}

void AsyncPersistenceManager::FlushDirtyTiles() {
    std::lock_guard<std::mutex> lock(m_flushActiveMutex);
    if (m_paused) return;
    FlushDirtyTilesInternal();
}

void AsyncPersistenceManager::FlushAndPause() {
    m_paused = true;

    // Wait for any in-progress flush to finish, then run a final flush
    std::lock_guard<std::mutex> lock(m_flushActiveMutex);
    FlushDirtyTilesInternal();
}

void AsyncPersistenceManager::FlushNow() {
    std::lock_guard<std::mutex> lock(m_flushActiveMutex);
    FlushDirtyTilesInternal();
}

void AsyncPersistenceManager::ResumeFlushing() {
    m_paused = false;
    RequestImmediateFlush();
}

void AsyncPersistenceManager::FlushDirtyTilesInternal() {
    try {

    if (!m_dirtyTracker.HasDirtyDataTiles()) {
        return;
    }

    auto startTime = std::chrono::steady_clock::now();

    // Get and clear dirty tiles atomically
    std::vector<Rcl> dirtyTiles = m_dirtyTracker.GetAndClearDirtyLevel16();

    if (dirtyTiles.empty()) {
        return;
    }

    size_t flushedCount = 0;
    std::vector<Rcl> flushedL16Tiles;  // Track which L16 tiles were actually written
    std::vector<std::pair<Rcl, std::vector<uint8_t>>> batchBuffer;
    batchBuffer.reserve(BATCH_SIZE);

    for (const auto& tile : dirtyTiles) {
        // Get the grid from cache
        auto grid = m_dataManager.GetParentGridForFlush(tile);

        if (!grid) {
            // Grid not in cache anymore - this shouldn't happen often
            // Re-mark as dirty so we don't lose the data if it's still relevant
            wxLogWarning("AsyncPersistenceManager: Grid not in cache for tile %d_%d_%d",
                        tile.Level(), tile.Row(), tile.Column());
            continue;
        }

        // Serialize the grid
        std::vector<uint8_t> blob = PbgSeedsReaderWriter::Serialize(*grid);

        if (blob.empty()) {
            wxLogWarning("AsyncPersistenceManager: Failed to serialize tile %d_%d_%d",
                        tile.Level(), tile.Row(), tile.Column());
            continue;
        }

        batchBuffer.push_back({tile, std::move(blob)});

        // Flush batch when full
        if (batchBuffer.size() >= BATCH_SIZE) {
            if (m_database.SetPbgBlobBatch(batchBuffer)) {
                flushedCount += batchBuffer.size();

                for (const auto& item : batchBuffer) {
                    m_dirtyTracker.MarkAllParentsPyramidDirty(item.first);
                    flushedL16Tiles.push_back(item.first);
                }
            }
            batchBuffer.clear();
        }
    }

    // Flush remaining tiles
    if (!batchBuffer.empty()) {
        if (m_database.SetPbgBlobBatch(batchBuffer)) {
            flushedCount += batchBuffer.size();

            for (const auto& item : batchBuffer) {
                m_dirtyTracker.MarkAllParentsPyramidDirty(item.first);
                flushedL16Tiles.push_back(item.first);
            }
        }
    }

    // Update statistics
    auto endTime = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalTilesFlushed += flushedCount;
        m_stats.totalFlushCycles++;
        m_stats.lastFlushTileCount = flushedCount;

        // Running average
        double totalTime = m_stats.avgFlushTimeMs * (m_stats.totalFlushCycles - 1) + elapsedMs;
        m_stats.avgFlushTimeMs = totalTime / m_stats.totalFlushCycles;
    }

    if (flushedCount > 0) {
        wxLogMessage("AsyncPersistenceManager: Flushed %zu tiles in %.1f ms",
                    flushedCount, elapsedMs);
    }

    // Incremental pyramid build for live recording (L16 -> L0)
    if (!flushedL16Tiles.empty()) {
        auto pyramidStart = std::chrono::steady_clock::now();

        PbgPyramidBuilder builder(&m_database);
        constexpr int LIVE_PYRAMID_MIN_LEVEL = DpPbg::MIN_LEVEL;

        try {
            builder.BuildPyramidToLevel(flushedL16Tiles, LIVE_PYRAMID_MIN_LEVEL);
        } catch (const std::exception& e) {
            wxLogError("AsyncPersistenceManager: Pyramid build failed: %s", e.what());
        } catch (...) {
            wxLogError("AsyncPersistenceManager: Pyramid build failed with unknown error");
        }

        // Always invalidate caches — even if pyramid build stopped early at some level,
        // tiles were written at the levels that did succeed
        m_dataManager.InvalidateCachesForPyramidBuild(flushedL16Tiles, LIVE_PYRAMID_MIN_LEVEL);

        auto pyramidEnd = std::chrono::steady_clock::now();
        double pyramidMs = std::chrono::duration<double, std::milli>(pyramidEnd - pyramidStart).count();
        wxLogMessage("AsyncPersistenceManager: Pyramid build (L16->L%d) for %zu tiles in %.1f ms",
                    LIVE_PYRAMID_MIN_LEVEL, flushedL16Tiles.size(), pyramidMs);
    }

    } catch (const std::exception& e) {
        wxLogError("AsyncPersistenceManager::FlushDirtyTiles failed: %s", e.what());
    } catch (...) {
        wxLogError("AsyncPersistenceManager::FlushDirtyTiles failed with unknown error");
    }
}

bool AsyncPersistenceManager::FlushSingleTile(const Rcl& tile) {
    // Check if tile is dirty
    if (!m_dirtyTracker.IsDataDirty(tile)) {
        return false;
    }

    // Get grid from cache
    auto grid = m_dataManager.GetParentGridForFlush(tile);
    if (!grid) {
        return false;
    }

    // Serialize
    std::vector<uint8_t> blob = PbgSeedsReaderWriter::Serialize(*grid);
    if (blob.empty()) {
        return false;
    }

    // Write to database
    if (!m_database.SetPbgBlob(tile, blob)) {
        return false;
    }

    // Mark parent levels as pyramid-dirty
    m_dirtyTracker.MarkAllParentsPyramidDirty(tile);

    return true;
}

bool AsyncPersistenceManager::SerializeAndWriteTile(const Rcl& tile) {
    return FlushSingleTile(tile);
}

AsyncPersistenceManager::Statistics AsyncPersistenceManager::GetStatistics() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}
