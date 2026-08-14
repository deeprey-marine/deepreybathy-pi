/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Record depth soundings from NMEA and render a bathymetric chart
 Author:  Deeprey Research Ltd

 Copyright (C) 2024-2026 Deeprey Research Ltd

 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include <GL/glew.h>
#include <GL/gl.h>

#include <cmath>

#include "DeepreyBathy_pi.h"
#include "ImportExportManager.h"
#include "BathymetryDataManager.h"
#include "DeepreyBathyPrefsDialog.h"

// the class factories, used to create and destroy instances of the PlugIn
extern "C" DECL_EXP opencpn_plugin *create_pi(void *ppimgr) {
  return new DeepreyBathy_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin *p) { delete p; }

namespace RefreshConfig {
constexpr int REFRESH_INTERVAL_MS = 16;  // 16ms = ~60 FPS
}

// Fixed render settings (see docs/OPENSOURCE_PLUGIN_SPEC.md §1.4b). These are
// applied once at startup and are deliberately not user-configurable: they are
// the render pipeline, not features.
namespace FixedSettings {
constexpr float kBrushSizeFactor  = 0.5f;   // Smoothing
constexpr int   kAutoDensity      = 2;      // Medium

constexpr double kFilterMinDepth      = 0.3;    // metres
constexpr double kFilterMaxDepth      = 200.0;  // metres
constexpr double kFilterMaxSpeedKnots = 15.0;
constexpr double kFilterMaxVariation  = 50.0;   // percent
constexpr int    kFilterMaxRepeats    = 5;

// A sounding is only recorded if the GPS fix backing it is fresher than this.
constexpr long long kFixMaxAgeMs = 5000;
// While DPT is arriving, DBT is ignored — DPT carries the transducer offset and
// recording both would log the same ping twice at two different depths.
constexpr long long kDptPreferenceMs = 5000;
}  // namespace FixedSettings

// NMEA 0183 message events. Defined at file scope so the event type outlives the
// Bind() call that references it.
wxDEFINE_EVENT(EVT_N0183_DPT, ObservedEvt);
wxDEFINE_EVENT(EVT_N0183_DBT, ObservedEvt);

//----------------------------------------------------------------------------
//    Deeprey Bathymetry PlugIn Implementation
//----------------------------------------------------------------------------

DeepreyBathy_pi::DeepreyBathy_pi(void *ppimgr)
    : opencpn_plugin_121(ppimgr),
      m_pconfig(nullptr),
      m_pidc(nullptr),
      m_Font_DC(nullptr),
      m_bathymetryInitialized(false),
      m_refreshTimer(nullptr),
      m_needsRefresh(false) {
  wxInitAllImageHandlers();
  wxImage panelIcon = LoadIMG(("deepreybathy_panel.png"), 32, 32);
  m_panelBitmap =
      panelIcon.IsOk() ? wxBitmap(panelIcon) : MakeFallbackIcon(32, 32);
  m_refreshTimer = new wxTimer(this, wxID_ANY);
  Bind(wxEVT_TIMER, &DeepreyBathy_pi::OnRefreshTimer, this,
       m_refreshTimer->GetId());
}

/** ------------------------------------------------------------------
 *  PLUGIN LIFECYCLE
 *  ------------------------------------------------------------------ */

