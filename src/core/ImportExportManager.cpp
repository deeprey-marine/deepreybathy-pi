/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "ImportExportManager.h"
#include <wx/textfile.h>
#include <wx/log.h>
#include <wx/wxsqlite3.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "PbgPyramidBuilder.h"
#include "PbgImportFilter.h"
#include "RclWorld.h"
#include "DpPbg.h"

ImportExportManager::ImportExportManager() {
    m_database.Initialize();
}

ImportExportManager::~ImportExportManager() {
}

bool ImportExportManager::ImportXYZFile(const wxString& xyzPath, 
                                       const PbgImportFilterConfig& filterConfig,
                                       wxProgressDialog* progressDlg) {
    if (!wxFileExists(xyzPath)) {
        wxLogError("File does not exist: %s", xyzPath);
        return false;
    }
    
    try {
        // 1. Detect file format
        XYZFileInfo fileInfo;
        if (!DetectXYZFormat(xyzPath, fileInfo)) {
            wxLogError("Unable to detect XYZ file format");
            return false;
        }
        
        wxLogMessage("XYZ file detected: ~%zu lines, separator='%c', skip=%d lines", 
                    fileInfo.estimatedLines, fileInfo.separator, fileInfo.skipLines);
        
        // 2. Open file in streaming mode
        std::ifstream file(xyzPath.ToStdString());
        if (!file.is_open()) {
            wxLogError("Cannot open file: %s", xyzPath);
            return false;
        }

        // Total size for byte-based read progress (accurate & monotonic, unlike
        // the line-count estimate).
        file.seekg(0, std::ios::end);
        std::streamoff fileSizeBytes = file.tellg();
        file.seekg(0, std::ios::beg);

        // Adapt the dialog to the shared progress-callback signature so the same
        // code path drives both the dialog and the async-callback overloads.
        ProgressCallback progressCb = [progressDlg](int p, const wxString& s) -> bool {
            return progressDlg ? progressDlg->Update(p, s) : true;
        };

        // 3. Skip header lines if necessary
        std::string line;
        for (int i = 0; i < fileInfo.skipLines; i++) {
            std::getline(file, line);
            wxLogMessage("Skipping header line %d: '%s'", i + 1, line.c_str());
        }

        // 4. Prepare structures
        std::map<wxString, TileAccumulator> globalTileMap;
        std::unordered_set<Rcl, RclHash> processedTiles;  // unique tiles touched (for pyramid)
        const int DEFAULT_LEVEL = DpPbg::DEFAULT_IMPORT_LEVEL;
        const bool dbWasEmpty = m_database.IsEmpty();  // enables fresh-import read-skip

        size_t totalLinesRead = 0;
        size_t totalValidPoints = 0;    // Points valides
        size_t totalImportedPoints = 0;
        size_t totalFiltered = 0;
        size_t accumulatedPoints = 0;   // points currently held in globalTileMap
        size_t bytesRead = 0;
        int chunkNumber = 0;

        // Reading is budgeted 0-60%, the final save 60-65%, the pyramid 65-100%.
        auto readPercent = [&]() -> int {
            if (fileSizeBytes <= 0) return 0;
            int p = static_cast<int>(60.0 * static_cast<double>(bytesRead) /
                                     static_cast<double>(fileSizeBytes));
            return std::min(p, 60);
        };

        // Each in-read flush advances the bar by ~one accumulation window's share
        // of the read band, so "Saving..." visibly moves instead of holding still.
        size_t estDataLines = (fileInfo.estimatedLines > static_cast<size_t>(fileInfo.skipLines))
            ? fileInfo.estimatedLines - fileInfo.skipLines : fileInfo.estimatedLines;
        int flushBandPct = (estDataLines > 0)
            ? std::max(1, static_cast<int>(60.0 * static_cast<double>(MAX_ACCUMULATED_POINTS) /
                                           static_cast<double>(estDataLines)))
            : 3;
        int progressFloor = 0;  // monotonic: the bar never moves backwards

        // 5. Open DB once for the entire import
        wxSQLite3Database db;
        db.Open(m_database.GetDatabasePath());
        db.Begin();
        
        // 6. Buffer for SQL batch
        std::vector<std::pair<Rcl, std::vector<uint8_t>>> sqlBatchBuffer;
        sqlBatchBuffer.reserve(TILE_BATCH_SIZE);
        
        // 7. Read and process by chunks
        while (!file.eof()) {
            chunkNumber++;
            std::vector<Position3D> chunkPoints;
            chunkPoints.reserve(CHUNK_SIZE);
            
            // Read a chunk
            size_t linesInChunk = 0;
            while (linesInChunk < CHUNK_SIZE && std::getline(file, line)) {
                linesInChunk++;
                totalLinesRead++;
                
                double lon, lat, depth;
                wxString wxLine(line);
                
                if (ProcessXYZLine(wxLine, fileInfo, lon, lat, depth)) {
                    double x, y;
                    PbgTileDatabase::LatLonToMercator(lat, lon, x, y);
                    chunkPoints.emplace_back(x, y, depth);
                    totalValidPoints++;
                }

                // Update read progress from bytes consumed (0-60%).
                if (totalLinesRead % 10000 == 0) {
                    std::streamoff pos = file.tellg();
                    if (pos > 0) bytesRead = static_cast<size_t>(pos);
                    if (!progressCb(std::max(readPercent(), progressFloor), "Reading survey data...")) {
                        db.Rollback();
                        file.close();
                        return false;
                    }
                }
            }

            // Apply filters to this chunk
            std::vector<Position3D> filteredChunk = PbgImportFilter::ApplyFilter(chunkPoints, filterConfig);
            size_t filtered = chunkPoints.size() - filteredChunk.size();
            totalFiltered += filtered;
            totalImportedPoints += filteredChunk.size();
            
            if (filtered > 0) {
                wxLogMessage("Chunk %d: %zu/%zu points kept after filtering", 
                            chunkNumber, filteredChunk.size(), chunkPoints.size());
            }
            
            // Organize by tiles
            for (const auto& point : filteredChunk) {
                Position pos = Position::FromMercator(point.x, point.y);
                Rcl tile = RclWorld::GetRcl(pos, DEFAULT_LEVEL);

                wxString key = wxString::Format("%d_%d_%d",
                    tile.Level(), tile.Row(), tile.Column());

                globalTileMap[key].tile = tile;
                globalTileMap[key].points.push_back(point);
                globalTileMap[key].depthRange.ExtendTo(static_cast<float>(point.z));
            }
            accumulatedPoints += filteredChunk.size();

            // Flush to disk once the accumulator gets large. Grid datasets keep
            // every tile far below MAX_POINTS_PER_TILE, so without this the whole
            // file would stay resident in RAM. PrepareTileData merges, and
            // FlushTileMap commits, so tiles spanning a flush boundary are safe.
            if (accumulatedPoints >= MAX_ACCUMULATED_POINTS) {
                // Advance the bar across this flush's band so saving visibly moves.
                int low = std::max(readPercent(), progressFloor);
                int high = std::min(low + flushBandPct, 60);
                if (!FlushTileMap(db, globalTileMap, sqlBatchBuffer, processedTiles,
                                  dbWasEmpty, progressCb, low, high)) {
                    file.close();
                    return false;  // cancelled
                }
                progressFloor = high;  // reading resumes from here, never backwards
                accumulatedPoints = 0;
            }

            wxLogMessage("Chunk %d: %zu lines read, %zu valid points found, %zu tiles active",
                        chunkNumber, linesInChunk, chunkPoints.size(), globalTileMap.size());
        }

        file.close();

        // 8. Flush remaining tiles (60-65%)
        wxLogMessage("\nProcessing remaining %zu tiles...", globalTileMap.size());
        if (!FlushTileMap(db, globalTileMap, sqlBatchBuffer, processedTiles,
                          dbWasEmpty, progressCb, 60, 65)) {
            return false;  // cancelled
        }

        // 9. Commit
        db.Commit();
        db.Close();

        wxLogMessage("\n=== IMPORT SUMMARY ===");
        wxLogMessage("Total lines read: %zu (excluding %d header lines)", totalLinesRead, fileInfo.skipLines);
        wxLogMessage("Valid points found: %zu", totalValidPoints);
        wxLogMessage("Points after filtering: %zu", totalImportedPoints);
        wxLogMessage("Points filtered out: %zu", totalFiltered);
        wxLogMessage("Total tiles created: %zu at level %d", processedTiles.size(), DEFAULT_LEVEL);
        wxLogMessage("Chunks processed: %d", chunkNumber);

        // Build pyramid with ALL processed tiles (65-100%)
        if (!processedTiles.empty()) {
            std::vector<Rcl> allProcessedTiles(processedTiles.begin(), processedTiles.end());
            progressCb(65, "Building depth map...");

            // Map the pyramid's 0-100 progress into 65-100% of the overall bar.
            PbgPyramidBuilder pyramidBuilder(&m_database);
            bool pyramidSuccess = pyramidBuilder.BuildPyramid(allProcessedTiles,
                [&progressCb](int p, const wxString& s) -> bool {
                    return progressCb(65 + p * 35 / 100, s);
                });

            if (!pyramidSuccess) {
                wxLogError("Error building pyramid");
            } else {
                wxLogMessage("Pyramid built successfully");
            }
        } else {
            wxLogWarning("No tiles created, skipping pyramid generation");
        }

        progressCb(100, "Import completed!");

        m_database.LogDatabaseCoverage();
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("Import error: %s", e.what());
        return false;
    }
}

