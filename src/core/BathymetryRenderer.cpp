/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "BathymetryRenderer.h"
#include "RclExtensions.h"
#include "RclWorld.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <wx/log.h>
#include "DpPbg.h"
#include <algorithm>
#include <cmath>
#include "BathymetryDataManager.h"

// Auto interval lookup table: [viewport level][density-1]
// Density: 1=Low, 2=Medium, 3=High
// Values in meters: 0.5, 1, 2, 5, 10, 15, 20, 50, 100
// NOTE: Minimum interval clamped to reduce "contour spaghetti" from sonar noise
//       at high zoom levels. 0.5m only available for High density at extreme zoom.
static const float kAutoIntervalTable[23][3] = {
    /* 0  */ {100.0f, 50.0f, 50.0f},
    /* 1  */ {100.0f, 50.0f, 50.0f},
    /* 2  */ {100.0f, 50.0f, 50.0f},
    /* 3  */ {100.0f, 50.0f, 50.0f},
    /* 4  */ {50.0f, 50.0f, 20.0f},
    /* 5  */ {50.0f, 20.0f, 15.0f},
    /* 6  */ {50.0f, 20.0f, 10.0f},
    /* 7  */ {50.0f, 20.0f, 10.0f},
    /* 8  */ {20.0f, 15.0f, 5.0f},
    /* 9  */ {20.0f, 10.0f, 5.0f},
    /* 10 */ {20.0f, 10.0f, 5.0f},
    /* 11 */ {15.0f, 10.0f, 5.0f},
    /* 12 */ {15.0f, 5.0f, 2.0f},
    /* 13 */ {10.0f, 5.0f, 2.0f},
    /* 14 */ {10.0f, 5.0f, 2.0f},
    /* 15 */ {5.0f, 2.0f, 1.0f},
    /* 16 */ {5.0f, 2.0f, 1.0f},
    /* 17 */ {5.0f, 2.0f, 1.0f},
    /* 18 */ {2.0f, 2.0f, 1.0f},   // Medium: 1m->2m, High: 0.5m->1m (reduce noise)
    /* 19 */ {2.0f, 2.0f, 1.0f},   // Medium: 1m->2m, High: 0.5m->1m
    /* 20 */ {2.0f, 2.0f, 1.0f},   // Medium: 1m->2m, High: 0.5m->1m
    /* 21 */ {2.0f, 1.0f, 0.5f},   // Low: 1m->2m, Medium: 0.5m->1m
    /* 22 */ {1.0f, 1.0f, 0.5f}    // Low: 0.5m->1m, Medium: 0.5m->1m
};

BathymetryRenderer::BathymetryRenderer()
    : m_initialized(false)
    , m_enabled(true)
    , m_quadVAO(0)
    , m_quadVBO(0)
    , m_brushSizeFactor(DpPbg::DEFAULT_BRUSH_SIZE_FACTOR)
    , m_tilesRecalculated(false)
    , m_viewportLevel(DpPbg::DEFAULT_VIEWPORT_LEVEL)
    , m_showTileFrame(false)
    , m_lastFrameTime(std::chrono::steady_clock::now())
{
    memset(&m_lastViewport, 0, sizeof(m_lastViewport));
    m_lastViewport.bValid = false;

    m_brushSize = DpPbg::BrushSizeConfig::CalculateBrushSize(
            m_viewportLevel,
            m_brushSizeFactor,
            DpPbg::PIXEL_TILE_SIZE,
            DpPbg::DEFAULT_IMPORT_LEVEL
        );
}

BathymetryRenderer::~BathymetryRenderer() {
    Cleanup();
}