int DeepreyBathy_pi::Init(void) {
  AddLocaleCatalog(_T("opencpn-deepreybathy_pi"));

  if (!m_pidc) {
    m_pidc = new piDC();
    wxFont fo = *OCPNGetFont(_("Dialog"), 10);
    m_Font_DC = new wxFont(fo);
    m_pidc->SetFont(*m_Font_DC);
  }

  m_pconfig = GetOCPNConfigObject();
  LoadConfig();

  // SVG when the artwork is actually on disk — OpenCPN rasterises it per toolbar
  // scale, so it stays sharp on hi-dpi. But an SVG path that does not resolve
  // buys a toolbar button with nothing drawn on it, which is indistinguishable
  // from a broken plugin, so fall back to the bitmap rather than risk that.
  const wxString svgNormal = DataFile(_T("deepreybathy.svg"));
  if (wxFileExists(svgNormal)) {
    m_toolbar_item_id = InsertPlugInToolSVG(
        wxEmptyString, svgNormal, DataFile(_T("deepreybathy_rollover.svg")),
        DataFile(_T("deepreybathy_toggled.svg")), wxITEM_CHECK,
        _("Deeprey Bathymetry"), wxEmptyString, NULL, -1, 0, this);
  } else {
    m_toolbar_item_id =
        InsertPlugInTool(wxEmptyString, &m_panelBitmap, &m_panelBitmap,
                         wxITEM_CHECK, _("Deeprey Bathymetry"), wxEmptyString,
                         NULL, -1, 0, this);
  }
  // Tracks panel visibility, not overlay state, and the panel starts closed.
  SetToolbarItemState(m_toolbar_item_id, false);

  SetupNmeaListeners();

  // WANTS_NMEA_SENTENCES is deliberately absent: depth arrives through
  // GetListener(), which is not subject to the user's per-connection sentence
  // filter. WANTS_NMEA_EVENTS is still needed for SetPositionFixEx.
  return (WANTS_CONFIG | WANTS_LATE_INIT | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_NMEA_EVENTS | INSTALLS_TOOLBAR_TOOL | WANTS_TOOLBAR_CALLBACK |
          WANTS_PREFERENCES);
}

void DeepreyBathy_pi::LateInit(void) {
  InitLogger();

  // GetHostApi() is not available during Init(). The cast fails only if the
  // host predates 1.21, which it cannot here — OpenCPN refuses to load a plugin
  // declaring a newer API than its own — but DpTideManager tolerates a null
  // handle rather than assuming.
  m_host_api = std::move(GetHostApi());
  m_api_121 = std::dynamic_pointer_cast<HostApi121>(m_host_api);
  m_tideManager = std::make_unique<DpTideManager>(m_api_121);

  // Deliberately not in ApplyStoredSettings(): that runs from the first GL
  // render, and a sounding arriving before the first frame would be recorded
  // with tide correction still at its default.
  ApplyStoredTideSettings();
}

bool DeepreyBathy_pi::InitializeGL() {
  if (m_glInitialized) return true;

  DLOG("Initializing GL on first render...");

  GLenum err = glewInit();
  if (err != GLEW_OK) {
    DLOG("GLEW initialization failed: %s", glewGetErrorString(err));
    return false;
  }

  if (!InitializeBathymetry()) {
    DLOG("Failed to initialize bathymetry renderer");
    return false;
  }

  if (!InitializeRealtimeInjection()) {
    DLOG("Failed to initialize real-time injection");
  }

  ApplyFixedRenderSettings();
  // The remaining config keys need the renderer and injector to exist, so they
  // are applied here rather than in LoadConfig().
  ApplyStoredSettings();

  m_glInitialized = true;
  return true;
}

bool DeepreyBathy_pi::DeInit(void) {
  // Before the subsystems it borrows: the panel holds this plugin plus its own
  // ImportExportManager with an open SQLite handle.
  if (m_prefsDialog) {
    m_prefsDialog->Destroy();
    m_prefsDialog = nullptr;
  }

  if (m_refreshTimer) {
    m_refreshTimer->Stop();
    delete m_refreshTimer;
    m_refreshTimer = nullptr;
  }

  SaveConfig();

  m_listener_dpt.reset();
  m_listener_dbt.reset();

  // After SaveConfig, which reads the tide settings back out of the manager.
  // Released here rather than left to the destructor so the host API handle
  // does not outlive DeInit.
  m_tideManager.reset();
  m_api_121.reset();
  m_host_api.reset();

  CleanupRealtimeInjection();
  CleanupBathymetry();

  if (m_pidc) {
    delete m_pidc;
    m_pidc = nullptr;
  }
  if (m_Font_DC) {
    delete m_Font_DC;
    m_Font_DC = nullptr;
  }

  DeInitLogger();
  return true;
}

