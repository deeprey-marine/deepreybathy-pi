/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgPyramidBuilder.h"
#include "PbgSeedsReaderWriter.h"
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#include <array>
#include <cstring>
#include "DpPbg.h"

namespace std {
    template<>
    struct hash<Rcl> {
        size_t operator()(const Rcl& rcl) const {
            return hash<int>()(rcl.Level()) ^ 
                   (hash<int>()(rcl.Row()) << 1) ^ 
                   (hash<int>()(rcl.Column()) << 2);
        }
    };
}

PbgPyramidBuilder::PbgPyramidBuilder(PbgTileDatabase* database) 
    : m_database(database) {
    if (!m_database) {
        throw std::invalid_argument("Database cannot be null");
    }
}

PbgPyramidBuilder::~PbgPyramidBuilder() {
}

bool PbgPyramidBuilder::BuildPyramid(const std::vector<Rcl>& tilesAtLevel16, wxProgressDialog* progressDlg) {
    // Delegate to the callback variant so progress weighting and wording live in
    // one place. (Used standalone by the "Rebuild pyramid" maintenance action.)
    if (!progressDlg) {
        return BuildPyramid(tilesAtLevel16, ProgressFn());
    }
    return BuildPyramid(tilesAtLevel16, [progressDlg](int pct, const wxString& msg) -> bool {
        return progressDlg->Update(pct, msg);
    });
}

bool PbgPyramidBuilder::BuildPyramid(const std::vector<Rcl>& tilesAtLevel16,
                                     const ProgressFn& progressCb) {
    if (tilesAtLevel16.empty()) {
        return true;
    }

    std::vector<Rcl> currentLevelTiles = tilesAtLevel16;
    int currentLevel = DpPbg::DEFAULT_IMPORT_LEVEL;

    // Weight progress by tiles processed, NOT by level count. Each level holds
    // ~1/4 the tiles of the one above, so the first level alone is ~3/4 of the
    // total work. Equal per-level slices would make the bar crawl during that
    // longest phase and then race through the trivial deep levels. The
    // geometric sum (1 + 1/4 + 1/16 + ...) of children processed ~= 4/3 * start.
    double estimatedTotalWork = static_cast<double>(tilesAtLevel16.size()) * 4.0 / 3.0;
    if (estimatedTotalWork < 1.0) estimatedTotalWork = 1.0;
    double cumulativeWork = 0.0;

    while (currentLevel > DpPbg::MIN_LEVEL && !currentLevelTiles.empty()) {
        int parentLevel = currentLevel - 1;

        double thisLevelWork = static_cast<double>(currentLevelTiles.size());
        int levelLow  = static_cast<int>(cumulativeWork / estimatedTotalWork * 100.0);
        int levelHigh = static_cast<int>((cumulativeWork + thisLevelWork) / estimatedTotalWork * 100.0);
        if (levelHigh > 100) levelHigh = 100;

        ProgressFn levelCb;
        if (progressCb) {
            if (!progressCb(levelLow, "Building depth map...")) {
                return false;
            }
            // Map this level's 0-100 sub-progress into [levelLow, levelHigh] and
            // present a single plain phase label (no level/tile jargon).
            levelCb = [&progressCb, levelLow, levelHigh](int localPct, const wxString& /*msg*/) -> bool {
                int pct = levelLow + (levelHigh - levelLow) * localPct / 100;
                return progressCb(pct, "Building depth map...");
            };
        }

        if (!GeneralizeLevel(currentLevelTiles, parentLevel, levelCb)) {
            return false;
        }

        cumulativeWork += thisLevelWork;

        std::unordered_set<Rcl> parentTilesSet;
        for (const auto& tile : currentLevelTiles) {
            if (tile.Level() > DpPbg::MIN_LEVEL) {
                parentTilesSet.insert(tile.Parent());
            }
        }

        currentLevelTiles.assign(parentTilesSet.begin(), parentTilesSet.end());
        currentLevel = parentLevel;
    }

    if (progressCb) {
        progressCb(100, "Building depth map...");
    }
    return true;
}