bool ImportExportManager::ImportXYZFile(const wxString& xyzPath,
                                       const PbgImportFilterConfig& filterConfig,
                                       ProgressCallback progressCb) {
    if (!wxFileExists(xyzPath)) {
        wxLogError("File does not exist: %s", xyzPath);
        return false;
    }

    try {
        XYZFileInfo fileInfo;
        if (!DetectXYZFormat(xyzPath, fileInfo)) {
            wxLogError("Unable to detect XYZ file format");
            return false;
        }

        std::ifstream file(xyzPath.ToStdString());
        if (!file.is_open()) {
            wxLogError("Cannot open file: %s", xyzPath);
            return false;
        }

        file.seekg(0, std::ios::end);
        std::streamoff fileSizeBytes = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string line;
        for (int i = 0; i < fileInfo.skipLines; i++) {
            std::getline(file, line);
        }

        std::map<wxString, TileAccumulator> globalTileMap;
        std::unordered_set<Rcl, RclHash> processedTiles;
        const int DEFAULT_LEVEL = DpPbg::DEFAULT_IMPORT_LEVEL;
        const bool dbWasEmpty = m_database.IsEmpty();  // enables fresh-import read-skip

        size_t totalLinesRead = 0;
        size_t totalValidPoints = 0;
        size_t totalImportedPoints = 0;
        size_t totalFiltered = 0;
        size_t accumulatedPoints = 0;
        size_t bytesRead = 0;
        int chunkNumber = 0;

        // Reading 0-60%, final save 60-65%, pyramid 65-100%.
        auto readPercent = [&]() -> int {
            if (fileSizeBytes <= 0) return 0;
            int p = static_cast<int>(60.0 * static_cast<double>(bytesRead) /
                                     static_cast<double>(fileSizeBytes));
            return std::min(p, 60);
        };

        size_t estDataLines = (fileInfo.estimatedLines > static_cast<size_t>(fileInfo.skipLines))
            ? fileInfo.estimatedLines - fileInfo.skipLines : fileInfo.estimatedLines;
        int flushBandPct = (estDataLines > 0)
            ? std::max(1, static_cast<int>(60.0 * static_cast<double>(MAX_ACCUMULATED_POINTS) /
                                           static_cast<double>(estDataLines)))
            : 3;
        int progressFloor = 0;  // monotonic: the bar never moves backwards

        wxSQLite3Database db;
        db.Open(m_database.GetDatabasePath());
        db.Begin();

        std::vector<std::pair<Rcl, std::vector<uint8_t>>> sqlBatchBuffer;
        sqlBatchBuffer.reserve(TILE_BATCH_SIZE);

        while (!file.eof()) {
            chunkNumber++;
            std::vector<Position3D> chunkPoints;
            chunkPoints.reserve(CHUNK_SIZE);

            size_t linesInChunk = 0;
            while (linesInChunk < CHUNK_SIZE && std::getline(file, line)) {
                linesInChunk++;
                totalLinesRead++;

                double lon, lat, depth;
                wxString wxLine(line);

                if (ProcessXYZLine(wxLine, fileInfo, lon, lat, depth)) {
                    double x, y;
                    PbgTileDatabase::LatLonToMercator(lat, lon, x, y);
                    chunkPoints.emplace_back(x, y, depth);
                    totalValidPoints++;
                }

                if (progressCb && totalLinesRead % 10000 == 0) {
                    std::streamoff pos = file.tellg();
                    if (pos > 0) bytesRead = static_cast<size_t>(pos);
                    if (!progressCb(std::max(readPercent(), progressFloor), "Reading survey data...")) {
                        db.Rollback();
                        file.close();
                        return false;
                    }
                }
            }

            std::vector<Position3D> filteredChunk = PbgImportFilter::ApplyFilter(chunkPoints, filterConfig);
            size_t filtered = chunkPoints.size() - filteredChunk.size();
            totalFiltered += filtered;
            totalImportedPoints += filteredChunk.size();

            for (const auto& point : filteredChunk) {
                Position pos = Position::FromMercator(point.x, point.y);
                Rcl tile = RclWorld::GetRcl(pos, DEFAULT_LEVEL);

                wxString key = wxString::Format("%d_%d_%d",
                    tile.Level(), tile.Row(), tile.Column());

                globalTileMap[key].tile = tile;
                globalTileMap[key].points.push_back(point);
                globalTileMap[key].depthRange.ExtendTo(static_cast<float>(point.z));
            }
            accumulatedPoints += filteredChunk.size();

            // Bound peak memory: flush the accumulator to disk periodically.
            if (accumulatedPoints >= MAX_ACCUMULATED_POINTS) {
                int low = std::max(readPercent(), progressFloor);
                int high = std::min(low + flushBandPct, 60);
                if (!FlushTileMap(db, globalTileMap, sqlBatchBuffer, processedTiles,
                                  dbWasEmpty, progressCb, low, high)) {
                    file.close();
                    return false;  // cancelled
                }
                progressFloor = high;
                accumulatedPoints = 0;
            }
        }

        file.close();

        // Final save (60-65%)
        if (!FlushTileMap(db, globalTileMap, sqlBatchBuffer, processedTiles,
                          dbWasEmpty, progressCb, 60, 65)) {
            return false;  // cancelled
        }

        db.Commit();
        db.Close();

        // Pyramid (65-100%)
        if (!processedTiles.empty()) {
            if (progressCb) {
                progressCb(65, "Building depth map...");
            }

            std::vector<Rcl> allProcessedTiles(processedTiles.begin(), processedTiles.end());
            PbgPyramidBuilder pyramidBuilder(&m_database);
            pyramidBuilder.BuildPyramid(allProcessedTiles,
                [&progressCb](int p, const wxString& s) -> bool {
                    return progressCb ? progressCb(65 + p * 35 / 100, s) : true;
                });
        }

        if (progressCb) {
            progressCb(100, "Import completed!");
        }

        m_database.LogDatabaseCoverage();
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("Import error: %s", e.what());
        return false;
    }
}