bool BathymetryRenderer::Initialize() {
    if (m_initialized) return true;
    
    try {
        m_pointRenderer = std::make_unique<PbgPointRenderer>();
        if (!m_pointRenderer->Initialize()) {
            wxLogError("Failed to initialize point renderer");
            return false;
        }
        
        m_dilationRenderer = std::make_unique<PbgDilationRenderer>();
        if (!m_dilationRenderer->Initialize()) {
            wxLogError("Failed to initialize dilation renderer");
            return false;
        }
        
        m_unifiedShader = std::make_unique<PbgUnifiedShader>();
        if (!m_unifiedShader->Initialize()) {
            wxLogError("Failed to initialize unified shader");
            return false;
        }
        m_isobathsEnabled = false;
        
        CreateQuadVAO();
        
        m_globalWorkElevation = std::make_unique<PbgTexture>(DpPbg::MAX_WORK_TEXTURE_SIZE, DpPbg::MAX_WORK_TEXTURE_SIZE, true);
        m_globalWorkWeight = std::make_unique<PbgTexture>(DpPbg::MAX_WORK_TEXTURE_SIZE, DpPbg::MAX_WORK_TEXTURE_SIZE, true);
        
        if (!m_globalWorkElevation->Initialize() || !m_globalWorkWeight->Initialize()) {
            wxLogError("Failed to initialize global work textures");
            return false;
        }
        
        
        m_dataManager = std::make_unique<BathymetryDataManager>();
        if (!m_dataManager->Initialize()) {
            wxLogError("Failed to initialize data manager");
            return false;
        }
        

        m_dataManager->SetExtendedGridReadyCallback([this](const Rcl& tile) {
            OnSeedsReady(tile);
        });

        m_renderCache = std::make_unique<PbgRenderCacheManager>();

        m_renderCache->SetDataManager(m_dataManager.get());

        

        m_renderCache->Initialize(DpPbg::DEFAULT_MAX_CACHED_TEXTURE, DpPbg::MAX_TILES_PER_FRAME);

        m_dataManager->SetGridCompleteChecker(
            [this](const Rcl& tile) { 
                return m_renderCache->IsTileComplete(tile); 
            }
        );
        
        m_renderCache->SetRenderFunction(
        [this](const Rcl& tile, PbgTexture* texture,
               const CachedExtendedGrid& seeds,
               GLint defaultFbo) {
            return RenderGridToTextureWithExtendedGrid(tile, texture, seeds, defaultFbo);
        });
        

        PbgRenderCacheManager::RenderParameters initialParams;
            initialParams.brushSizeFactor = m_brushSizeFactor;

            m_renderCache->NotifyParameterChange(initialParams);

        m_watchdogTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            if (!m_renderCache) return;
    
            int obsoleteCount = m_renderCache->WatchdogCheck();
    
            if (obsoleteCount > 0 && m_refreshCallback) {
                m_refreshCallback();
            } else {
                m_watchdogTimer.Stop();
            }
        });

        
        m_renderParams.SetTargetDepthRange(m_dataManager->GetGlobalDepthRange());

        // Seed both per-canvas slots from the initialized live members so the first
        // RenderOverlay->LoadCanvasState gets sane state (valid initial depth range,
        // invalid viewport, etc.) instead of default-constructed garbage.
        SaveCanvasState(0);
        SaveCanvasState(1);

        m_initialized = true;
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("Exception in Initialize: %s", e.what());
        return false;
    }
}

void BathymetryRenderer::OnSeedsReady(const Rcl& tile) {
    m_needsRefresh = true;
    
    if (m_refreshCallback) {
        wxTheApp->CallAfter([this]() {
            if (m_refreshCallback) {
                m_refreshCallback();
            }
        });
    }
}

void BathymetryRenderer::Cleanup() {

    m_watchdogTimer.Stop();

    if (m_dataManager) {
        m_dataManager->SetGridCompleteChecker(nullptr);
    }

    if (m_quadVAO) {
        glDeleteVertexArrays(1, &m_quadVAO);
        m_quadVAO = 0;
    }
    if (m_quadVBO) {
        glDeleteBuffers(1, &m_quadVBO);
        m_quadVBO = 0;
    }
    
    if (m_renderCache) {
        m_renderCache->Shutdown();
        m_renderCache.reset();
    }
    
    if (m_globalWorkElevation) {
        m_globalWorkElevation.reset();
    }
    if (m_globalWorkWeight) {
        m_globalWorkWeight.reset();
    }
    
    if (m_dataManager) {
        m_dataManager->Shutdown();
    }
    
    m_initialized = false;
}

void BathymetryRenderer::CreateQuadVAO() {
    // UV coords map the inner 256x256 data region of the 258x258 padded texture,
    // skipping the 1px ghost border on each side
    float uvMin = 1.0f / static_cast<float>(DpPbg::PADDED_TILE_SIZE);   // 1/258
    float uvMax = static_cast<float>(DpPbg::PADDED_TILE_SIZE - 1) / static_cast<float>(DpPbg::PADDED_TILE_SIZE);  // 257/258

    float quadVertices[] = {
        -1.0f,  1.0f,  uvMin, uvMax,
        -1.0f, -1.0f,  uvMin, uvMin,
         1.0f, -1.0f,  uvMax, uvMin,

        -1.0f,  1.0f,  uvMin, uvMax,
         1.0f, -1.0f,  uvMax, uvMin,
         1.0f,  1.0f,  uvMax, uvMax
    };
    
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glBindVertexArray(0);
}