bool PbgPyramidBuilder::BuildPyramidToLevel(const std::vector<Rcl>& tilesAtLevel16, int minLevel) {
    if (tilesAtLevel16.empty()) {
        return true;
    }

    if (minLevel < DpPbg::MIN_LEVEL) {
        minLevel = DpPbg::MIN_LEVEL;
    }

    wxLogMessage("PbgPyramidBuilder::BuildPyramidToLevel - %zu L16 tiles, building down to level %d",
                 tilesAtLevel16.size(), minLevel);

    std::vector<Rcl> currentLevelTiles = tilesAtLevel16;
    int currentLevel = DpPbg::DEFAULT_IMPORT_LEVEL;

    while (currentLevel > minLevel && !currentLevelTiles.empty()) {
        int parentLevel = currentLevel - 1;

        if (!GeneralizeLevel(currentLevelTiles, parentLevel)) {
            // GeneralizeLevel returns false when 0 tiles are created at this level.
            // This is a natural stopping point (data fully converged), not a failure.
            wxLogMessage("BuildPyramidToLevel: No tiles created at level %d, stopping", parentLevel);
            break;
        }

        // Collect unique parent tiles for next iteration
        std::unordered_set<Rcl> parentTilesSet;
        for (const auto& tile : currentLevelTiles) {
            if (tile.Level() > minLevel) {
                parentTilesSet.insert(tile.Parent());
            }
        }

        currentLevelTiles.clear();
        currentLevelTiles.insert(currentLevelTiles.end(),
                                 parentTilesSet.begin(),
                                 parentTilesSet.end());
        currentLevel = parentLevel;
    }

    wxLogMessage("PbgPyramidBuilder::BuildPyramidToLevel - Done (L16 -> L%d)", minLevel);
    return true;
}

bool PbgPyramidBuilder::RebuildFromDatabase(wxProgressDialog* progressDlg) {
    std::vector<Rcl> tiles;
    if (!m_database->GetAvailableTiles(tiles, DpPbg::DEFAULT_IMPORT_LEVEL)) {
        return false;
    }
    
    
    return BuildPyramid(tiles, progressDlg);
}

bool PbgPyramidBuilder::GeneralizeSingleLevel(int fromLevel, int toLevel, wxProgressDialog* progressDlg) {
    if (toLevel != fromLevel - 1) {
        return false;
    }
    
    std::vector<Rcl> tiles;
    if (!m_database->GetAvailableTiles(tiles, fromLevel)) {
        return false;
    }
    
    
    return GeneralizeLevel(tiles, toLevel, progressDlg);
}

bool PbgPyramidBuilder::GeneralizeLevel(const std::vector<Rcl>& tilesAtCurrentLevel,
                                        int targetLevel, wxProgressDialog* progressDlg) {
    // Thin wrapper: forward to the callback variant, preserving the dialog's
    // Pulse behavior (and its cancel semantics via Pulse's return value).
    ProgressFn cb;
    if (progressDlg) {
        cb = [progressDlg](int /*pct*/, const wxString& msg) -> bool {
            return progressDlg->Pulse(msg);
        };
    }
    return GeneralizeLevel(tilesAtCurrentLevel, targetLevel, cb);
}