/** ------------------------------------------------------------------
 *  BATHYMETRY SETUP
 *  ------------------------------------------------------------------ */

bool DeepreyBathy_pi::InitializeBathymetry() {
  try {
    m_bathymetryRenderer = std::make_unique<BathymetryRenderer>();

    if (!m_bathymetryRenderer->Initialize()) {
      DLOG("BathymetryRenderer::Initialize() failed");
      m_bathymetryRenderer.reset();
      return false;
    }

    m_dirtyTracker = std::make_unique<DirtyTileTracker>();

    BathymetryDataManager *dataManager = m_bathymetryRenderer->GetDataManager();
    if (dataManager) {
      m_persistenceManager = std::make_unique<AsyncPersistenceManager>(
          *m_dirtyTracker, *dataManager, dataManager->GetDatabase());

      dataManager->SetDirtyTileTracker(m_dirtyTracker.get());
      dataManager->SetAsyncPersistenceManager(m_persistenceManager.get());

      m_persistenceManager->Start();
      DLOG("Async persistence manager started");
    }

    m_bathymetryRenderer->SetRefreshCallback(
        [this]() { this->CallAfter([this] { this->DpRequestRefresh(); }); });

    m_bathymetryInitialized = true;
    return true;

  } catch (const std::exception &e) {
    DLOG("Exception initializing bathymetry: %s", e.what());
    m_bathymetryRenderer.reset();
    m_persistenceManager.reset();
    m_dirtyTracker.reset();
    m_bathymetryInitialized = false;
    return false;
  }
}

void DeepreyBathy_pi::CleanupBathymetry() {
  if (m_persistenceManager) {
    m_persistenceManager->Stop();
    m_persistenceManager.reset();
  }
  m_dirtyTracker.reset();

  if (m_bathymetryRenderer) {
    m_bathymetryRenderer.reset();
    m_bathymetryInitialized = false;
  }
}

bool DeepreyBathy_pi::InitializeRealtimeInjection() {
  try {
    if (!m_bathymetryRenderer || !m_bathymetryRenderer->GetDataManager()) {
      DLOG("Cannot initialize real-time injection: renderer not ready");
      return false;
    }

    m_pointInjector = std::make_unique<RealtimePointInjector>();

    if (!m_pointInjector->Initialize(m_bathymetryRenderer->GetDataManager(),
                                     m_bathymetryRenderer->GetRenderCache())) {
      DLOG("Failed to initialize RealtimePointInjector");
      m_pointInjector.reset();
      return false;
    }

    if (m_dirtyTracker) {
      m_pointInjector->SetDirtyTileTracker(m_dirtyTracker.get());
    }

    m_pointInjector->SetRefreshCallback(
        [this]() { this->CallAfter([this]() { this->DpRequestRefresh(); }); });

    DLOG("Real-time injection initialized");
    return true;

  } catch (const std::exception &e) {
    DLOG("Exception initializing real-time injection: %s", e.what());
    m_pointInjector.reset();
    return false;
  }
}

void DeepreyBathy_pi::CleanupRealtimeInjection() {
  if (m_pointInjector) {
    m_pointInjector->Shutdown();
    m_pointInjector.reset();
  }
}