bool ImportExportManager::ExportAllToXYZ(const wxString& filepath, ProgressCallback progressCb) {
    try {
        std::vector<Rcl> tiles;
        if (!m_database.GetAvailableTiles(tiles, DpPbg::DEFAULT_EXPORT_LEVEL)) {
            wxLogError("ExportAllToXYZ: failed to get available tiles");
            return false;
        }

        if (tiles.empty()) {
            wxLogWarning("ExportAllToXYZ: no tiles found at level %d", DpPbg::DEFAULT_EXPORT_LEVEL);
            return false;
        }

        std::ofstream file(filepath.ToStdString());
        if (!file.is_open()) {
            wxLogError("ExportAllToXYZ: cannot open file: %s", filepath);
            return false;
        }

        file << "longitude,latitude,depth\n";

        size_t totalPoints = 0;
        int processedTiles = 0;
        int totalTiles = static_cast<int>(tiles.size());

        for (const auto& tile : tiles) {
            std::vector<Position3D> tilePoints;
            if (m_database.GetPointsForTile(tile, tilePoints)) {
                for (const auto& pt : tilePoints) {
                    double lat, lon;
                    PbgTileDatabase::MercatorToLatLon(pt.x, pt.y, lat, lon);
                    file << wxString::Format("%.6f,%.6f,%.2f", lon, lat, pt.z).ToStdString() << "\n";
                    totalPoints++;
                }
            }

            processedTiles++;
            if (progressCb && processedTiles % 10 == 0) {
                int percent = (processedTiles * 100) / totalTiles;
                if (!progressCb(percent,
                    wxString::Format("Exporting tile %d/%d (%zu points)...",
                                    processedTiles, totalTiles, totalPoints))) {
                    file.close();
                    return false;
                }
            }
        }

        file.close();

        wxLogMessage("ExportAllToXYZ: exported %zu points from %d tiles to %s",
                    totalPoints, totalTiles, filepath);
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("ExportAllToXYZ error: %s", e.what());
        return false;
    }
}

