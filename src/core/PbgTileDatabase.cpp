/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgTileDatabase.h"
#include "PbgSeedsReaderWriter.h"
#include "RclExtensions.h"
#include "RclWorld.h"
#include <wx/textfile.h>
#include <wx/dir.h>
#include <wx/stdpaths.h>
#include <cmath>
#include <set>
#include <tuple>
#include <wx/log.h>
#include <cstring>
#include "DpPbg.h"
#include <mutex>
#include "ocpn_plugin.h"

PbgTileDatabase::PbgTileDatabase() {
}

PbgTileDatabase::~PbgTileDatabase() {
}

bool PbgTileDatabase::Initialize() {
    m_dbPath = GetDatabasePath();
    bool isNewDatabase = !wxFileExists(m_dbPath);
    
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        db.ExecuteUpdate("PRAGMA cache_size = 10000");
        db.ExecuteUpdate("PRAGMA temp_store = MEMORY");
        db.ExecuteUpdate("PRAGMA journal_mode = WAL");
        db.ExecuteUpdate("PRAGMA synchronous = NORMAL");
        
        if (isNewDatabase) {
            if (!CreateTables(db)) {
                return false;
            }
        } else {
            LogDatabaseCoverage();
        }
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::CreateTables(wxSQLite3Database& db) {
    try {
        // Column names are lower-case by convention for this tile format.
        db.ExecuteUpdate(
            "CREATE TABLE IF NOT EXISTS MX_RCL ("
            "r INTEGER NOT NULL,"      
            "c INTEGER NOT NULL,"      
            "l INTEGER NOT NULL,"      
            "datablob BLOB,"          
            "PRIMARY KEY (r, c, l)"
            ")"
        );
        
        db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_level ON MX_RCL(l)");
        
        db.ExecuteUpdate(
            "CREATE TABLE IF NOT EXISTS settings ("
            "key TEXT PRIMARY KEY,"
            "value TEXT"
            ")"
        );
        
        db.ExecuteUpdate("INSERT OR REPLACE INTO settings VALUES ('version', '1.0')");
        db.ExecuteUpdate("INSERT OR REPLACE INTO settings VALUES ('pbg_format', 'pbg_v1')");
        
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::GetPbgBlob(const Rcl& tile, std::vector<uint8_t>& blob) {
    /*
    static std::mutex logMutex;
    static size_t requestCount = 0;
    
    {
        std::lock_guard<std::mutex> lock(logMutex);
        requestCount++;
        wxLogMessage("[SQL-REQUEST #%zu] L=%d R=%d C=%d", 
                    requestCount, 
                    tile.Level(), 
                    tile.Row(), 
                    tile.Column());
    }
    
    */
    
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.ExecuteUpdate("PRAGMA busy_timeout = 5000");

        wxSQLite3Statement stmt = db.PrepareStatement(
            "SELECT datablob FROM MX_RCL WHERE l = ? AND c = ? AND r = ?"
        );
        
        stmt.Bind(1, tile.Level());
        stmt.Bind(2, tile.Column());
        stmt.Bind(3, tile.Row());
        
        wxSQLite3ResultSet result = stmt.ExecuteQuery();
        
        if (result.NextRow()) {
            int blobSize = 0;
            const void* blobData = result.GetBlob("datablob", blobSize);
            
            if (blobData && blobSize > 0) {
                blob.resize(blobSize);
                memcpy(blob.data(), blobData, blobSize);
                
                db.Close();
                return true;
            }
        }
        
        db.Close();
        return false;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::SetPbgBlob(const Rcl& tile, const std::vector<uint8_t>& blob) {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.ExecuteUpdate("PRAGMA busy_timeout = 5000");

        wxSQLite3Statement stmt = db.PrepareStatement(
            "INSERT OR REPLACE INTO MX_RCL (r, c, l, datablob) VALUES (?, ?, ?, ?)"
        );
        
        stmt.Bind(1, tile.Row());
        stmt.Bind(2, tile.Column());
        stmt.Bind(3, tile.Level());
        stmt.Bind(4, blob.data(), blob.size());
        
        stmt.ExecuteUpdate();
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::GetAvailableTiles(std::vector<Rcl>& tiles, int level) {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        wxString query = "SELECT r, c, l FROM MX_RCL";
        if (level >= 0) {
            query += wxString::Format(" WHERE l = %d", level);
        }
        
        wxSQLite3ResultSet result = db.ExecuteQuery(query);
        
        tiles.clear();
        while (result.NextRow()) {
            int r = result.GetInt(0);
            int c = result.GetInt(1);
            int l = result.GetInt(2);
            tiles.push_back(Rcl(r, c, l));
        }
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        wxLogError("PbgTileDatabase::GetAvailableTiles error: %s", e.GetMessage());
        return false;
    }
}

void PbgTileDatabase::LogDatabaseCoverage() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        
        wxSQLite3ResultSet levelCount = db.ExecuteQuery(
            "SELECT l, COUNT(*) as cnt FROM MX_RCL GROUP BY l ORDER BY l"
        );
        
        while (levelCount.NextRow()) {
            int level = levelCount.GetInt(0);
            int count = levelCount.GetInt(1);
        }
        
        wxSQLite3ResultSet levels = db.ExecuteQuery("SELECT DISTINCT l FROM MX_RCL ORDER BY l");
        
        while (levels.NextRow()) {
            int level = levels.GetInt(0);
            
            wxSQLite3ResultSet bounds = db.ExecuteQuery(
                wxString::Format("SELECT MIN(r), MAX(r), MIN(c), MAX(c) FROM MX_RCL WHERE l = %d", level)
            );
            
            if (bounds.NextRow()) {
                int minRow = bounds.GetInt(0);
                int maxRow = bounds.GetInt(1);
                int minCol = bounds.GetInt(2);
                int maxCol = bounds.GetInt(3);
                
                Rcl centerTile((minRow + maxRow) / 2, (minCol + maxCol) / 2, level);
                Position center = centerTile.GetPositionCenter();
                double lat, lon;
                MercatorToLatLon(center.X(), center.Y(), lat, lon);
                
                wxLogMessage("\nNiveau %d - Zone couverte:", level);
                wxLogMessage("  Centre approximatif: %.4f°N, %.4f°E", lat, lon);
                
                Rcl topLeft(minRow, minCol, level);
                Rcl bottomRight(maxRow, maxCol, level);
                
                double lat1, lon1, lat2, lon2;
                Position tl = topLeft.GetPositionTopLeft();
                Position br = bottomRight.GetPositionBottomRight();
                
                MercatorToLatLon(tl.X(), tl.Y(), lat1, lon1);
                MercatorToLatLon(br.X(), br.Y(), lat2, lon2);
                
                
            }
        }
        
        wxLogMessage("=========================================\n");
        
        db.Close();
    }
    catch (wxSQLite3Exception& e) {
    }
}

int PbgTileDatabase::GetSuggestedScale(int level) {
    if (level < 0 || level > DpPbg::MAX_LEVEL) {
        return 1000000;
    }
    return DpPbg::SUGGESTED_SCALES[level];
}

bool PbgTileDatabase::GetTileForLocation(double lat, double lon, int level, Rcl& tile) {
    double x, y;
    LatLonToMercator(lat, lon, x, y);
    Position pos = Position::FromMercator(x, y);
    tile = RclWorld::GetRcl(pos, level);
    
    std::vector<uint8_t> blob;
    return GetPbgBlob(tile, blob);
}

void PbgTileDatabase::LogRequestedTile(const Rcl& tile) {
    static int logCount = 0;
    if (logCount++ % 100 == 0) {
        Position center = tile.GetPositionCenter();
        double lat, lon;
        MercatorToLatLon(center.X(), center.Y(), lat, lon);
        
    }
}

bool PbgTileDatabase::GetGlobalBounds(double& minLon, double& maxLon, double& minLat, double& maxLat,
                                     double& minDepth, double& maxDepth) {
    minLon = -180.0;
    maxLon = 180.0;
    minLat = DpPbg::MIN_LATITUDE;
    maxLat = DpPbg::MAX_LATITUDE;
    minDepth = DpPbg::MIN_DEPTH;
    maxDepth = DpPbg::MAX_DEPTH;
    
    return true;
}

wxString PbgTileDatabase::GetDatabasePath() const {

    // First, check OpenCPN's private data directory (preferred location)
    wxString opencpnPath = GetOpenCPNDatabasePath();
    if (wxFileExists(opencpnPath)) {
        return opencpnPath;
    }

    // Check legacy local path (portable installation)
    wxString localPath = GetLocalDatabasePath();
    if (wxFileExists(localPath)) {
        return localPath;
    }

    // Check legacy system path
    wxString systemPath = GetSystemDatabasePath();
    if (wxFileExists(systemPath)) {
        return systemPath;
    }

    // Create new database in OpenCPN's private data directory
    return opencpnPath;
}

wxString PbgTileDatabase::GetOpenCPNDatabasePath() const {
    wxString* pPrivateDataDir = GetpPrivateApplicationDataLocation();
    if (pPrivateDataDir && !pPrivateDataDir->IsEmpty()) {
        wxString dbDir = *pPrivateDataDir;
        // Ensure directory exists
        if (!wxDir::Exists(dbDir)) {
            wxFileName::Mkdir(dbDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        return dbDir + wxFileName::GetPathSeparator() + "bathymetry_pbg.db";
    }
    // Fallback to system path if OpenCPN API not available
    return GetSystemDatabasePath();
}

wxString PbgTileDatabase::GetLocalDatabasePath() const {
    wxString appPath = wxStandardPaths::Get().GetExecutablePath();
    wxFileName fn(appPath);
    wxString dataDir = fn.GetPath() + wxFileName::GetPathSeparator() + "data";
    
    if (!wxDir::Exists(dataDir)) {
        wxFileName::Mkdir(dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    
    return dataDir + wxFileName::GetPathSeparator() + "bathymetry_pbg.db";
}

wxString PbgTileDatabase::GetSystemDatabasePath() const {
    wxString dbDir;
    
#ifdef __WXMSW__
    dbDir = "C:\\ProgramData\\BathymetriePBG";
#else
    dbDir = wxGetHomeDir() + "/.bathymetrie_pbg";
#endif
    
    if (!wxDir::Exists(dbDir)) {
        wxFileName::Mkdir(dbDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    
    return dbDir + wxFileName::GetPathSeparator() + "bathymetry_pbg.db";
}

wxString PbgTileDatabase::GetDatabaseInfo() const {
    wxString info;
    
    if (wxFileExists(m_dbPath)) {
        wxFileName fn(m_dbPath);
        
        wxString location = "local";
        if (m_dbPath.Contains("ProgramData") || m_dbPath.Contains(".bathymetrie_pbg")) {
            location = "system";
        }
        
        size_t tileCount = 0;
        try {
            wxSQLite3Database db;
            db.Open(m_dbPath);
            wxSQLite3ResultSet result = db.ExecuteQuery("SELECT COUNT(*) FROM MX_RCL");
            if (result.NextRow()) {
                tileCount = result.GetInt(0);
            }
            db.Close();
        }
        catch (...) {}
        
        info = wxString::Format(
            "PBG tile database:\n"
            "Location: %s\n"
            "Path: %s\n"
            "Size: %.2f MB\n"
            "Tiles: %zu\n"
            "Last modified: %s",
            location,
            m_dbPath,
            fn.GetSize().ToDouble() / (1024.0 * 1024.0),
            tileCount,
            fn.GetModificationTime().Format("%Y-%m-%d %H:%M:%S")
        );
    } else {
        info = "Database not found";
    }
    
    return info;
}

void PbgTileDatabase::LatLonToMercator(double lat, double lon, double& x, double& y) {
    x = lon * DpPbg::PI / 180.0 * DpPbg::EARTH_RADIUS;
    
    if (lat > DpPbg::MAX_LATITUDE) lat = DpPbg::MAX_LATITUDE;
    if (lat < DpPbg::MIN_LATITUDE) lat = DpPbg::MIN_LATITUDE;
    
    double latRad = lat * DpPbg::PI / 180.0;
    y = DpPbg::EARTH_RADIUS * log(tan(DpPbg::PI / 4.0 + latRad / 2.0));
}

void PbgTileDatabase::MercatorToLatLon(double x, double y, double& lat, double& lon) {
    lon = x / DpPbg::EARTH_RADIUS * 180.0 / DpPbg::PI;
    lat = atan(sinh(y / DpPbg::EARTH_RADIUS)) * 180.0 / DpPbg::PI;
}

bool PbgTileDatabase::ClearAllData() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        db.ExecuteUpdate("DELETE FROM MX_RCL");
        db.ExecuteUpdate("DELETE FROM settings WHERE key LIKE 'last_%'");
        db.ExecuteUpdate("VACUUM");
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception&) {
        return false;
    }
}

size_t PbgTileDatabase::GetTotalPointsCount() const {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        wxSQLite3ResultSet result = db.ExecuteQuery("SELECT COUNT(*) FROM MX_RCL");
        size_t tileCount = 0;
        if (result.NextRow()) {
            tileCount = result.GetInt(0);
        }
        
        db.Close();
        return tileCount * 1000;
    }
    catch (wxSQLite3Exception&) {
        return 0;
    }
}

double PbgTileDatabase::GetDatabaseSizeMB() const {
    if (!wxFileExists(m_dbPath)) return 0.0;
    wxFileName fn(m_dbPath);
    return fn.GetSize().ToDouble() / (1024.0 * 1024.0);
}

wxString PbgTileDatabase::GetLastModifiedTime() const {
    if (!wxFileExists(m_dbPath)) return wxEmptyString;
    wxFileName fn(m_dbPath);
    wxDateTime modTime = fn.GetModificationTime();
    if (!modTime.IsValid()) return wxEmptyString;

    wxDateTime now = wxDateTime::Now();
    if (modTime.FormatDate() == now.FormatDate()) {
        return wxString::Format(_("Today, %s"), modTime.Format("%H:%M"));
    }
    wxDateTime yesterday = now - wxTimeSpan::Day();
    if (modTime.FormatDate() == yesterday.FormatDate()) {
        return wxString::Format(_("Yesterday, %s"), modTime.Format("%H:%M"));
    }
    return modTime.Format("%Y-%m-%d %H:%M");
}

bool PbgTileDatabase::OptimizeDatabase() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        db.ExecuteUpdate("VACUUM");
        db.ExecuteUpdate("ANALYZE");
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::GetPointsForTile(const Rcl& tile, std::vector<Position3D>& points) {
    points.clear();

    std::vector<uint8_t> blob;
    if (!GetPbgBlob(tile, blob))
        return false;

    FloatGrid grid(DpPbg::PIXEL_TILE_SIZE);
    if (!PbgSeedsReaderWriter::Deserialize(blob, grid))
        return false;

    const int dim = grid.GetDimension();
    const double coordRange = GridCoordRange(dim);

    Position extTL, extBR;
    RclExtensions::GetExtendedRclPosition(tile, extTL, extBR);

    const double w = MercatorWidth(extTL, extBR);
    const double h = MercatorHeight(extTL, extBR);

    for (int y = 0; y < dim; ++y) {
        double relY = double(y) / coordRange;
        double mercY = extBR.Y() + relY * h;  // Y=0 au Sud
        
        for (int x = 0; x < dim; ++x) {
            float v = grid.GetValue(x, y);
            if (v == DpPbg::NO_DATA_VALUE)
                continue;
                
            double relX = double(x) / coordRange;
            double mercX = extTL.X() + relX * w;
            points.emplace_back(mercX, mercY, v);
        }
    }
    return true;
}

bool PbgTileDatabase::ExportPointsInArea(double minLat, double minLon, double maxLat, double maxLon,
                                        std::vector<Position3D>& points, wxProgressDialog* progressDlg) {
    points.clear();
    
    try {
        double minX, minY, maxX, maxY;
        LatLonToMercator(minLat, minLon, minX, minY);
        LatLonToMercator(maxLat, maxLon, maxX, maxY);
        
        wxSQLite3Database db;
        db.Open(m_dbPath);
        
        wxSQLite3ResultSet result = db.ExecuteQuery("SELECT DISTINCT l FROM MX_RCL ORDER BY l DESC LIMIT 1");
        int defaultLevel = DpPbg::DEFAULT_EXPORT_LEVEL;
        if (result.NextRow()) {
            defaultLevel = result.GetInt(0);
        }
        
        Position posMin = Position::FromMercator(minX, minY);
        Position posMax = Position::FromMercator(maxX, maxY);
        
        Rcl tileMin = RclWorld::GetRcl(posMin, defaultLevel);
        Rcl tileMax = RclWorld::GetRcl(posMax, defaultLevel);
        
        int minRow = std::min(tileMin.Row(), tileMax.Row());
        int maxRow = std::max(tileMin.Row(), tileMax.Row());
        int minCol = std::min(tileMin.Column(), tileMax.Column());
        int maxCol = std::max(tileMin.Column(), tileMax.Column());
        
        int totalTiles = (maxRow - minRow + 1) * (maxCol - minCol + 1);
        int processedTiles = 0;
        
        for (int row = minRow; row <= maxRow; row++) {
            for (int col = minCol; col <= maxCol; col++) {
                Rcl tile(row, col, defaultLevel);
                
                std::vector<Position3D> tilePoints;
                if (GetPointsForTile(tile, tilePoints)) {
                    for (const auto& pt : tilePoints) {
                        double lat, lon;
                        MercatorToLatLon(pt.x, pt.y, lat, lon);
                        
                        if (lat >= minLat && lat <= maxLat && 
                            lon >= minLon && lon <= maxLon) {
                            points.push_back(pt);
                        }
                    }
                }
                
                processedTiles++;
                if (progressDlg) {
                    int percent = (processedTiles * 100) / totalTiles;
                    progressDlg->Update(percent, 
                        wxString::Format("Processing tile %d/%d", processedTiles, totalTiles));
                }
            }
        }
        
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        wxLogError("ExportPointsInArea error: %s", e.GetMessage());
        return false;
    }
}

bool PbgTileDatabase::MergeTileData(const Rcl& tile, const std::vector<Position3D>& newPoints) {
    if (newPoints.empty()) {
        return true;
    }
    
    FloatGrid grid(DpPbg::PIXEL_TILE_SIZE);
    
    std::vector<uint8_t> existingBlob;
    bool hasExistingData = GetPbgBlob(tile, existingBlob);
    
    if (hasExistingData) {
        if (!PbgSeedsReaderWriter::Deserialize(existingBlob, grid)) {
            return false;
        }
    }
    
    grid.RasterizePoints(newPoints, tile);
    
    std::vector<uint8_t> newBlob = PbgSeedsReaderWriter::Serialize(grid);
    
    return SetPbgBlob(tile, newBlob);
}

bool PbgTileDatabase::RemoveDuplicates() {
    return true;
}

bool PbgTileDatabase::SetPbgBlobBatch(const std::vector<std::pair<Rcl, std::vector<uint8_t>>>& tileBatch) {
    if (tileBatch.empty()) {
        return true;
    }
    
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.ExecuteUpdate("PRAGMA busy_timeout = 5000");

        wxSQLite3Statement stmt = db.PrepareStatement(
            "INSERT OR REPLACE INTO MX_RCL (r, c, l, datablob) VALUES (?, ?, ?, ?)"
        );

        // Syntaxe C++11/14 compatible
        for (const auto& tileData : tileBatch) {
            const Rcl& tile = tileData.first;
            const std::vector<uint8_t>& blob = tileData.second;

            stmt.Bind(1, tile.Row());
            stmt.Bind(2, tile.Column());
            stmt.Bind(3, tile.Level());
            stmt.Bind(4, blob.data(), blob.size());

            stmt.ExecuteUpdate();
            stmt.Reset();
        }

        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        wxLogError("SetPbgBlobBatch FAILED (%zu tiles): %s", tileBatch.size(), e.GetMessage());
        for (const auto& tileData : tileBatch) {
            const Rcl& tile = tileData.first;
            wxLogError("  -> Failed tile: L%d R%d C%d", tile.Level(), tile.Row(), tile.Column());
        }
        return false;
    }
}

bool PbgTileDatabase::DeletePyramidTiles(const std::vector<Rcl>& l16Tiles) {
    if (l16Tiles.empty()) return true;

    // Collect all pyramid tile keys (L15 down to L0)
    std::set<std::tuple<int,int,int>> toDelete;
    for (const auto& tile : l16Tiles) {
        Rcl current = tile.Parent();
        while (current.Level() >= 0) {
            toDelete.insert({current.Row(), current.Column(), current.Level()});
            if (current.Level() == 0) break;
            current = current.Parent();
        }
    }

    if (toDelete.empty()) return true;

    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.ExecuteUpdate("PRAGMA busy_timeout = 5000");
        db.Begin();

        wxSQLite3Statement stmt = db.PrepareStatement(
            "DELETE FROM MX_RCL WHERE r = ? AND c = ? AND l = ?"
        );

        for (const auto& [r, c, l] : toDelete) {
            stmt.Bind(1, r);
            stmt.Bind(2, c);
            stmt.Bind(3, l);
            stmt.ExecuteUpdate();
            stmt.Reset();
        }

        db.Commit();
        db.Close();

        wxLogMessage("PbgTileDatabase::DeletePyramidTiles: Deleted %zu pyramid tiles for %zu L16 tiles",
                     toDelete.size(), l16Tiles.size());
        return true;
    } catch (wxSQLite3Exception& e) {
        wxLogError("DeletePyramidTiles failed: %s", e.GetMessage());
        return false;
    }
}

bool PbgTileDatabase::BeginTransaction() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.Begin();
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::CommitTransaction() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.Commit();
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::RollbackTransaction() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        db.Rollback();
        db.Close();
        return true;
    }
    catch (wxSQLite3Exception& e) {
        return false;
    }
}

bool PbgTileDatabase::PrepareTileData(const Rcl& tile,
                                      const std::vector<Position3D>& newPoints,
                                      std::vector<uint8_t>& outBlob,
                                      bool mergeWithExisting) {
    if (newPoints.empty()) {
        return true;
    }

    FloatGrid grid(DpPbg::PIXEL_TILE_SIZE);

    if (mergeWithExisting) {
        std::vector<uint8_t> existingBlob;
        bool hasExistingData = GetPbgBlob(tile, existingBlob);

        if (hasExistingData) {
            if (!PbgSeedsReaderWriter::Deserialize(existingBlob, grid)) {
                return false;
            }
        }
    }

    grid.RasterizePoints(newPoints, tile);

    outBlob = PbgSeedsReaderWriter::Serialize(grid);

    return true;
}

bool PbgTileDatabase::RasterizeToBlob(const Rcl& tile,
                                      const std::vector<Position3D>& newPoints,
                                      const std::vector<uint8_t>* existingBlob,
                                      std::vector<uint8_t>& outBlob) {
    if (newPoints.empty()) {
        return true;
    }

    FloatGrid grid(DpPbg::PIXEL_TILE_SIZE);

    if (existingBlob && !existingBlob->empty()) {
        if (!PbgSeedsReaderWriter::Deserialize(*existingBlob, grid)) {
            wxLogError("RasterizeToBlob: failed to deserialize existing tile");
            return false;
        }
    }

    grid.RasterizePoints(newPoints, tile);
    outBlob = PbgSeedsReaderWriter::Serialize(grid);
    return true;
}

bool PbgTileDatabase::IsEmpty() {
    try {
        wxSQLite3Database db;
        db.Open(m_dbPath);
        wxSQLite3ResultSet result =
            db.ExecuteQuery("SELECT 1 FROM MX_RCL LIMIT 1");
        bool hasRow = result.NextRow();
        db.Close();
        return !hasRow;
    } catch (const wxSQLite3Exception&) {
        // Table missing / brand-new file => treat as empty.
        return true;
    }
}