// Everything the user cannot change. Applied once, after the renderer and the
// injector exist.
void DeepreyBathy_pi::ApplyFixedRenderSettings() {
  if (m_bathymetryRenderer) {
    m_bathymetryRenderer->SetUseGlobalDepthRange(true);  // automatic range
    m_bathymetryRenderer->SetTransitionDuration(1.0f);
    m_bathymetryRenderer->SetBrushSizeFactor(FixedSettings::kBrushSizeFactor);
    m_bathymetryRenderer->SetContourRangeLimited(false);

    IsobathParameters params = m_bathymetryRenderer->GetIsobathParameters();
    params.mode = IsobathParameters::Mode::Auto;
    params.autoDensity = FixedSettings::kAutoDensity;
    m_bathymetryRenderer->SetIsobathParameters(params);
  }

  if (m_pointInjector) {
    auto *filter = m_pointInjector->GetFilter();
    if (filter) {
      filter->SetRangeFilterEnabled(true);
      filter->SetMinDepth(FixedSettings::kFilterMinDepth);
      filter->SetMaxDepth(FixedSettings::kFilterMaxDepth);
      filter->SetSpeedFilterEnabled(true);
      filter->SetMaxSpeedKnots(FixedSettings::kFilterMaxSpeedKnots);
      filter->SetSpikeFilterEnabled(true);
      filter->SetMaxVariationPercent(FixedSettings::kFilterMaxVariation);
      filter->SetRepetitionFilterEnabled(true);
      filter->SetMaxConsecutiveRepetitions(FixedSettings::kFilterMaxRepeats);
    }
  }
}

/** ------------------------------------------------------------------
 *  NMEA DEPTH PATH
 *  ------------------------------------------------------------------ */

void DeepreyBathy_pi::SetupNmeaListeners() {
  m_listener_dpt = GetListener(NMEA0183Id("DPT"), EVT_N0183_DPT, this);
  Bind(EVT_N0183_DPT, [&](ObservedEvt ev) {
    OnDepthSentence(GetN0183Payload(NMEA0183Id("DPT"), ev), true);
  });

  m_listener_dbt = GetListener(NMEA0183Id("DBT"), EVT_N0183_DBT, this);
  Bind(EVT_N0183_DBT, [&](ObservedEvt ev) {
    OnDepthSentence(GetN0183Payload(NMEA0183Id("DBT"), ev), false);
  });
}

// Called on the main GUI thread. Must not throw — OpenCPN dispatches plugin
// callbacks inside a sigsetjmp guard, and a fault here skips every plugin
// loaded after this one.
void DeepreyBathy_pi::OnDepthSentence(const std::string &payload, bool isDpt) {
  if (!m_pointInjector || payload.empty()) return;

  // The message bus delivers filtered and bad-checksum sentences too, and the
  // payload does not carry the message state, so validate here.
  wxString sentence(payload.c_str(), wxConvUTF8);
  m_nmea << sentence;
  if (!m_nmea.PreParse()) return;
  if (!m_nmea.Parse()) return;

  const long long nowMs = wxGetUTCTimeMillis().GetValue();
  double depthMeters = 0.0;

  if (isDpt) {
    depthMeters = m_nmea.Dpt.DepthMeters;
    // Positive offset = transducer to waterline, negative = transducer to keel.
    // Either way it is additive.
    const double offset = m_nmea.Dpt.OffsetFromTransducerMeters;
    if (!std::isnan(offset)) depthMeters += offset;
    m_lastDptMs = nowMs;
  } else {
    // Prefer DPT while it is flowing; it is self-describing and DBT is not.
    if (m_lastDptMs != 0 &&
        (nowMs - m_lastDptMs) < FixedSettings::kDptPreferenceMs) {
      return;
    }
    depthMeters = m_nmea.Dbt.DepthMeters;  // feet/fathoms fields are often empty
  }

  if (!(depthMeters > 0.0)) return;  // also rejects NaN

  if (!m_fixValid) return;
  if ((nowMs - m_fixTimeMs) > FixedSettings::kFixMaxAgeMs) return;

  Position pos = Position::FromLatLon(m_fixLat, m_fixLon);

  // Reduce to chart datum. Applied after the sanity check above, which is about
  // the sentence being usable, and before injection, so what reaches the tile
  // database is comparable between passes taken at different states of tide.
  //
  // A consequence worth knowing: InjectPoint only queues, and RealTimePointFilter
  // range-gates later on the injector's worker thread — against the REDUCED
  // depth. Ground that dries below kFilterMinDepth at datum is therefore
  // dropped by the range filter. That is intended, not a bug.
  if (m_tideManager) depthMeters -= m_tideManager->GetTideCorrection();

  // z is NEGATIVE depth: the filter negates it back and range-gates on the
  // positive value, so a positive z here means every point is rejected.
  m_pointInjector->InjectPoint(Position3D(pos.X(), pos.Y(), -depthMeters));
}