bool ImportExportManager::DetectXYZFormat(const wxString& filepath, XYZFileInfo& info) {
    std::ifstream file(filepath.ToStdString());
    if (!file.is_open()) {
        return false;
    }
    
    info.format = XYZFormat::UNKNOWN;
    info.separator = ',';
    info.skipLines = 0;
    info.hasHeader = false;
    
    // Read first lines to detect format
    std::vector<std::string> sampleLines;
    std::string line;
    int lineCount = 0;
    
    while (std::getline(file, line) && lineCount < 10) {
        sampleLines.push_back(line);
        lineCount++;
    }
    
    if (sampleLines.empty()) {
        return false;
    }
    
    // Detect separator
    std::vector<char> possibleSeparators = {',', '\t', ' ', ';', '|'};
    std::map<char, int> separatorCounts;
    
    // Count separators in each line
    for (size_t i = 0; i < sampleLines.size(); i++) {
        // Skip empty lines
        if (sampleLines[i].empty()) continue;
        
        for (char sep : possibleSeparators) {
            int count = std::count(sampleLines[i].begin(), sampleLines[i].end(), sep);
            if (count >= 2) {  // At least 2 separators for 3 columns
                separatorCounts[sep]++;
            }
        }
    }
    
    // Special check for comma with spaces (", ")
    bool hasCommaSpace = false;
    for (const auto& sampleLine : sampleLines) {
        if (sampleLine.find(", ") != std::string::npos) {
            hasCommaSpace = true;
            break;
        }
    }
    
    // Choose most frequent separator, prefer comma if comma+space detected
    int maxCount = 0;
    for (const auto& pair : separatorCounts) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            info.separator = pair.first;
        }
    }
    
    // Override with comma if we detected comma+space pattern
    if (hasCommaSpace && separatorCounts.find(',') != separatorCounts.end()) {
        info.separator = ',';
        wxLogMessage("Detected comma with spaces pattern, using comma as separator");
    }
    
    wxLogMessage("Separator detection: chosen '%c' (ASCII %d)", info.separator, (int)info.separator);
    
    // Detect header and format
    wxString firstLine(sampleLines[0]);
    wxString firstLineLower = firstLine.Lower();
    
    // Check for common headers
    if (firstLineLower.Contains("long") || firstLineLower.Contains("lon") || 
        firstLineLower.Contains("lat") || firstLineLower.Contains("depth") || 
        firstLineLower.Contains("elev") || firstLineLower.Contains("x") || 
        firstLineLower.Contains("y") || firstLineLower.Contains("z")) {
        
        info.hasHeader = true;
        info.skipLines = 1;
        
        wxLogMessage("Header line detected: '%s'", firstLine);
        
        // Split header to analyze column order
        wxArrayString headerTokens;
        if (info.separator == ',') {
            wxString normalizedHeader = firstLineLower;
            normalizedHeader.Replace(", ", ",");
            headerTokens = wxSplit(normalizedHeader, ',');
        } else {
            headerTokens = wxSplit(firstLineLower, info.separator);
        }
        
        // Clean tokens
        for (size_t i = 0; i < headerTokens.Count(); i++) {
            headerTokens[i].Trim(true).Trim(false);
        }
        
        wxLogMessage("Header tokens: ['%s', '%s', '%s']",
                    headerTokens.Count() > 0 ? headerTokens[0] : "?",
                    headerTokens.Count() > 1 ? headerTokens[1] : "?",
                    headerTokens.Count() > 2 ? headerTokens[2] : "?");
        
        // Determine format from header tokens
        if (headerTokens.Count() >= 3) {
            wxString col1 = headerTokens[0];
            wxString col2 = headerTokens[1];
            wxString col3 = headerTokens[2];
            
            if ((col1.Contains("long") || col1.Contains("lon")) && col2.Contains("lat")) {
                info.format = XYZFormat::LON_LAT_DEPTH;
                wxLogMessage("Format determined from header: longitude, latitude, depth/elevation");
            } else if (col1.Contains("lat") && (col2.Contains("long") || col2.Contains("lon"))) {
                info.format = XYZFormat::LAT_LON_DEPTH;
                wxLogMessage("Format determined from header: latitude, longitude, depth/elevation");
            } else if (col1 == "x" && col2 == "y" && col3 == "z") {
                info.format = XYZFormat::X_Y_Z;
                wxLogMessage("Format determined from header: x, y, z");
            }
        }
    }
    
    // If no header detected or format still unknown, try to guess from values
    if (info.format == XYZFormat::UNKNOWN) {
        // Take first data line
        int dataLineIndex = info.hasHeader ? 1 : 0;
        if (dataLineIndex < sampleLines.size()) {
            wxString wxDataLine(sampleLines[dataLineIndex]);
            wxArrayString tokens = wxSplit(wxDataLine, info.separator);
            
            if (tokens.Count() >= 3) {
                // Clean tokens
                for (size_t i = 0; i < tokens.Count(); i++) {
                    tokens[i].Trim(true).Trim(false);
                }
                
                double val1, val2, val3;
                if (tokens[0].ToDouble(&val1) && tokens[1].ToDouble(&val2) && tokens[2].ToDouble(&val3)) {
                    // When both columns fall within latitude range (|v| <= 90) a
                    // single line can't tell lat,lon from lon,lat. Disambiguate
                    // using the sample: gridded bathymetry (e.g. EMODnet) is
                    // scanned row-major, so one coordinate is constant across
                    // consecutive samples while the other steps. The stepping
                    // column is the inner (fast) axis = longitude. This is
                    // order-agnostic: it works whether the file is lon,lat or
                    // lat,lon, as long as it is a row/column-ordered grid.
                    if (std::abs(val1) <= 90 && std::abs(val2) <= 90) {
                        bool c1Varies = false, c2Varies = false;
                        for (size_t i = dataLineIndex; i < sampleLines.size(); i++) {
                            wxArrayString t = wxSplit(wxString(sampleLines[i]), info.separator);
                            if (t.Count() < 3) continue;
                            double a, b;
                            if (t[0].Trim(true).Trim(false).ToDouble(&a) &&
                                t[1].Trim(true).Trim(false).ToDouble(&b)) {
                                if (std::abs(a - val1) > 1e-9) c1Varies = true;
                                if (std::abs(b - val2) > 1e-9) c2Varies = true;
                            }
                        }
                        if (c2Varies && !c1Varies) {
                            info.format = XYZFormat::LAT_LON_DEPTH;
                            wxLogMessage("Guessed format: lat,lon,depth (col1 constant, col2 steps -> grid)");
                        } else {
                            // col1 steps (or undecidable): treat col1 as longitude.
                            // Also the GIS-conventional default for XYZ files.
                            info.format = XYZFormat::LON_LAT_DEPTH;
                            wxLogMessage("Guessed format: lon,lat,depth (col1 steps / default)");
                        }
                    } else if (std::abs(val1) <= 90 && std::abs(val2) <= 180) {
                        info.format = XYZFormat::LAT_LON_DEPTH;
                        wxLogMessage("Guessed format: lat,lon,depth (based on value ranges)");
                    } else if (std::abs(val1) <= 180 && std::abs(val2) <= 90) {
                        info.format = XYZFormat::LON_LAT_DEPTH;
                        wxLogMessage("Guessed format: lon,lat,depth (based on value ranges)");
                    } else if (std::abs(val1) > 180 || std::abs(val2) > 180) {
                        // Probably already in projected coordinates
                        info.format = XYZFormat::X_Y_Z;
                        wxLogMessage("Guessed format: x,y,z (values exceed geographic ranges)");
                    } else {
                        // Default to lon,lat,depth
                        info.format = XYZFormat::LON_LAT_DEPTH;
                        wxLogMessage("Using default format: lon,lat,depth");
                    }
                }
            }
        }
    }
    
    // Estimate number of lines
    file.clear();
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.close();
    
    // Better estimation based on average of first few lines
    size_t totalLineLength = 0;
    size_t validLines = 0;
    for (size_t i = info.skipLines; i < sampleLines.size(); i++) {
        if (!sampleLines[i].empty()) {
            totalLineLength += sampleLines[i].length() + 1; // +1 for newline
            validLines++;
        }
    }
    
    if (validLines > 0) {
        size_t avgLineSize = totalLineLength / validLines;
        info.estimatedLines = fileSize / avgLineSize;
    } else {
        // Fallback estimation
        info.estimatedLines = fileSize / 30; // Assume 30 chars per line average
    }
    
    wxLogMessage("File analysis complete:");
    wxLogMessage("  Format: %s", 
                info.format == XYZFormat::LON_LAT_DEPTH ? "longitude,latitude,depth" :
                info.format == XYZFormat::LAT_LON_DEPTH ? "latitude,longitude,depth" :
                info.format == XYZFormat::X_Y_Z ? "x,y,z" : "unknown");
    wxLogMessage("  Separator: '%c' (ASCII %d)", info.separator, (int)info.separator);
    wxLogMessage("  Header lines to skip: %d", info.skipLines);
    wxLogMessage("  Estimated total lines: %zu", info.estimatedLines);
    
    return info.format != XYZFormat::UNKNOWN;
}

