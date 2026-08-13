/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once
#include <unordered_set>
#include <memory>
#include <array>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <queue>
#include <mutex>
#include <atomic>
#include <GL/glew.h>
#include <wx/glcanvas.h>
#include "PbgPointRenderer.h"
#include "PbgDilationRenderer.h"

#include "PbgTexture.h"
#include "BathymetryRenderParams.h"
#include "Rcl.h"

#include "PbgRenderCacheManager.h"
#include "PbgUnifiedShader.h"
#include "IsobathParameters.h"

#ifdef STANDALONE_BUILD
    #include "Plugin_ViewPort.h"  
#else
    #include "ocpn_plugin.h"         
#endif

struct CachedExtendedGrid;
struct GridLoadRequest;


class BathymetryRenderer {

public:
    BathymetryRenderer();
    ~BathymetryRenderer();
    
    bool Initialize();
    void Cleanup();
    
    // canvasIndex selects which chart canvas's per-canvas state to render with
    // (dual-chart combo). State is loaded at entry and saved at exit so each
    // canvas keeps its own viewport/tiles/auto depth-range independently.
    void RenderOverlay(int canvasIndex, wxGLContext* context, PlugIn_ViewPort* vp);
    void UpdateViewport(PlugIn_ViewPort* vp);
    
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    
    
    BathymetryDataManager* GetDataManager() { return m_dataManager.get(); }
    
    

    
    GLuint GetGradientTexture() const;
    int GetGradientTextureHeight() const;

    void SetBrushSizeFactor(float brushSizeFactor);
    float GetBrushSizeFactor() const { return m_brushSizeFactor; }

    





    // Phase 1: Opacity/transparency (0.0 = transparent, 1.0 = opaque)
    void SetBathymetryOpacity(float opacity);
    float GetBathymetryOpacity() const { return m_bathymetryOpacity; }

    // Phase 1: Contour range restriction (limit contours to depth range)
    void SetContourRangeLimited(bool limited);
    bool GetContourRangeLimited() const { return m_contourRangeLimited; }
   
   
    
    // These are global (apply to every canvas), so fan out to both per-canvas
    // states as well as the live scratch copy.
    void SetUseGlobalDepthRange(bool useGlobal) {
        m_renderParams.SetUseGlobalDepthRange(useGlobal);
        for (auto& cs : m_canvasState) cs.renderParams.SetUseGlobalDepthRange(useGlobal);
    }
    bool GetUseGlobalDepthRange() const { return m_renderParams.UseGlobalDepthRange(); }

    void SetTransitionDuration(float duration) {
        m_renderParams.SetTransitionDuration(duration);
        for (auto& cs : m_canvasState) cs.renderParams.SetTransitionDuration(duration);
    }
    float GetTransitionDuration() const { return m_renderParams.GetTransitionDuration(); }

    // Per-canvas depth range (each chart canvas auto-scales independently).
    MinMax GetCurrentDepthRange() const {
        return m_canvasState[m_activeCanvasIndex].renderParams.GetGlobalDepthRange();
    }
    MinMax GetCurrentDepthRange(int canvasIndex) const {
        return m_canvasState[(canvasIndex == 1) ? 1 : 0].renderParams.GetGlobalDepthRange();
    }

    
    using RefreshCallback = std::function<void()>;
    void SetRefreshCallback(RefreshCallback callback) { m_refreshCallback = callback; }

    // Auto isoline step calculation
    float CalculateAutoIsolineStep(int level, int density) const;
    void UpdateAutoIsolineStep();
    

    int GetViewportLevel() const { return m_canvasState[m_activeCanvasIndex].viewportLevel; }
    const std::vector<Rcl>& GetVisibleTiles() const {
        return m_canvasState[m_activeCanvasIndex].currentVisibleTiles;
    }
    const std::vector<Rcl>& GetVisibleTiles(int canvasIndex) const {
        return m_canvasState[(canvasIndex == 1) ? 1 : 0].currentVisibleTiles;
    }

    void SetShowTileFrame(bool showTileFrame){ m_showTileFrame = showTileFrame; }
    bool GetShowTileFrame() const {return m_showTileFrame;}

    PbgRenderCacheManager* GetRenderCache() { return m_renderCache.get(); }

    // Force recalculation of visible tiles on next render (e.g. after cache invalidation)
    void ForceViewportRecalculation() { m_forceRecalculation = true; }

   
    wxString GetTileInfoAt(double lat, double lon);

private:

    struct TilesBoundingBox {
        double minX, maxX, minY, maxY;
        int level;
        bool isValid;
    
        TilesBoundingBox() : minX(0), maxX(0), minY(0), maxY(0), level(-1), isValid(false) {}
    
        void invalidate() { isValid = false; }
    