void DeepreyBathy_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix) {
  m_fixLat = pfix.Lat;
  m_fixLon = pfix.Lon;
  m_fixValid = (pfix.Lat != 0.0 || pfix.Lon != 0.0);
  m_fixTimeMs = wxGetUTCTimeMillis().GetValue();

  // Sog may be NaN. Feeding -1.0 tells the filter to derive speed from
  // successive positions instead.
  if (m_pointInjector) {
    m_pointInjector->SetCurrentSpeedKnots(std::isnan(pfix.Sog) ? -1.0
                                                               : pfix.Sog);
  }

  // Drives station selection and the tide sample cache. No timer is needed:
  // fixes arrive around 1 Hz, and both jobs inside Update() are rate-limited.
  if (m_tideManager && m_fixValid) {
    m_tideManager->UpdatePosition(m_fixLat, m_fixLon);
    m_tideManager->Update();
  }
}

/** ------------------------------------------------------------------
 *  RENDERING
 *  ------------------------------------------------------------------ */

bool DeepreyBathy_pi::RenderGLOverlayMultiCanvas(wxGLContext *pcontext,
                                                PlugIn_ViewPort *vp,
                                                int canvasIndex, int priority) {
  // Lazy GL initialization — the context is not guaranteed valid in LateInit().
  if (!m_glInitialized) {
    if (!InitializeGL()) return false;
  }

  // OVERLAY_LEGACY is the documented level for depth contours and weather-style
  // overlays, so routes, waypoints and AIS targets draw on top.
  if (priority != OVERLAY_LEGACY) return false;

  if (!m_bathymetryInitialized || !m_bathymetryRenderer) return false;
  if (canvasIndex < 0 || canvasIndex > 1) return false;
  if (!m_enabledByCanvas[canvasIndex]) return false;

  m_bathymetryRenderer->RenderOverlay(canvasIndex, pcontext, vp);
  return true;
}

/** ------------------------------------------------------------------
 *  UI ENTRY POINTS
 *  ------------------------------------------------------------------ */

void DeepreyBathy_pi::OnToolbarToolCallback(int id) {
  if (id != m_toolbar_item_id) return;
  ToggleControlPanel();
}

void DeepreyBathy_pi::ToggleControlPanel() {
  // Created on first click rather than in Init(): the renderer and the injector
  // only exist after InitializeGL(), which runs on the first render callback. A
  // panel built any earlier would read null from every getter and silently drop
  // whatever the user set.
  if (!m_prefsDialog) {
    m_prefsDialog =
        new DeepreyBathyPrefsDialog(GetOCPNCanvasWindow(), this, /*modeless=*/true);
  }

  const bool show = !m_prefsDialog->IsShown();
  m_prefsDialog->Show(show);
  if (show) m_prefsDialog->Raise();
  SetToolbarItemState(m_toolbar_item_id, show);
}

void DeepreyBathy_pi::OnControlPanelClosed() {
  SetToolbarItemState(m_toolbar_item_id, false);
}

void DeepreyBathy_pi::ShowPreferencesDialog(wxWindow *parent) {
  DeepreyBathyPrefsDialog dlg(parent, this);
  dlg.ShowModal();
}

/** ------------------------------------------------------------------
 *  SETTINGS ACCESSORS (used by the preferences dialog)
 *  ------------------------------------------------------------------ */