bool ImportExportManager::ProcessXYZLine(const wxString& line, const XYZFileInfo& info,
                                        double& lon, double& lat, double& depth) {
    wxString trimmed = line;
    trimmed.Trim();
    
    // Ignore empty lines or comments
    if (trimmed.IsEmpty() || trimmed.StartsWith("#") || trimmed.StartsWith("//")) {
        return false;
    }
    
    // Split tokens
    wxArrayString tokens;
    
    // Special handling for comma with spaces
    if (info.separator == ',') {
        // Replace ", " with just "," to normalize
        wxString normalized = trimmed;
        normalized.Replace(", ", ",");
        tokens = wxSplit(normalized, ',');
    } else {
        tokens = wxSplit(trimmed, info.separator);
    }
    
    // Clean each token (remove leading/trailing spaces)
    for (size_t i = 0; i < tokens.Count(); i++) {
        tokens[i].Trim(true).Trim(false);
    }
    
    // Debug log for first few lines
    static int debugLineCount = 0;
    if (debugLineCount < 5) {
        wxLogMessage("Line %d: '%s'", debugLineCount + 1, trimmed);
        wxLogMessage("  Tokens (%zu): ['%s', '%s', '%s']", 
                    tokens.Count(),
                    tokens.Count() > 0 ? tokens[0] : "?",
                    tokens.Count() > 1 ? tokens[1] : "?",
                    tokens.Count() > 2 ? tokens[2] : "?");
        debugLineCount++;
    }
    
    if (tokens.Count() < 3) {
        return false;
    }
    
    double val1, val2, val3;
    bool parsed1 = tokens[0].ToDouble(&val1);
    bool parsed2 = tokens[1].ToDouble(&val2);
    bool parsed3 = tokens[2].ToDouble(&val3);
    
    if (!parsed1 || !parsed2 || !parsed3) {
        static int errorCount = 0;
        if (errorCount < 5) {
            wxLogError("Failed to parse line: '%s'", trimmed);
            wxLogError("  Token[0]='%s' parsed=%d val=%f", tokens[0], parsed1, val1);
            wxLogError("  Token[1]='%s' parsed=%d val=%f", tokens[1], parsed2, val2);
            wxLogError("  Token[2]='%s' parsed=%d val=%f", tokens[2], parsed3, val3);
            errorCount++;
        }
        return false;
    }
    
    // Assign according to detected format
    switch (info.format) {
        case XYZFormat::LON_LAT_DEPTH:
            lon = val1;
            lat = val2;
            depth = val3;
            break;
            
        case XYZFormat::LAT_LON_DEPTH:
            lat = val1;
            lon = val2;
            depth = val3;
            break;
            
        case XYZFormat::X_Y_Z:
            // Already in Mercator, convert to lat/lon
            PbgTileDatabase::MercatorToLatLon(val1, val2, lat, lon);
            depth = val3;
            break;
            
        default:
            // Default to lon,lat,depth
            lon = val1;
            lat = val2;
            depth = val3;
            break;
    }
    
    // Validate values with debug info
    if (lat < -90.0 || lat > 90.0) {
        static int latErrorCount = 0;
        if (latErrorCount < 5) {
            wxLogError("Invalid latitude: %f (must be between -90 and 90)", lat);
            latErrorCount++;
        }
        return false;
    }
    
    if (lon < -180.0 || lon > 180.0) {
        static int lonErrorCount = 0;
        if (lonErrorCount < 5) {
            wxLogError("Invalid longitude: %f (must be between -180 and 180)", lon);
            lonErrorCount++;
        }
        return false;
    }
    
    // Validate depth (between -20000m and +10000m)
    if (depth < -20000.0 || depth > 10000.0) {
        static int depthErrorCount = 0;
        if (depthErrorCount < 5) {
            wxLogError("Invalid depth: %f (must be between -20000 and 10000)", depth);
            depthErrorCount++;
        }
        return false;
    }
    
    // Log first few valid points
    static int validPointCount = 0;
    if (validPointCount < 3) {
        wxLogMessage("Valid point %d: lon=%f, lat=%f, depth=%f", 
                    validPointCount + 1, lon, lat, depth);
        validPointCount++;
    }
    
    return true;
}

