/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef IMPORT_EXPORT_MANAGER_H
#define IMPORT_EXPORT_MANAGER_H

#include <wx/string.h>
#include <wx/progdlg.h>
#include <wx/wxsqlite3.h>
#include "PbgTileDatabase.h"
#include "PbgImportFilter.h"
#include "MinMax.h"
#include <map>
#include <fstream>
#include <vector>
#include <functional>
#include <unordered_set>
#include "Rcl.h"

class ImportExportManager {
public:
    ImportExportManager();
    ~ImportExportManager();

    // Returns true to continue, false to cancel
    using ProgressCallback = std::function<bool(int percent, const wxString& status)>;

    // Import methods
    bool ImportXYZFile(const wxString& filepath,
                      const PbgImportFilterConfig& filterConfig,
                      wxProgressDialog* progressDlg = nullptr);

    bool ImportXYZFile(const wxString& filepath, wxProgressDialog* progressDlg = nullptr) {
        PbgImportFilterConfig defaultConfig;
        return ImportXYZFile(filepath, defaultConfig, progressDlg);
    }

    bool ImportXYZFile(const wxString& filepath,
                      const PbgImportFilterConfig& filterConfig,
                      ProgressCallback progressCb);

    // Export methods
    bool ExportAllToXYZ(const wxString& filepath,
                        wxProgressDialog* progressDlg = nullptr);

    bool ExportAllToXYZ(const wxString& filepath, ProgressCallback progressCb);

    bool ExportToXYZ(const wxString& filepath,
                     double minLat, double minLon,
                     double maxLat, double maxLon,
                     wxProgressDialog* progressDlg = nullptr);
    
    bool ExportTileToXYZ(const wxString& filepath, const Rcl& tile,
                        wxProgressDialog* progressDlg = nullptr);
    
    // Database management
    bool ClearDatabase();
    bool RemoveDuplicates();
    bool OptimizeDatabase();
    wxString GetDatabaseInfo() const;
    size_t GetTotalPointCount() const;
    double GetDatabaseSizeMB() const;
    wxString GetLastModifiedTime() const;
    bool RebuildPyramid();
    
private:
    PbgTileDatabase m_database;
    
    // Structure to accumulate points for a tile
    struct TileAccumulator {
        Rcl tile;
        std::vector<Position3D> points;
        MinMax depthRange;
    };
    
    // XYZ format detection
    enum class XYZFormat {
        UNKNOWN,
        LON_LAT_DEPTH,    // longitude,latitude,depth
        LAT_LON_DEPTH,    // latitude,longitude,depth
        X_Y_Z,            // x,y,z (already in mercator)
        NOAA_FORMAT       // special formats
    };
    
    // File information structure
    struct XYZFileInfo {
        XYZFormat format;
        char separator;
        int skipLines;
        bool hasHeader;
        size_t estimatedLines;
    };
    
    // Private methods for chunked reading
    bool DetectXYZFormat(const wxString& filepath, XYZFileInfo& info);
    bool ProcessXYZLine(const wxString& line, const XYZFileInfo& info,
                       double& lon, double& lat, double& depth);
    void WriteBatchToDB(wxSQLite3Database& db,
                       const std::vector<std::pair<Rcl, std::vector<uint8_t>>>& batch);

    // Serialize every tile currently in tileMap to the database, then clear it.
    // Commits the open transaction so the next flush's merge (PrepareTileData ->
    // GetPbgBlob, which reads on a separate WAL connection) sees these writes.
    // Required to bound memory for very large imports without losing points for
    // tiles whose data spans a flush boundary.
    //
    // dbWasEmpty: when true, a tile not yet in processedTiles is brand new, so
    //   the per-tile merge read-back is skipped (big speedup for fresh imports).
    // progressCb/pctLow/pctHigh: reports "Saving tiles..." progress mapped into
    //   [pctLow, pctHigh]. Returns false if the user cancelled.
    bool FlushTileMap(wxSQLite3Database& db,
                      std::map<wxString, TileAccumulator>& tileMap,
                      std::vector<std::pair<Rcl, std::vector<uint8_t>>>& sqlBatchBuffer,
                      std::unordered_set<Rcl, RclHash>& processedTiles,
                      bool dbWasEmpty,
                      const ProgressCallback& progressCb,
                      int pctLow, int pctHigh);

    // Constants
    static const size_t CHUNK_SIZE = 100000;        // 100k lines per chunk
    static const size_t TILE_BATCH_SIZE = 100;     // Write to DB every 100 tiles
    static const size_t MAX_POINTS_PER_TILE = 10000; // Process tile when it has this many points
    // Flush the accumulator to disk once it holds this many points. Bounds peak
    // RAM (~1M * sizeof(Position3D) ~= 24 MB raw) so multi-GB grid files (e.g.
    // EMODnet, ~55 m spacing => ~70 pts/L16 tile, far below MAX_POINTS_PER_TILE)
    // don't accumulate the whole dataset in memory.
    static const size_t MAX_ACCUMULATED_POINTS = 1000000;
};

#endif // IMPORT_EXPORT_MANAGER_H