bool DeepreyBathy_pi::IsBathymetryEnabled(int canvasIndex) const {
  if (canvasIndex < 0 || canvasIndex > 1) return false;
  return m_enabledByCanvas[canvasIndex];
}

void DeepreyBathy_pi::SetBathymetryEnabled(int canvasIndex, bool enabled) {
  if (canvasIndex < 0 || canvasIndex > 1) return;
  m_enabledByCanvas[canvasIndex] = enabled;
  if (m_bathymetryRenderer) {
    m_bathymetryRenderer->SetEnabled(m_enabledByCanvas[0] ||
                                     m_enabledByCanvas[1]);
  }
}

bool DeepreyBathy_pi::IsContoursEnabled() const {
  return m_bathymetryRenderer ? m_bathymetryRenderer->GetIsobathEnabled()
                              : false;
}

void DeepreyBathy_pi::SetContoursEnabled(bool enabled) {
  if (m_bathymetryRenderer) m_bathymetryRenderer->SetIsobathEnabled(enabled);
}

float DeepreyBathy_pi::GetOpacity() const {
  return m_bathymetryRenderer ? m_bathymetryRenderer->GetBathymetryOpacity()
                              : 1.0f;
}

void DeepreyBathy_pi::SetOpacity(float opacity) {
  if (!m_bathymetryRenderer) return;
  m_bathymetryRenderer->SetBathymetryOpacity(opacity);
  // SetBathymetryOpacity is a plain field write and does not fire the renderer's
  // refresh callback, so the chart would not update until an unrelated redraw.
  DpRequestRefresh();
}

bool DeepreyBathy_pi::IsRecordingEnabled() const {
  return m_pointInjector ? m_pointInjector->IsRecordingEnabled() : false;
}

void DeepreyBathy_pi::SetRecordingEnabled(bool enabled) {
  if (m_pointInjector) m_pointInjector->SetRecordingEnabled(enabled);
}

bool DeepreyBathy_pi::AreFiltersEnabled() const {
  if (!m_pointInjector) return false;
  const auto *filter = m_pointInjector->GetFilter();
  return filter ? filter->IsMasterEnabled() : false;
}

void DeepreyBathy_pi::SetFiltersEnabled(bool enabled) {
  if (!m_pointInjector) return;
  auto *filter = m_pointInjector->GetFilter();
  if (filter) filter->SetMasterEnabled(enabled);
}

bool DeepreyBathy_pi::IsTideCorrectionEnabled() const {
  return m_tideManager ? m_tideManager->IsEnabled() : false;
}

void DeepreyBathy_pi::SetTideCorrectionEnabled(bool enabled) {
  if (m_tideManager) m_tideManager->SetEnabled(enabled);
}

double DeepreyBathy_pi::GetTideDatumOffset() const {
  return m_tideManager ? m_tideManager->GetDatumOffset() : 0.0;
}

void DeepreyBathy_pi::SetTideDatumOffset(double meters) {
  // The manager clamps to +/-MAX_DATUM_OFFSET itself.
  if (m_tideManager) m_tideManager->SetDatumOffset(meters);
}

/** ------------------------------------------------------------------
 *  CONFIG
 *  ------------------------------------------------------------------ */

bool DeepreyBathy_pi::LoadConfig(void) {
  wxFileConfig *pConf = m_pconfig;
  if (!pConf) return false;

  pConf->SetPath(_T("/Settings/DeepreyBathymetry"));
  pConf->Read(_T("BathymetryEnabledCanvas0"), &m_enabledByCanvas[0], false);
  pConf->Read(_T("BathymetryEnabledCanvas1"), &m_enabledByCanvas[1], false);
  return true;
}