bool ImportExportManager::FlushTileMap(wxSQLite3Database& db,
                                       std::map<wxString, TileAccumulator>& tileMap,
                                       std::vector<std::pair<Rcl, std::vector<uint8_t>>>& sqlBatchBuffer,
                                       std::unordered_set<Rcl, RclHash>& processedTiles,
                                       bool dbWasEmpty,
                                       const ProgressCallback& progressCb,
                                       int pctLow, int pctHigh) {
    if (tileMap.empty()) {
        return true;
    }

    const size_t total = tileMap.size();
    size_t done = 0;

    // Reuse ONE prepared SELECT on the import's own connection for all merge
    // reads (instead of GetPbgBlob opening a fresh connection per tile, which is
    // the dominant cost when saving onto an existing database). Reads here see
    // the last committed state (each flush commits), which is the correct merge
    // target. Only needed when merges are possible at all (db not empty).
    wxSQLite3Statement readStmt;
    const bool mayNeedMerge = !dbWasEmpty;
    if (mayNeedMerge) {
        readStmt = db.PrepareStatement(
            "SELECT datablob FROM MX_RCL WHERE l = ? AND c = ? AND r = ?");
    }

    for (auto& pair : tileMap) {
        const TileAccumulator& accumulator = pair.second;
        const Rcl& tile = accumulator.tile;

        // On a fresh import a tile we have not written yet this run cannot exist
        // in the DB, so skip the merge read-back entirely.
        bool isNewTile = dbWasEmpty &&
                         processedTiles.find(tile) == processedTiles.end();

        std::vector<uint8_t> existingBlob;
        const std::vector<uint8_t>* existingPtr = nullptr;
        if (!isNewTile && mayNeedMerge) {
            readStmt.Bind(1, tile.Level());
            readStmt.Bind(2, tile.Column());
            readStmt.Bind(3, tile.Row());
            wxSQLite3ResultSet rs = readStmt.ExecuteQuery();
            if (rs.NextRow()) {
                int blobSize = 0;
                const void* blobData = rs.GetBlob("datablob", blobSize);
                if (blobData && blobSize > 0) {
                    existingBlob.resize(blobSize);
                    memcpy(existingBlob.data(), blobData, blobSize);
                    existingPtr = &existingBlob;
                }
            }
            readStmt.Reset();
        }

        std::vector<uint8_t> blob;
        if (m_database.RasterizeToBlob(tile, accumulator.points, existingPtr, blob)) {
            sqlBatchBuffer.push_back(std::make_pair(tile, blob));
            processedTiles.insert(tile);

            if (sqlBatchBuffer.size() >= TILE_BATCH_SIZE) {
                WriteBatchToDB(db, sqlBatchBuffer);
                sqlBatchBuffer.clear();
            }
        }

        done++;
        if (progressCb && (done % 500 == 0 || done == total)) {
            int pct = pctLow + static_cast<int>((pctHigh - pctLow) * done / total);
            if (!progressCb(pct, "Saving...")) {
                db.Rollback();
                tileMap.clear();
                return false;  // cancelled
            }
        }
    }

    // Drain remaining batch and commit so the next flush's merge reads see it.
    if (!sqlBatchBuffer.empty()) {
        WriteBatchToDB(db, sqlBatchBuffer);
        sqlBatchBuffer.clear();
    }
    db.Commit();
    db.Begin();

    tileMap.clear();
    return true;
}

