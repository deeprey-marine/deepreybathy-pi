/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_TILE_DATABASE_H
#define PBG_TILE_DATABASE_H

#include <wx/wxsqlite3.h>
#include <wx/filename.h>
#include <wx/progdlg.h>
#include <map>
#include <memory>
#include <vector>
#include "Rcl.h"
#include "Position.h"
#include "FloatGrid.h"
#include "MinMax.h"

class PbgTileDatabase {
public:
    PbgTileDatabase();
    ~PbgTileDatabase();
    
    bool Initialize();
    wxString GetDatabasePath() const;
       
    bool GetPbgBlob(const Rcl& tile, std::vector<uint8_t>& blob);
    bool SetPbgBlob(const Rcl& tile, const std::vector<uint8_t>& blob);
    bool SetPbgBlobBatch(const std::vector<std::pair<Rcl, std::vector<uint8_t>>>& tileBatch);
    
    bool GetAvailableTiles(std::vector<Rcl>& tiles, int level = -1);
    
    bool GetGlobalBounds(double& minLon, double& maxLon, double& minLat, double& maxLat,
                        double& minDepth, double& maxDepth);
    
    wxString GetDatabaseInfo() const;
    bool ClearAllData();
    
    static void LatLonToMercator(double lat, double lon, double& x, double& y);
    static void MercatorToLatLon(double x, double y, double& lat, double& lon);
    
    size_t GetTotalPointsCount() const;
    double GetDatabaseSizeMB() const;
    wxString GetLastModifiedTime() const;
    bool OptimizeDatabase();
    bool GetPointsForTile(const Rcl& tile, std::vector<Position3D>& points);
    bool RemoveDuplicates();
    bool ExportPointsInArea(double minLat, double minLon, double maxLat, double maxLon,
                           std::vector<Position3D>& points, wxProgressDialog* progressDlg = nullptr);
    
    void LogDatabaseCoverage();
    bool GetTileForLocation(double lat, double lon, int level, Rcl& tile);
    void LogRequestedTile(const Rcl& tile);
    static int GetSuggestedScale(int level);
    bool MergeTileData(const Rcl& tile, const std::vector<Position3D>& newPoints);
    // When mergeWithExisting is false the existing tile blob is NOT read back
    // from disk before rasterizing; the caller guarantees the tile is new
    // (e.g. a fresh import into an empty database). This skips one DB query +
    // deserialize per tile, which dominates bulk-import flush time.
    bool PrepareTileData(const Rcl& tile, const std::vector<Position3D>& newPoints,
                         std::vector<uint8_t>& outBlob, bool mergeWithExisting = true);

    // Cheap "does the tile table contain any row" check (SELECT 1 ... LIMIT 1).
    bool IsEmpty();

    // Rasterize newPoints into a tile blob, merging onto existingBlob if given
    // (pass nullptr/empty for a fresh tile). Unlike PrepareTileData this does NOT
    // read the DB itself, so the caller can supply the existing blob via a single
    // reused connection instead of opening one per tile.
    bool RasterizeToBlob(const Rcl& tile, const std::vector<Position3D>& newPoints,
                         const std::vector<uint8_t>* existingBlob, std::vector<uint8_t>& outBlob);

    // Delete pyramid tiles (L0-L15) for affected L16 tiles
    bool DeletePyramidTiles(const std::vector<Rcl>& l16Tiles);

private:
    wxString m_dbPath;

    wxString GetOpenCPNDatabasePath() const;
    wxString GetLocalDatabasePath() const;
    wxString GetSystemDatabasePath() const;
    
    
    bool CreateTables(wxSQLite3Database& db);

    bool BeginTransaction();
    bool CommitTransaction();
    bool RollbackTransaction();

};

#endif