void BathymetryRenderer::UpdateViewport(PlugIn_ViewPort* vp) {
    if (!vp || !vp->bValid) return;
    
    bool forceRecalc = m_forceRecalculation;
    m_forceRecalculation = false;

    if (forceRecalc || IsViewportDifferent(vp)) {
        m_lastViewport = *vp;
              
        int newLevel = CalculateAppropriateLevel(m_lastViewport.view_scale_ppm);
        bool levelChanged = (newLevel != m_viewportLevel);
        
        if (levelChanged) {
            m_viewportLevel = newLevel;

            // Recalculate brush size for the new zoom level using the
            // level-based lookup table.
            m_brushSize = DpPbg::BrushSizeConfig::CalculateBrushSize(
                m_viewportLevel,
                m_brushSizeFactor,
                DpPbg::PIXEL_TILE_SIZE,
                DpPbg::DEFAULT_IMPORT_LEVEL
            );

            // Update auto isoline step if in Auto mode
            UpdateAutoIsolineStep();

            m_tilesBoundingBox.invalidate();
        }
        
        bool needsRecalculation = false;
        
        if (forceRecalc || !m_tilesBoundingBox.isValid || levelChanged) {
            needsRecalculation = true;
        } else {
            double viewCenterX, viewCenterY;
            PbgTileDatabase::LatLonToMercator(vp->clat, vp->clon, viewCenterX, viewCenterY);
            
            double viewMinX, viewMinY, viewMaxX, viewMaxY;
            PbgTileDatabase::LatLonToMercator(vp->lat_min, vp->lon_min, viewMinX, viewMinY);
            PbgTileDatabase::LatLonToMercator(vp->lat_max, vp->lon_max, viewMaxX, viewMaxY);
            
            if (!m_tilesBoundingBox.contains(viewMinX, viewMinY, m_viewportLevel) ||
                !m_tilesBoundingBox.contains(viewMaxX, viewMaxY, m_viewportLevel) ||
                !m_tilesBoundingBox.contains(viewMinX, viewMaxY, m_viewportLevel) ||
                !m_tilesBoundingBox.contains(viewMaxX, viewMinY, m_viewportLevel)) {
                needsRecalculation = true;
            }
        }
        
        if (needsRecalculation) {
            CalculateVisibleTiles();
            m_tilesRecalculated = true;

            if (m_renderCache) {
                std::vector<Rcl> unionVisible  = m_currentVisibleTiles;
                std::vector<Rcl> unionAdjacent = m_currentAdjacentTiles;
                const CanvasState& other = m_canvasState[1 - m_activeCanvasIndex];
                unionVisible.insert(unionVisible.end(),
                    other.currentVisibleTiles.begin(), other.currentVisibleTiles.end());
                unionAdjacent.insert(unionAdjacent.end(),
                    other.currentAdjacentTiles.begin(), other.currentAdjacentTiles.end());
                m_renderCache->NotifyViewportChange(unionVisible, unionAdjacent);
            }
        }
    }
}

bool BathymetryRenderer::IsViewportDifferent(const PlugIn_ViewPort* vp) const {
    const double EPSILON = DpPbg::VIEWPORT_EPSILON;
    
    if (std::abs(vp->clat - m_lastViewport.clat) > EPSILON ||
        std::abs(vp->clon - m_lastViewport.clon) > EPSILON) {
        return true;
    }
    
    if (std::abs(vp->view_scale_ppm - m_lastViewport.view_scale_ppm) > EPSILON) {
        return true;
    }
    
    if (vp->pix_width != m_lastViewport.pix_width ||
        vp->pix_height != m_lastViewport.pix_height) {
        return true;
    }
    
    return false;
}

int BathymetryRenderer::CalculateAppropriateLevel(double viewScale) const {

    double metersPerPixel = (1.0 / viewScale) / DpPbg::LOD_SWITCH_BIAS;

    if (metersPerPixel <  DpPbg::MIN_METERS_PER_PIXEL) metersPerPixel = DpPbg::MIN_METERS_PER_PIXEL ;
    
    Position centerPos = Position::FromLatLon(
        (m_lastViewport.lat_min + m_lastViewport.lat_max) / 2.0,
        (m_lastViewport.lon_min + m_lastViewport.lon_max) / 2.0
    );
    
    int level = RclWorld::GetLevelFromPixelScaleMercator(centerPos, metersPerPixel);
    
    if (level > DpPbg::MAX_LEVEL) level = DpPbg::MAX_LEVEL;
    if (level < DpPbg::MIN_LEVEL) level = DpPbg::MIN_LEVEL;
    
    return level;
}