void ImportExportManager::WriteBatchToDB(wxSQLite3Database& db,
                                        const std::vector<std::pair<Rcl, std::vector<uint8_t>>>& batch) {
    wxLogMessage("Writing batch of %zu tiles to database...", batch.size());
    
    wxSQLite3Statement stmt = db.PrepareStatement(
        "INSERT OR REPLACE INTO MX_RCL (r, c, l, datablob) VALUES (?, ?, ?, ?)"
    );
    
    for (const auto& tileData : batch) {
        const Rcl& tile = tileData.first;
        const std::vector<uint8_t>& blob = tileData.second;
        
        stmt.Bind(1, tile.Row());
        stmt.Bind(2, tile.Column());
        stmt.Bind(3, tile.Level());
        stmt.Bind(4, blob.data(), blob.size());
        stmt.ExecuteUpdate();
        stmt.Reset();
    }
}

bool ImportExportManager::ExportToXYZ(const wxString& filepath, 
                                     double minLat, double minLon, 
                                     double maxLat, double maxLon,
                                     wxProgressDialog* progressDlg) {
    wxTextFile file;
    if (!file.Create(filepath)) {
        return false;
    }
    
    file.AddLine("longitude,latitude,depth");
    
    // Use new method to extract points
    std::vector<Position3D> points;
    if (!m_database.ExportPointsInArea(minLat, minLon, maxLat, maxLon, points, progressDlg)) {
        return false;
    }
    
    // Write points
    for (size_t i = 0; i < points.size(); i++) {
        double lat, lon;
        PbgTileDatabase::MercatorToLatLon(points[i].x, points[i].y, lat, lon);
        
        wxString line = wxString::Format("%.6f,%.6f,%.2f", lon, lat, points[i].z);
        file.AddLine(line);
        
        if (progressDlg && i % 1000 == 0) {
            progressDlg->Pulse(wxString::Format("Writing %zu/%zu points...", i, points.size()));
        }
    }
    
    file.Write();
    file.Close();
    
    return true;
}