        bool contains(double x, double y, int currentLevel) const {
            return isValid &&
                   level == currentLevel &&
                   x >= minX && x <= maxX &&
                   y >= minY && y <= maxY;
        }
    };

    // Per-canvas render state (dual-chart combo). The "live" members below act as
    // scratch for the canvas currently rendering; LoadCanvasState/SaveCanvasState
    // swap them in/out of these slots at RenderOverlay entry/exit so each chart
    // canvas keeps its own viewport, visible tiles, and auto depth-range/transition.
    struct CanvasState {
        std::vector<Rcl>       currentVisibleTiles;
        std::vector<Rcl>       currentAdjacentTiles;
        PlugIn_ViewPort        lastViewport{};
        int                    viewportLevel = 0;
        TilesBoundingBox       tilesBoundingBox;
        BathymetryRenderParams renderParams;
        bool                   tilesRecalculated = false;
        Position               viewportCenter;
    };

    std::array<CanvasState, 2> m_canvasState;
    int m_activeCanvasIndex = 0;   // slot currently loaded into the live members

    void LoadCanvasState(int canvasIndex);   // slot -> live members
    void SaveCanvasState(int canvasIndex);   // live members -> slot

    TilesBoundingBox m_tilesBoundingBox;
    

    std::unique_ptr<PbgTexture> m_globalWorkElevation;
    std::unique_ptr<PbgTexture> m_globalWorkWeight;

    std::atomic<bool> m_needsRefresh{false};
    bool m_forceRecalculation{false};
    
    void CreateQuadVAO();
    void RenderGrid(const Rcl& tile);
   
    void CalculateTileTransform(const Rcl& tile, float* transform);
    void ConfigureShaderTextureSize();
    void CalculateVisibleTiles();
    int CalculateAppropriateLevel(double viewScale) const;
    bool IsViewportDifferent(const PlugIn_ViewPort* vp) const;
    void UpdateGlobalDepthRange();

    void RenderPointsToTextureWithBorder(const std::vector<Position3D>& points, const Rcl& tile, int borderWidth, GLint defaultFbo);

    bool GetExtendedSeedsForTileWithCount(const Rcl& tile, std::vector<Position3D>& allSeeds,
                                                          MinMax& depthRange,
                                                          int borderWidth,
                                                          int& usedNeighborCount) ;
    


    void ExtractCenterRegion(PbgTexture* source, PbgTexture* destination, GLint defaultFbo);
    
    bool m_initialized;
    bool m_enabled;
    
    
    GLuint m_quadVAO;
    GLuint m_quadVBO;
    
    std::unique_ptr<BathymetryDataManager> m_dataManager;
    std::unique_ptr<PbgPointRenderer> m_pointRenderer;
    std::unique_ptr<PbgDilationRenderer> m_dilationRenderer;


    std::vector<Rcl> m_currentAdjacentTiles;

    
    std::vector<Rcl> m_currentVisibleTiles;
    PlugIn_ViewPort m_lastViewport;
    
    
    

    // Phase 1: New settings
    float m_bathymetryOpacity{1.0f};     // 0.0-1.0 (1.0 = fully opaque)
    bool m_contourRangeLimited{false};   // Limit contours to depth range
    
    BathymetryRenderParams m_renderParams;
    
    bool m_tilesRecalculated;
    int m_viewportLevel;

    
    RefreshCallback m_refreshCallback;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    




    wxTimer m_watchdogTimer;



    std::unique_ptr<PbgUnifiedShader> m_unifiedShader;
    bool m_isobathsEnabled;
    IsobathParameters m_isobathParameters;



    void AddAdjacentTilesForPrefetch(std::unordered_set<Rcl, RclHash>& adjacentSet);

    Position m_viewportCenter;

public:
    // The tiles nearest the viewport centre, used to pick prefetch candidates.
    std::vector<Rcl> GetCentralVisibleTiles(int maxCount = 4) const;

private: 
 
    /******************* isobath ******************/

    public:
    void SetIsobathParameters(const IsobathParameters& params);
    IsobathParameters GetIsobathParameters() const;
    void SetIsobathEnabled(bool enabled);
    bool GetIsobathEnabled() const { return m_isobathsEnabled; }



    int m_brushSize;
    float m_brushSizeFactor;

   bool m_showTileFrame;


   GLint m_frameDefaultFbo = 0;

   std::unique_ptr<PbgRenderCacheManager> m_renderCache;


   std::vector<GridLoadRequest> PrepareLoadRequests() const;
   float CalculateTilePriority(const Rcl& tile) const;



   void OnSeedsReady(const Rcl& tile);
    bool RenderGridToTextureWithExtendedGrid(const Rcl& tile, PbgTexture* texture,
                                     const CachedExtendedGrid& seeds,
                                     GLint defaultFbo);


};