void BathymetryRenderer::CalculateVisibleTiles() {
    std::vector<Rcl> oldVisibleTiles = m_currentVisibleTiles;
    m_currentVisibleTiles.clear();
    m_currentAdjacentTiles.clear();  // NOUVEAU
    
    if (!m_lastViewport.bValid || !m_dataManager) return;
    
    double minX, minY, maxX, maxY;
    PbgTileDatabase::LatLonToMercator(m_lastViewport.lat_min, m_lastViewport.lon_min, minX, minY);
    PbgTileDatabase::LatLonToMercator(m_lastViewport.lat_max, m_lastViewport.lon_max, maxX, maxY);

    double centerX, centerY;
    PbgTileDatabase::LatLonToMercator(m_lastViewport.clat, m_lastViewport.clon, centerX, centerY);
    m_viewportCenter = Position::FromMercator(centerX, centerY);
    
    Position posMin = Position::FromMercator(minX, minY);
    Position posMax = Position::FromMercator(maxX, maxY);
    
    Rcl tileMin = RclWorld::GetRcl(posMin, m_viewportLevel);
    Rcl tileMax = RclWorld::GetRcl(posMax, m_viewportLevel);
    
    int minRow = std::min(tileMin.row(), tileMax.row());
    int maxRow = std::max(tileMin.row(), tileMax.row());
    int minCol = std::min(tileMin.column(), tileMax.column());
    int maxCol = std::max(tileMin.column(), tileMax.column());
    
    double bboxMinX = std::numeric_limits<double>::max();
    double bboxMaxX = std::numeric_limits<double>::lowest();
    double bboxMinY = std::numeric_limits<double>::max();
    double bboxMaxY = std::numeric_limits<double>::lowest();
    
    std::multimap<float, Rcl> sortedTiles;

    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            Rcl tile(row, col, m_viewportLevel);
        
            float priority = CalculateTilePriority(tile);
            sortedTiles.insert({priority, tile});
        
            Position tileTL = tile.GetPositionTopLeft();
            Position tileBR = tile.GetPositionBottomRight();
        
            bboxMinX = std::min(bboxMinX, tileTL.X());
            bboxMaxX = std::max(bboxMaxX, tileBR.X());
            bboxMinY = std::min(bboxMinY, tileBR.Y());
            bboxMaxY = std::max(bboxMaxY, tileTL.Y());
        }
    }

    m_currentVisibleTiles.clear();
    m_currentVisibleTiles.reserve(sortedTiles.size());
    for (const auto& pair : sortedTiles) {
        m_currentVisibleTiles.push_back(pair.second);
    }

   

    std::unordered_set<Rcl, RclHash> adjacentSet;
    for (const auto& visibleTile : m_currentVisibleTiles) {
        std::vector<Rcl> neighbors;
        BathymetryDataManager::GetRclNeighbors(visibleTile, neighbors);
        
        for (const auto& neighbor : neighbors) {
            if (std::find(m_currentVisibleTiles.begin(), 
                        m_currentVisibleTiles.end(), 
                        neighbor) == m_currentVisibleTiles.end()) {
                adjacentSet.insert(neighbor);
            }
        }
    }

    // Prefetch neighbouring levels (n-1 and n+1) so zooming is not a cold start
    AddAdjacentTilesForPrefetch(adjacentSet);

    m_currentAdjacentTiles.assign(adjacentSet.begin(), adjacentSet.end());
    
    if (!m_currentVisibleTiles.empty()) {
       
        m_tilesBoundingBox.minX = bboxMinX;
        m_tilesBoundingBox.maxX = bboxMaxX;
        m_tilesBoundingBox.minY = bboxMinY;
        m_tilesBoundingBox.maxY = bboxMaxY;
        m_tilesBoundingBox.level = m_viewportLevel;
        m_tilesBoundingBox.isValid = true;
    } else {
        m_tilesBoundingBox.invalidate();
    }
    
    if (m_dataManager && m_dataManager->IsInitialized()) {
        auto loadRequests = PrepareLoadRequests();
        m_dataManager->LoadGridsWithPriority(loadRequests);
    }
}

void BathymetryRenderer::UpdateGlobalDepthRange() {
    if (!m_renderParams.UseGlobalDepthRange() || !m_renderCache) {
        return;
    }

    // Store previous range to detect changes
    MinMax previousRange = m_renderParams.GetGlobalDepthRange();

    MinMax frameDepthRange;
    frameDepthRange.Reset();
    bool hasValidData = false;

    for (const auto& tile : m_currentVisibleTiles) {
        MinMax tileRange = m_renderCache->GetTileDepthRange(tile);

        if (tileRange.min != DpPbg::NO_DATA_VALUE &&
            tileRange.max != DpPbg::NO_DATA_VALUE &&
            tileRange.min <= tileRange.max) {

            if (!hasValidData) {
                frameDepthRange = tileRange;
                hasValidData = true;
            } else {
                frameDepthRange.Union(tileRange);
            }
        }
    }

    if (hasValidData) {
        m_renderParams.SetTargetDepthRange(frameDepthRange);
    }
}