bool ImportExportManager::ExportAllToXYZ(const wxString& filepath,
                                         wxProgressDialog* progressDlg) {
    try {
        // Get all tiles at the default export level
        std::vector<Rcl> tiles;
        if (!m_database.GetAvailableTiles(tiles, DpPbg::DEFAULT_EXPORT_LEVEL)) {
            wxLogError("ExportAllToXYZ: failed to get available tiles");
            return false;
        }

        if (tiles.empty()) {
            wxLogWarning("ExportAllToXYZ: no tiles found at level %d", DpPbg::DEFAULT_EXPORT_LEVEL);
            return false;
        }

        std::ofstream file(filepath.ToStdString());
        if (!file.is_open()) {
            wxLogError("ExportAllToXYZ: cannot open file: %s", filepath);
            return false;
        }

        file << "longitude,latitude,depth\n";

        size_t totalPoints = 0;
        int processedTiles = 0;
        int totalTiles = static_cast<int>(tiles.size());

        for (const auto& tile : tiles) {
            std::vector<Position3D> tilePoints;
            if (m_database.GetPointsForTile(tile, tilePoints)) {
                for (const auto& pt : tilePoints) {
                    double lat, lon;
                    PbgTileDatabase::MercatorToLatLon(pt.x, pt.y, lat, lon);
                    file << wxString::Format("%.6f,%.6f,%.2f", lon, lat, pt.z).ToStdString() << "\n";
                    totalPoints++;
                }
            }

            processedTiles++;
            if (progressDlg && processedTiles % 10 == 0) {
                int percent = (processedTiles * 100) / totalTiles;
                if (!progressDlg->Update(percent,
                    wxString::Format("Exporting tile %d/%d (%zu points)...",
                                    processedTiles, totalTiles, totalPoints))) {
                    file.close();
                    return false;
                }
            }
        }

        file.close();

        wxLogMessage("ExportAllToXYZ: exported %zu points from %d tiles to %s",
                    totalPoints, totalTiles, filepath);
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("ExportAllToXYZ error: %s", e.what());
        return false;
    }
}

bool ImportExportManager::ExportTileToXYZ(const wxString& filepath, const Rcl& tile,
                                         wxProgressDialog* progressDlg) {
    std::vector<Position3D> points;
    if (!m_database.GetPointsForTile(tile, points)) {
        return false;
    }
    
    wxTextFile file;
    if (!file.Create(filepath)) {
        return false;
    }
    
    file.AddLine("longitude,latitude,depth");
    
    for (size_t i = 0; i < points.size(); i++) {
        double lat, lon;
        PbgTileDatabase::MercatorToLatLon(points[i].x, points[i].y, lat, lon);
        
        wxString line = wxString::Format("%.6f,%.6f,%.2f", lon, lat, points[i].z);
        file.AddLine(line);
        
        if (progressDlg && i % 1000 == 0) {
            int percent = (i * 100) / points.size();
            progressDlg->Update(percent, wxString::Format("Exporting point %zu/%zu", i, points.size()));
        }
    }
    
    file.Write();
    file.Close();
    
    return true;
}

bool ImportExportManager::ClearDatabase() {
    return m_database.ClearAllData();
}

bool ImportExportManager::RemoveDuplicates() {
    return m_database.RemoveDuplicates();
}

bool ImportExportManager::OptimizeDatabase() {
    return m_database.OptimizeDatabase();
}

wxString ImportExportManager::GetDatabaseInfo() const {
    return m_database.GetDatabaseInfo();
}

size_t ImportExportManager::GetTotalPointCount() const {
    return m_database.GetTotalPointsCount();
}

double ImportExportManager::GetDatabaseSizeMB() const { return m_database.GetDatabaseSizeMB(); }
wxString ImportExportManager::GetLastModifiedTime() const { return m_database.GetLastModifiedTime(); }

bool ImportExportManager::RebuildPyramid() {
    // Create progress dialog for rebuild
    wxProgressDialog progressDlg(
        "Rebuilding pyramid",
        "Initializing...",
        100,
        nullptr,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_ESTIMATED_TIME
    );
    
    progressDlg.Show();
    
    PbgPyramidBuilder builder(&m_database);
    bool success = builder.RebuildFromDatabase(&progressDlg);
    
    if (success) {
        progressDlg.Update(100, "Pyramid rebuilt successfully!");
        wxLogMessage("Pyramid rebuilt successfully");
    } else {
        wxLogError("Failed to rebuild pyramid");
    }
    
    return success;
}