bool DeepreyBathy_pi::SaveConfig(void) {
  wxFileConfig *pConf = m_pconfig;
  if (!pConf) return false;

  pConf->SetPath(_T("/Settings/DeepreyBathymetry"));
  pConf->Write(_T("BathymetryEnabledCanvas0"), m_enabledByCanvas[0]);
  pConf->Write(_T("BathymetryEnabledCanvas1"), m_enabledByCanvas[1]);
  pConf->Write(_T("IsobathEnabled"), IsContoursEnabled());
  pConf->Write(_T("BathymetryOpacity"), (double)GetOpacity());
  pConf->Write(_T("LiveRecordingEnabled"), IsRecordingEnabled());
  pConf->Write(_T("FiltersEnabled"), AreFiltersEnabled());
  pConf->Write(_T("TideCorrectionEnabled"), IsTideCorrectionEnabled());
  pConf->Write(_T("TideDatumOffset"), GetTideDatumOffset());
  return true;
}

// Applied from LateInit, as soon as the host API handle exists — before any
// sounding can be recorded.
void DeepreyBathy_pi::ApplyStoredTideSettings() {
  wxFileConfig *pConf = m_pconfig;
  if (!pConf) return;

  pConf->SetPath(_T("/Settings/DeepreyBathymetry"));

  // Defaults on: an uncorrected survey is the problem this exists to solve, so
  // it should take a deliberate act to turn it off.
  bool tide = true;
  pConf->Read(_T("TideCorrectionEnabled"), &tide, true);
  SetTideCorrectionEnabled(tide);

  double datumOffset = 0.0;
  pConf->Read(_T("TideDatumOffset"), &datumOffset, 0.0);
  SetTideDatumOffset(datumOffset);
}

// Applied after the renderer exists, from the first render pass.
void DeepreyBathy_pi::ApplyStoredSettings() {
  wxFileConfig *pConf = m_pconfig;
  if (!pConf) return;

  pConf->SetPath(_T("/Settings/DeepreyBathymetry"));

  bool isobath = false;
  pConf->Read(_T("IsobathEnabled"), &isobath, false);
  SetContoursEnabled(isobath);

  double opacity = 1.0;
  pConf->Read(_T("BathymetryOpacity"), &opacity, 1.0);
  if (opacity < 0.01 || opacity > 1.0) opacity = 1.0;
  if (m_bathymetryRenderer)
    m_bathymetryRenderer->SetBathymetryOpacity((float)opacity);

  bool recording = false;
  pConf->Read(_T("LiveRecordingEnabled"), &recording, false);
  SetRecordingEnabled(recording);

  bool filters = false;
  pConf->Read(_T("FiltersEnabled"), &filters, false);
  SetFiltersEnabled(filters);
}

/** ------------------------------------------------------------------
 *  PLUGIN IDENTITY
 *  ------------------------------------------------------------------ */

int DeepreyBathy_pi::GetAPIVersionMajor() { return atoi(API_VERSION); }

int DeepreyBathy_pi::GetAPIVersionMinor() {
  std::string v(API_VERSION);
  size_t dotpos = v.find('.');
  return atoi(v.substr(dotpos + 1).c_str());
}

int DeepreyBathy_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }

int DeepreyBathy_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

int GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

int GetPlugInVersionPost() { return PLUGIN_VERSION_TWEAK; }

const char *GetPlugInVersionPre() { return PKG_PRERELEASE; }

const char *GetPlugInVersionBuild() { return PKG_BUILD_INFO; }

wxBitmap *DeepreyBathy_pi::GetPlugInBitmap() { return &m_panelBitmap; }

wxString DeepreyBathy_pi::GetCommonName() { return _T("DeepreyBathymetry"); }

wxString DeepreyBathy_pi::GetShortDescription() {
  return _("Record depth soundings and build your own bathymetric chart");
}

wxString DeepreyBathy_pi::GetLongDescription() {
  return _(
      "Record depth soundings from your echosounder as you sail, and build "
      "your own high-resolution bathymetric chart, rendered live on the chart "
      "canvas.\n\n"
      "Depth is read from standard NMEA 0183 DPT and DBT sentences and tagged "
      "with your GPS position. Soundings accumulate in a local tile database "
      "and are drawn as a colour-shaded depth surface with automatic contour "
      "lines. Existing survey data can be imported from, and exported to, XYZ "
      "files.");
}