void BathymetryRenderer::RenderGrid(const Rcl& tile) {
    PbgRenderCacheManager::RenderParameters currentParams;
    currentParams.brushSizeFactor = m_brushSizeFactor;

    PbgTexture* tileTexture = m_renderCache->RequestGridTexture(tile, currentParams);
    if (!tileTexture) return;
    
    float transform[16];
    CalculateTileTransform(tile, transform);
    
    MinMax depthRangeToUse = m_renderParams.GetGlobalDepthRange();
    
    m_unifiedShader->Use();
    m_unifiedShader->SetShowTileFrame(m_showTileFrame);
    m_unifiedShader->SetTransform(transform);

    // Ghost border in 258x258 texture eliminates need for UV inset padding
    m_unifiedShader->SetBorderPadding(0.0f);
    
    tileTexture->BindAsTexture(0);
    m_unifiedShader->SetElevationTexture(0);
    
    m_unifiedShader->SetDepthRange(depthRangeToUse.min, depthRangeToUse.max);

    // A single palette ships, so the gradient row is fixed: the atlas is cropped
    // to one band and we sample its middle.
    {
        float textureHeight = m_unifiedShader->GetGradientTextureHeight();
        float yCoord = (textureHeight > 0.0f)
                           ? (DpPbg::GRADIENT_PALETTE_HEIGHT * 0.5f) / textureHeight
                           : 0.5f;
        m_unifiedShader->SetGradientRow(yCoord);

        glActiveTexture(GL_TEXTURE0 + DpPbg::DEFAULT_GRADIENT_TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D, m_unifiedShader->GetGradientTexture());
        m_unifiedShader->SetGradientTexture(DpPbg::DEFAULT_GRADIENT_TEXTURE_UNIT);
    }
    
    ConfigureShaderTextureSize();
    
    m_unifiedShader->SetIsobathsEnabled(m_isobathsEnabled);

    // User contours - always set regardless of isobath toggle (render independently)
    m_unifiedShader->SetContourLineLuminance(m_isobathParameters.contourLineLuminance);
    m_unifiedShader->SetIsolineColor(m_isobathParameters.isolineColor.r,
                                    m_isobathParameters.isolineColor.g,
                                    m_isobathParameters.isolineColor.b);

    // Regular isobath parameters - only when enabled
    if (m_isobathsEnabled) {
        m_unifiedShader->SetIsolineStep(m_isobathParameters.isolineStep);
    }

    // Phase 1: Set new uniforms
    m_unifiedShader->SetBathymetryOpacity(m_bathymetryOpacity);

    // Contour color mode: force black (mode 1) when depth shading is enabled for contrast

    m_unifiedShader->SetContourRangeLimited(m_contourRangeLimited);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

   

}

void BathymetryRenderer::SetBrushSizeFactor(float brushSizeFactor) {
    if (m_brushSizeFactor != brushSizeFactor) {
        m_brushSizeFactor = brushSizeFactor;

        m_brushSize = DpPbg::BrushSizeConfig::CalculateBrushSize(
            m_viewportLevel,
            m_brushSizeFactor,
            DpPbg::PIXEL_TILE_SIZE,
            DpPbg::DEFAULT_IMPORT_LEVEL
        );

        // m_tilesBoundingBox.invalidate();

        if (m_renderCache) {
            PbgRenderCacheManager::RenderParameters newParams;
            newParams.brushSizeFactor = m_brushSizeFactor;

            m_renderCache->NotifyParameterChange(newParams);
            CalculateVisibleTiles();
        }

        if (m_refreshCallback) {
            m_refreshCallback();
        }
    }
}

void BathymetryRenderer::LoadCanvasState(int idx) {
    const CanvasState& cs = m_canvasState[idx];
    m_currentVisibleTiles    = cs.currentVisibleTiles;
    m_currentAdjacentTiles   = cs.currentAdjacentTiles;
    m_lastViewport           = cs.lastViewport;
    m_viewportLevel          = cs.viewportLevel;
    m_tilesBoundingBox       = cs.tilesBoundingBox;
    m_renderParams           = cs.renderParams;
    m_tilesRecalculated      = cs.tilesRecalculated;
    m_viewportCenter         = cs.viewportCenter;
}

void BathymetryRenderer::SaveCanvasState(int idx) {
    CanvasState& cs = m_canvasState[idx];
    cs.currentVisibleTiles    = m_currentVisibleTiles;
    cs.currentAdjacentTiles   = m_currentAdjacentTiles;
    cs.lastViewport           = m_lastViewport;
    cs.viewportLevel          = m_viewportLevel;
    cs.tilesBoundingBox       = m_tilesBoundingBox;
    cs.renderParams           = m_renderParams;
    cs.tilesRecalculated      = m_tilesRecalculated;
    cs.viewportCenter         = m_viewportCenter;
}