bool PbgPyramidBuilder::GeneralizeLevel(const std::vector<Rcl>& tilesAtCurrentLevel,
                                        int targetLevel, const ProgressFn& progressCb) {
    std::unordered_map<Rcl, std::vector<Rcl>> parentToChildTiles;
    
    for (const auto& tile : tilesAtCurrentLevel) {
        Rcl parent = tile.Parent();
        if (parent.Level() == targetLevel) {
            parentToChildTiles[parent].push_back(tile);
        }
    }
    
    int totalParents = parentToChildTiles.size();
    int createdCount = 0;
    int processedCount = 0;
    int skippedCount = 0;
    
    const int BATCH_SIZE = 100;
    std::vector<std::pair<Rcl, std::vector<uint8_t>>> batchBuffer;
    batchBuffer.reserve(BATCH_SIZE);
    
    wxLogMessage("Potential parent tiles to create: %d", totalParents);
    
    // Open DB once for the entire level
    try {
    wxSQLite3Database db;
    db.Open(m_database->GetDatabasePath());
    db.ExecuteUpdate("PRAGMA busy_timeout = 5000");
    db.Begin();

    // Prepare read statement once, reuse for all child tile reads
    // This avoids opening separate connections per GetPbgBlob() call
    wxSQLite3Statement readStmt = db.PrepareStatement(
        "SELECT datablob FROM MX_RCL WHERE l = ? AND c = ? AND r = ?"
    );

    for (const auto& pair : parentToChildTiles) {
        const Rcl& parentTile = pair.first;
        const std::vector<Rcl>& childTiles = pair.second;

        // Report ~100 updates per level (independent of level size) to avoid
        // flooding the UI queue on huge levels while staying smooth on small ones.
        int reportStep = std::max(1, totalParents / 100);
        if (progressCb && processedCount % reportStep == 0) {
            int localPct = totalParents > 0 ? (processedCount * 100 / totalParents) : 100;
            wxString subMessage = wxString::Format(
                "Building pyramid - Level %d\nParent tile %d/%d (created: %d)\nAvailable children: %zu/4",
                targetLevel, processedCount + 1, totalParents, createdCount, childTiles.size()
            );
            if (!progressCb(localPct, subMessage)) {
                db.Rollback();
                db.Close();
                wxLogMessage("Pyramid generalization cancelled by user at level %d", targetLevel);
                return false;
            }
        }

        std::unique_ptr<FloatGrid> nwGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
        std::unique_ptr<FloatGrid> neGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
        std::unique_ptr<FloatGrid> swGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
        std::unique_ptr<FloatGrid> seGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);

        bool hasNW = false, hasNE = false, hasSW = false, hasSE = false;

        // Load ALL 4 children from DB using the already-open connection
        // This is critical for incremental builds where siblings were flushed in previous cycles
        std::vector<uint8_t> blob;
        auto readChild = [&](const Rcl& child) -> bool {
            readStmt.Bind(1, child.Level());
            readStmt.Bind(2, child.Column());
            readStmt.Bind(3, child.Row());
            wxSQLite3ResultSet result = readStmt.ExecuteQuery();
            bool found = false;
            if (result.NextRow()) {
                int blobSize = 0;
                const void* blobData = result.GetBlob("datablob", blobSize);
                if (blobData && blobSize > 0) {
                    blob.resize(blobSize);
                    memcpy(blob.data(), blobData, blobSize);
                    found = true;
                }
            }
            readStmt.Reset();
            return found;
        };

        if (readChild(parentTile.ChildNorthwest())) {
            PbgSeedsReaderWriter::Deserialize(blob, *nwGrid);
            hasNW = true;
        }
        if (readChild(parentTile.ChildNortheast())) {
            PbgSeedsReaderWriter::Deserialize(blob, *neGrid);
            hasNE = true;
        }
        if (readChild(parentTile.ChildSouthwest())) {
            PbgSeedsReaderWriter::Deserialize(blob, *swGrid);
            hasSW = true;
        }
        if (readChild(parentTile.ChildSoutheast())) {
            PbgSeedsReaderWriter::Deserialize(blob, *seGrid);
            hasSE = true;
        }

        if (hasNW || hasNE || hasSW || hasSE) {
            FloatGrid parentGrid(DpPbg::PIXEL_TILE_SIZE);
            parentGrid.CopyFromChildren(
                hasNW ? nwGrid.get() : nullptr,
                hasNE ? neGrid.get() : nullptr,
                hasSW ? swGrid.get() : nullptr,
                hasSE ? seGrid.get() : nullptr
            );

            if (!parentGrid.IsEmpty()) {
                std::vector<uint8_t> blob = PbgSeedsReaderWriter::Serialize(parentGrid);

                batchBuffer.push_back(std::make_pair(parentTile, blob));
                createdCount++;

                if (batchBuffer.size() >= BATCH_SIZE) {
                    wxLogMessage("  - Writing batch of %d tiles for level %d", BATCH_SIZE, targetLevel);

                    wxSQLite3Statement stmt = db.PrepareStatement(
                        "INSERT OR REPLACE INTO MX_RCL (r, c, l, datablob) VALUES (?, ?, ?, ?)"
                    );

                    // Syntaxe C++11/14
                    for (const auto& tileData : batchBuffer) {
                        const Rcl& tile = tileData.first;
                        const std::vector<uint8_t>& data = tileData.second;

                        stmt.Bind(1, tile.Row());
                        stmt.Bind(2, tile.Column());
                        stmt.Bind(3, tile.Level());
                        stmt.Bind(4, data.data(), data.size());
                        stmt.ExecuteUpdate();
                        stmt.Reset();
                    }

                    batchBuffer.clear();
                }

                int childCount = (hasNW ? 1 : 0) + (hasNE ? 1 : 0) + (hasSW ? 1 : 0) + (hasSE ? 1 : 0);
                if (childCount < 4 && createdCount % 50 == 0) {
                    wxLogMessage("  Progress: %d tiles created (last one with %d/4 children)",
                                createdCount, childCount);
                }
            } else {
                skippedCount++;
            }
        } else {
            skippedCount++;
        }

        processedCount++;
    }

    if (!batchBuffer.empty()) {
        wxLogMessage("  - Writing final batch of %zu tiles for level %d", batchBuffer.size(), targetLevel);

        wxSQLite3Statement stmt = db.PrepareStatement(
            "INSERT OR REPLACE INTO MX_RCL (r, c, l, datablob) VALUES (?, ?, ?, ?)"
        );

        // Syntaxe C++11/14
        for (const auto& tileData : batchBuffer) {
            const Rcl& tile = tileData.first;
            const std::vector<uint8_t>& data = tileData.second;

            stmt.Bind(1, tile.Row());
            stmt.Bind(2, tile.Column());
            stmt.Bind(3, tile.Level());
            stmt.Bind(4, data.data(), data.size());
            stmt.ExecuteUpdate();
            stmt.Reset();
        }
    }

    db.Commit();
    db.Close();

    } catch (wxSQLite3Exception& e) {
        wxLogError("PbgPyramidBuilder::GeneralizeLevel - SQLite error at level %d: %s",
                   targetLevel, e.GetMessage());
        // db is scoped inside the try block, destructor handles cleanup
        return false;
    }
    
    wxLogMessage("Level %d completed: %d tiles created, %d skipped (empty)", 
                targetLevel, createdCount, skippedCount);
    
    return createdCount > 0;
}

