/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Record depth soundings from NMEA and render a bathymetric chart
 Author:  Deeprey Research Ltd

 Copyright (C) 2024-2026 Deeprey Research Ltd

 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef _DEEPREYBATHY_PI_H_
#define _DEEPREYBATHY_PI_H_

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include <wx/fileconf.h>
#include <wx/file.h>

#include <memory>
#include "BathymetryRenderer.h"
#include "DirtyTileTracker.h"
#include "AsyncPersistenceManager.h"
#include "RealtimePointInjector.h"

#include "ocpn_plugin.h"
#include "config.h"
#include "pidc.h"

#include "nmea0183.h"

#define DLOG(message, ...) \
  DpLog(__FILE__, __LINE__, *wxBLACK, wxString::Format(message, ##__VA_ARGS__))

class DeepreyBathyPrefsDialog;

//----------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------
//
// API 1.18 is targeted deliberately: it covers OpenCPN 5.8.0 through 5.14.0,
// which is the overwhelming majority of the install base and what the plugin
// catalog targets. Everything used here exists at 1.18.
//
// wxEvtHandler is inherited so that GetListener() has an event sink to deliver
// NMEA 0183 messages to. (The upstream plugin got this for free from wxTimer.)

class DeepreyBathy_pi : public opencpn_plugin_118, public wxEvtHandler {
public:
  DeepreyBathy_pi(void *ppimgr);

  //    The required PlugIn Methods
  int Init(void);
  bool DeInit(void);
  void LateInit(void);

  int GetAPIVersionMajor();
  int GetAPIVersionMinor();
  int GetPlugInVersionMajor();
  int GetPlugInVersionMinor();
  wxBitmap *GetPlugInBitmap();
  wxString GetCommonName();
  wxString GetShortDescription();
  wxString GetLongDescription();

  bool RenderGLOverlayMultiCanvas(wxGLContext *pcontext, PlugIn_ViewPort *vp,
                                  int canvasIndex, int priority);

  //    Optional overrides
  void ShowPreferencesDialog(wxWindow *parent);
  void OnToolbarToolCallback(int id);

  // The toolbar tool shows and hides the control panel, GRIB-style, and its
  // checked state tracks the panel's visibility.
  void ToggleControlPanel();
  void OnControlPanelClosed();  // called by the panel when it hides itself

  // Ownship position/heading from OpenCPN. This is the position that every
  // recorded sounding is tagged with, and the source of SOG for the speed
  // filter.
  void SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix);

  // --- Settings surfaced in the preferences dialog -------------------------
  bool IsBathymetryEnabled(int canvasIndex) const;
  void SetBathymetryEnabled(int canvasIndex, bool enabled);
  bool IsContoursEnabled() const;
  void SetContoursEnabled(bool enabled);
  float GetOpacity() const;
  void SetOpacity(float opacity);
  bool IsRecordingEnabled() const;
  void SetRecordingEnabled(bool enabled);
  bool AreFiltersEnabled() const;
  void SetFiltersEnabled(bool enabled);

  BathymetryRenderer *GetRenderer() { return m_bathymetryRenderer.get(); }
  RealtimePointInjector *GetInjector() { return m_pointInjector.get(); }
  AsyncPersistenceManager *GetPersistenceManager() {
    return m_persistenceManager.get();
  }
  // Named DpRequestRefresh, not RequestRefresh: the plugin API exports a global
  // RequestRefresh(wxWindow*) and an identically-named member would shadow it.
  void DpRequestRefresh();
  bool SaveConfig(void);

private:
  // Absolute path to a file in the installed data/ directory.
  static wxString DataFile(const wxString &filename);
  wxImage LoadIMG(const wxString &filename, int width, int height);
  // Drawn in code so the toolbar tool is never invisible: InsertPlugInTool with
  // an invalid wxBitmap yields a blank, unclickable-looking button, which is
  // exactly what a partial install (.so copied without the data/ directory)
  // produces.
  static wxBitmap MakeFallbackIcon(int width, int height);
  bool InitLogger();
  void DeInitLogger();
  void DpLog(const wxString &file, int line, const wxColour &color,
             const wxString &message);
  bool LoadConfig(void);
  void ApplyStoredSettings();
  void ApplyFixedRenderSettings();

  // --- NMEA depth path -----------------------------------------------------
  // Depth arrives via GetListener() rather than SetNMEASentence() because the
  // latter is filtered by the user's per-connection sentence whitelist and can
  // be silently starved. The listeners MUST be members; a local never fires.
  void SetupNmeaListeners();
  void OnDepthSentence(const std::string &payload, bool isDpt);

  std::shared_ptr<ObservableListener> m_listener_dpt;
  std::shared_ptr<ObservableListener> m_listener_dbt;
  NMEA0183 m_nmea;
  long long m_lastDptMs = 0;  // DPT wins over DBT while it is flowing

  bool m_initializedLog = false;
  wxFile m_DpLogFile;

  // Cached ownship fix from SetPositionFixEx.
  double m_fixLat = 0.0, m_fixLon = 0.0;
  bool m_fixValid = false;
  long long m_fixTimeMs = 0;  // wxGetUTCTimeMillis when last fix arrived

  wxFileConfig *m_pconfig;
  wxBitmap m_panelBitmap;
  piDC *m_pidc;
  wxFont *m_Font_DC;
  int m_toolbar_item_id = -1;

  // Modeless control panel, created on first toolbar click and hidden — never
  // destroyed — thereafter. Owned here and torn down in DeInit.
  DeepreyBathyPrefsDialog *m_prefsDialog = nullptr;

  // GL initialization is deferred to the first render callback.
  bool m_glInitialized = false;
  bool InitializeGL();

  std::unique_ptr<BathymetryRenderer> m_bathymetryRenderer;
  bool m_bathymetryInitialized;

  // Real-time persistence
  std::unique_ptr<DirtyTileTracker> m_dirtyTracker;
  std::unique_ptr<AsyncPersistenceManager> m_persistenceManager;

  // Live sounding ingestion
  std::unique_ptr<RealtimePointInjector> m_pointInjector;

  bool InitializeRealtimeInjection();
  void CleanupRealtimeInjection();
  bool InitializeBathymetry();
  void CleanupBathymetry();

  wxTimer *m_refreshTimer;
  bool m_needsRefresh;
  void OnRefreshTimer(wxTimerEvent &event);

  // Per-canvas visibility. The renderer has a single global enable flag, so the
  // per-canvas state is owned here.
  bool m_enabledByCanvas[2] = {false, false};
};

#endif  //  _DEEPREYBATHY_PI_H_