/** ------------------------------------------------------------------
 *  UTILITIES
 *  ------------------------------------------------------------------ */

wxString DeepreyBathy_pi::DataFile(const wxString &filename) {
  wxFileName fn;
  fn.SetPath(GetPluginDataDir("deepreybathy_pi"));
  fn.AppendDir(_T("data"));
  fn.SetFullName(filename);
  return fn.GetFullPath();
}

wxImage DeepreyBathy_pi::LoadIMG(const wxString &filename, int width,
                                int height) {
  wxImage image;
  if (!image.LoadFile(DataFile(filename), wxBITMAP_TYPE_ANY)) {
    return wxImage();
  }

  if (width > 0 && height > 0) {
    image = image.Scale(width, height, wxIMAGE_QUALITY_HIGH);
  }
  return image;
}

wxBitmap DeepreyBathy_pi::MakeFallbackIcon(int width, int height) {
  wxBitmap bmp(width, height, 32);
  wxMemoryDC dc(bmp);
  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  // Flat stand-in for data/deepreybathy.svg: the same shallow-to-deep bands, drawn
  // as plain horizontal strips. Only ever seen if the data directory is missing,
  // so it just has to be recognisable and never blank.
  const wxColour bands[4] = {wxColour(168, 220, 239), wxColour(79, 159, 206),
                             wxColour(26, 106, 165), wxColour(10, 58, 107)};
  dc.SetPen(*wxTRANSPARENT_PEN);
  for (int i = 0; i < 4; ++i) {
    dc.SetBrush(wxBrush(bands[i]));
    const int top = 2 + (height - 4) * i / 4;
    const int bot = 2 + (height - 4) * (i + 1) / 4;
    dc.DrawRectangle(2, top, width - 4, bot - top);
  }

  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.SetPen(wxPen(wxColour(0, 10, 80), 2));
  dc.DrawRoundedRectangle(1, 1, width - 2, height - 2, 4);

  dc.SelectObject(wxNullBitmap);
  return bmp;
}

bool DeepreyBathy_pi::InitLogger() {
  if (m_initializedLog) return true;

  wxFileName fn;
  fn.SetPath(GetPluginDataDir("deepreybathy_pi"));
  fn.AppendDir(_T("data"));
  fn.SetFullName("deepreybathy.log");

  if (!m_DpLogFile.Open(fn.GetFullPath(), wxFile::write_append)) {
    return false;
  }

  m_initializedLog = true;
  return true;
}

void DeepreyBathy_pi::DeInitLogger() {
  if (!m_initializedLog) return;

  m_initializedLog = false;
  if (m_DpLogFile.IsOpened()) m_DpLogFile.Close();
}

void DeepreyBathy_pi::DpLog(const wxString &file, int line,
                           const wxColour &color, const wxString &message) {
  if (!m_initializedLog) return;

  wxString timestamp = wxDateTime::Now().FormatISOCombined(' ');
  wxString logMessage =
      wxString::Format("[%s] [%s:%d] %s", timestamp, file, line, message);

  if (m_DpLogFile.IsOpened()) {
    m_DpLogFile.Write(logMessage + "\n");
  }
}

void DeepreyBathy_pi::DpRequestRefresh() {
  m_needsRefresh = true;

  if (m_refreshTimer && !m_refreshTimer->IsRunning()) {
    m_refreshTimer->Start(RefreshConfig::REFRESH_INTERVAL_MS, wxTIMER_ONE_SHOT);
  }
}

void DeepreyBathy_pi::OnRefreshTimer(wxTimerEvent &event) {
  if (!m_needsRefresh) return;
  m_needsRefresh = false;

  wxWindow *canvas = GetOCPNCanvasWindow();
  if (canvas) {
    ::RequestRefresh(canvas);
  }
}