void BathymetryRenderer::RenderOverlay(int canvasIndex, wxGLContext* context, PlugIn_ViewPort* vp) {
    if (!m_initialized || !m_dataManager) return;

    // Swap in this canvas's persistent state; saved back before every return so each
    // chart canvas keeps its own viewport/tiles/auto depth-range independently.
    m_activeCanvasIndex = (canvasIndex == 1) ? 1 : 0;
    LoadCanvasState(m_activeCanvasIndex);

    if (vp) {
        UpdateViewport(vp);
    }

    // Runs ahead of the draw gate further down, so cache work still progresses
    // on frames where nothing ends up being drawn.
    int itemsProcessed = 0;

    if (m_renderCache) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_frameDefaultFbo);
        itemsProcessed = m_renderCache->ProcessFrame(m_frameDefaultFbo);

        if (itemsProcessed > 0 && m_refreshCallback) {
            m_refreshCallback();
        }

        if (itemsProcessed > 0 && !m_watchdogTimer.IsRunning()) {
            m_watchdogTimer.Start(DpPbg::WATCHDOG_RENDER_TIMER);
        }
    }

    // Skip 2D rendering if disabled or no visible tiles
    if (!m_enabled || m_currentVisibleTiles.empty()) {
        SaveCanvasState(m_activeCanvasIndex);
        return;
    }

    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;
    
    if (deltaTime > DpPbg::MAX_DELTA_TIME) deltaTime = DpPbg::MAX_DELTA_TIME;
    
    
    
    if (m_renderParams.UseGlobalDepthRange() && m_renderCache) {
        // Per-canvas auto-range: derive from THIS canvas's own visible tiles, not
        // the cache's shared visible set (which holds the union of both canvases).
        MinMax currentRange = m_renderCache->ComputeDepthRangeForTiles(m_currentVisibleTiles);
        if (currentRange.IsValid()) {
            m_renderParams.SetTargetDepthRange(currentRange);
        }

        if (m_tilesRecalculated) {
            m_tilesRecalculated = false;
        }
    }
    
    m_renderParams.UpdateTransition(deltaTime);

    if (m_renderParams.IsTransitioning() && m_refreshCallback) {
        m_refreshCallback();
    }
    
    GLint currentFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFbo);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    for (const auto& tile : m_currentVisibleTiles) {
       RenderGrid(tile);
    }

    glUseProgram(0);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);

    SaveCanvasState(m_activeCanvasIndex);
}

// Hillshading was removed for the open-source build, so all that remains here is
// the texture size the fragment shader needs for its neighbour taps.
void BathymetryRenderer::ConfigureShaderTextureSize() {
    if (!m_currentVisibleTiles.empty()) {
        // Texture is 258x258 (256 + 1px ghost border each side)
        m_unifiedShader->SetTextureSize(static_cast<float>(DpPbg::PADDED_TILE_SIZE));
    }
}

// Phase 1: New setters

void BathymetryRenderer::SetBathymetryOpacity(float opacity) {
    m_bathymetryOpacity = std::max(0.0f, std::min(opacity, 1.0f));
}