bool PbgPyramidBuilder::CreateParentTileFromChildren(const Rcl& parentTile, 
                                                const std::vector<Rcl>& childTiles) {
    std::unique_ptr<FloatGrid> nwGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    std::unique_ptr<FloatGrid> neGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    std::unique_ptr<FloatGrid> swGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    std::unique_ptr<FloatGrid> seGrid = std::make_unique<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    
    bool hasNW = false, hasNE = false, hasSW = false, hasSE = false;
    
    for (const auto& childTile : childTiles) {
        std::vector<uint8_t> blob;
        if (m_database->GetPbgBlob(childTile, blob)) {
            if (childTile == parentTile.ChildNorthwest()) {
                PbgSeedsReaderWriter::Deserialize(blob, *nwGrid);
                hasNW = true;
            }
            else if (childTile == parentTile.ChildNortheast()) {
                PbgSeedsReaderWriter::Deserialize(blob, *neGrid);
                hasNE = true;
            }
            else if (childTile == parentTile.ChildSouthwest()) {
                PbgSeedsReaderWriter::Deserialize(blob, *swGrid);
                hasSW = true;
            }
            else if (childTile == parentTile.ChildSoutheast()) {
                PbgSeedsReaderWriter::Deserialize(blob, *seGrid);
                hasSE = true;
            }
        }
    }
    
    if (!hasNW && !hasNE && !hasSW && !hasSE) {
        return false;
    }
    
    int childCount = (hasNW ? 1 : 0) + (hasNE ? 1 : 0) + (hasSW ? 1 : 0) + (hasSE ? 1 : 0);
    if (childCount < 4 && parentTile.Level() % 3 == 0) {
    }
    
    FloatGrid parentGrid(DpPbg::PIXEL_TILE_SIZE);
    parentGrid.CopyFromChildren(
        hasNW ? nwGrid.get() : nullptr,
        hasNE ? neGrid.get() : nullptr,
        hasSW ? swGrid.get() : nullptr,
        hasSE ? seGrid.get() : nullptr
    );
    
    if (!parentGrid.IsEmpty()) {
        std::vector<uint8_t> blob = PbgSeedsReaderWriter::Serialize(parentGrid);
        return m_database->SetPbgBlob(parentTile, blob);
    }
    
    return false;
}