void BathymetryRenderer::SetContourRangeLimited(bool limited) {
    m_contourRangeLimited = limited;
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
void BathymetryRenderer::CalculateTileTransform(const Rcl& tile, float* mat)
{
    Position tl = tile.GetPositionTopLeft();
    Position br = tile.GetPositionBottomRight();

    double tileLeft   = tl.X();
    double tileRight  = br.X();
    double tileTop    = tl.Y();
    double tileBottom = br.Y();
    
<<<<<<< HEAD
=======
    // --- 2. Bornes viewport Mercator -----------------------------------------
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
    double viewLeft, viewBottom;
    PbgTileDatabase::LatLonToMercator(
        m_lastViewport.lat_min, m_lastViewport.lon_min, viewLeft, viewBottom);
    double viewRight, viewTop;
    PbgTileDatabase::LatLonToMercator(
        m_lastViewport.lat_max, m_lastViewport.lon_max, viewRight, viewTop);

    double cameraPovX  = 0.5 * (viewLeft + viewRight);
    double tileCenterX = 0.5 * (tileLeft + tileRight);
    double shiftX      = RclWorld::GetTranslationForAntiMeridian(cameraPovX,
                                                                 tileCenterX);
    tileLeft  += shiftX;
    tileRight += shiftX;

    const double halfWorld = Position::MercatorEquatorialSizeInMeter() * 0.5;
    if (std::abs(viewRight - viewLeft) > halfWorld) {
        if (viewLeft > viewRight) viewRight +=
            Position::MercatorEquatorialSizeInMeter();
    }

    // --- 5. Conversion Mercator → NDC ----------------------------------------
    double viewW = viewRight - viewLeft;
    double viewH = viewTop   - viewBottom;

    float ndcL =  2.0f * float((tileLeft   - viewLeft)   / viewW) - 1.0f;
    float ndcR =  2.0f * float((tileRight  - viewLeft)   / viewW) - 1.0f;
    float ndcB =  2.0f * float((tileBottom - viewBottom) / viewH) - 1.0f;
    float ndcT =  2.0f * float((tileTop    - viewBottom) / viewH) - 1.0f;

    float scaleX = 0.5f * (ndcR - ndcL);
    float scaleY = 0.5f * (ndcT - ndcB);
    float transX = 0.5f * (ndcR + ndcL);
    float transY = 0.5f * (ndcT + ndcB);

    std::memset(mat, 0, 16 * sizeof(float));
    mat[0]  = scaleX;
    mat[5]  = scaleY;
    mat[10] = 1.0f;
    mat[12] = transX;
    mat[13] = transY;
    mat[15] = 1.0f;
}

void BathymetryRenderer::SetIsobathParameters(const IsobathParameters& params) {
    m_isobathParameters = params;
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

IsobathParameters BathymetryRenderer::GetIsobathParameters() const {
    return m_isobathParameters;
}

void BathymetryRenderer::SetIsobathEnabled(bool enabled) {
    m_isobathsEnabled = enabled;
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

float BathymetryRenderer::CalculateAutoIsolineStep(int level, int density) const {
    // Clamp level to valid range
    int clampedLevel = std::max(0, std::min(level, 22));
    // Clamp density to valid range (1-3)
    int clampedDensity = std::max(1, std::min(density, 3));
    return kAutoIntervalTable[clampedLevel][clampedDensity - 1];
}

void BathymetryRenderer::UpdateAutoIsolineStep() {
    // Only update if in Auto mode
    if (m_isobathParameters.mode != IsobathParameters::Mode::Auto) {
        return;
    }

    float newStep = CalculateAutoIsolineStep(m_viewportLevel, m_isobathParameters.autoDensity);

    // Only update if step changed
    if (std::abs(newStep - m_isobathParameters.isolineStep) > 0.001f) {
        m_isobathParameters.isolineStep = newStep;

        if (m_refreshCallback) {
            m_refreshCallback();
        }
    }
}

void BathymetryRenderer::RenderPointsToTextureWithBorder(const std::vector<Position3D>& points, 
                                                         const Rcl& tile,
                                                         int borderWidth, GLint defaultFbo) {
    
    m_globalWorkElevation->Clear();
    m_globalWorkWeight->Clear();
    
    m_pointRenderer->SetRcl(tile);
    
   
    
 
    float matrix[16];

    Position tl = tile.GetPositionTopLeft();
    Position br = tile.GetPositionBottomRight();
    
    double tileWidth = br.X() - tl.X();
    double pixelSize = tileWidth / DpPbg::PIXEL_TILE_SIZE;
    
    double borderSizeMercator =  DpPbg::FIXED_BORDER_WIDTH * pixelSize;
    double extendedLeft = tl.X() - borderSizeMercator;
    double extendedRight = br.X() + borderSizeMercator;
    double extendedTop = tl.Y() + borderSizeMercator;
    double extendedBottom = br.Y() - borderSizeMercator;
    
    double scaleX = 2.0 / (extendedRight - extendedLeft);
    double scaleY = 2.0 / (extendedTop - extendedBottom);
    double transX = -1.0 - extendedLeft * scaleX;
    double transY = -1.0 - extendedBottom * scaleY;
    
    std::memset(matrix, 0, 16 * sizeof(float));
    matrix[0]  = static_cast<float>(scaleX);
    matrix[5]  = static_cast<float>(scaleY);
    matrix[10] = 1.0f;
    matrix[12] = static_cast<float>(transX);
    matrix[13] = static_cast<float>(transY);
    matrix[15] = 1.0f;

    m_pointRenderer->RenderPointsWithMatrix(points, 
                                          m_globalWorkElevation.get(),  // Tampon global
                                          m_globalWorkWeight.get(),     // Tampon global
                                          matrix);
    
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
}

void BathymetryRenderer::ExtractCenterRegion(PbgTexture* source, PbgTexture* destination, GLint defaultFbo) {
 

    int sourceSize = source->GetWidth();  // 512
    int destSize = destination->GetWidth();  // 256
    
   
        int offset = (sourceSize - destSize) / 2;
        
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source->GetFramebuffer());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination->GetFramebuffer());
        
        glBlitFramebuffer(
            offset, offset,
            offset + destSize, offset + destSize,
            0, 0,
            destSize, destSize,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
 
    
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
}

bool BathymetryRenderer::RenderGridToTextureWithExtendedGrid(
    const Rcl& tile, 
    PbgTexture* texture,
    const CachedExtendedGrid& seeds,
    GLint defaultFbo) {
    
    RenderPointsToTextureWithBorder(seeds.points, tile, DpPbg::FIXED_BORDER_WIDTH, defaultFbo);
    
<<<<<<< HEAD
=======
    // Dilater
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
    m_dilationRenderer->RenderPbgData(
        m_globalWorkElevation.get(),
        m_globalWorkWeight.get(),
        m_brushSize);
    
    ExtractCenterRegion(m_globalWorkElevation.get(), texture, defaultFbo);
    
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    
    return true;
}

GLuint BathymetryRenderer::GetGradientTexture() const {
    return m_unifiedShader ? m_unifiedShader->GetGradientTexture() : 0;
}

int BathymetryRenderer::GetGradientTextureHeight() const {
    return m_unifiedShader ? m_unifiedShader->GetGradientTextureHeight() : 0;
}

void BathymetryRenderer::AddAdjacentTilesForPrefetch(std::unordered_set<Rcl, RclHash>& adjacentSet) {
    

    if (!DpPbg::ENABLE_ADJACENT_TILE_PREFETCH) {
        return;
    }

    if (m_viewportLevel > DpPbg::MIN_LEVEL) {
        std::unordered_set<Rcl, RclHash> parentSet;
        
        for (const auto& visibleTile : m_currentVisibleTiles) {
            int parentRow = visibleTile.row() >> 1;
            int parentCol = visibleTile.column() >> 1;
            Rcl parentTile(parentRow, parentCol, m_viewportLevel - 1);
            
            parentSet.insert(parentTile);
        }
        
        for (const auto& parent : parentSet) {
            adjacentSet.insert(parent);
        }
        
    }
    
    if (m_viewportLevel < DpPbg::MAX_LEVEL) {
        std::vector<Rcl> centralTiles = GetCentralVisibleTiles(4);
        
        for (const auto& centralTile : centralTiles) {
            int baseRow = centralTile.row() << 1;
            int baseCol = centralTile.column() << 1;
            
            for (int dr = 0; dr < 2; dr++) {
                for (int dc = 0; dc < 2; dc++) {
                    Rcl childTile(baseRow + dr, baseCol + dc, m_viewportLevel + 1);
                    adjacentSet.insert(childTile);
                }
            }
        }
              
    }
}

std::vector<Rcl> BathymetryRenderer::GetCentralVisibleTiles(int maxCount) const {
    std::vector<Rcl> result;
    
    if (m_currentVisibleTiles.empty()) {
        return result;
    }
    
    if (m_currentVisibleTiles.size() <= static_cast<size_t>(maxCount)) {
        return m_currentVisibleTiles;
    }
    
    std::vector<std::pair<double, size_t>> tilesWithDistance;
    tilesWithDistance.reserve(m_currentVisibleTiles.size());
    
    double worldSize = Position::MercatorEquatorialSizeInMeter();
    int numTiles = Rcl::GetNumberColumnsAtLevel(m_viewportLevel);
    double tileSize = worldSize / numTiles;
    double halfWorld = worldSize * 0.5;
    
    for (size_t i = 0; i < m_currentVisibleTiles.size(); i++) {
        const auto& tile = m_currentVisibleTiles[i];
        
        double tileCenterX = (tile.column() + 0.5) * tileSize - halfWorld;
        double tileCenterY = (tile.row() + 0.5) * tileSize - halfWorld;
        
        double dx = tileCenterX - m_viewportCenter.X();
        double dy = tileCenterY - m_viewportCenter.Y();
        double distanceSquared = dx * dx + dy * dy;
        
        tilesWithDistance.push_back({distanceSquared, i});
    }
    
    int count = std::min(maxCount, static_cast<int>(tilesWithDistance.size()));
    std::partial_sort(tilesWithDistance.begin(), 
                     tilesWithDistance.begin() + count,
                     tilesWithDistance.end(),
                     [](const auto& a, const auto& b) {
                         return a.first < b.first;
                     });
    
    result.reserve(count);
    for (int i = 0; i < count; i++) {
        result.push_back(m_currentVisibleTiles[tilesWithDistance[i].second]);
    }
    
    return result;
}

float BathymetryRenderer::CalculateTilePriority(const Rcl& tile) const {
    Position tileCenter = tile.GetPositionCenter();
    
    double dx = tileCenter.X() - m_viewportCenter.X();
    double dy = tileCenter.Y() - m_viewportCenter.Y();
    double distance = std::sqrt(dx * dx + dy * dy);
    
    Position tl = tile.GetPositionTopLeft();
    Position br = tile.GetPositionBottomRight();
    double tileSize = br.X() - tl.X();
    
    return static_cast<float>(distance / tileSize);
}

std::vector<GridLoadRequest> BathymetryRenderer::PrepareLoadRequests() const {
    std::vector<GridLoadRequest> requests;
    
    requests.reserve(m_currentVisibleTiles.size() + m_currentAdjacentTiles.size());
    
    for (const auto& tile : m_currentVisibleTiles) {
        float priority = CalculateTilePriority(tile);
        requests.push_back({tile, priority, GridLoadRequest::VISIBLE});
    }
    
    for (const auto& tile : m_currentAdjacentTiles) {
        float priority = CalculateTilePriority(tile) + 1000.0f;
        requests.push_back({tile, priority, GridLoadRequest::ADJACENT});
    }
    
    std::sort(requests.begin(), requests.end());
        
    return requests;
}

wxString BathymetryRenderer::GetTileInfoAt(double lat, double lon) {
    wxString info;
    info << "Position: " << wxString::Format("%.6f, %.6f\n", lat, lon);
    
    double x, y;
    PbgTileDatabase::LatLonToMercator(lat, lon, x, y);
    Position pos = Position::FromMercator(x, y);
    
    Rcl tile = RclWorld::GetRcl(pos, m_viewportLevel);
    wxString tileKey = RclExtensions::RclToKey(tile);
    
    info << "Tile: " << tileKey << " (Level " << m_viewportLevel << ")\n";
    info << "----------------------------------------\n";
    
    if (m_renderCache) {
        info << m_renderCache->GetTileDebugInfo(tile);
    }
    
    if (m_dataManager) {
        info << m_dataManager->GetExtendedGridDebugInfo(tile);
        info << m_dataManager->GetChildGridDebugInfo(tile);
    }
    
    return info;
}