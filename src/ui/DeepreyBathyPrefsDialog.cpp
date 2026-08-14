/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Preferences dialog — the plugin's entire settings UI

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>

#include "DeepreyBathyPrefsDialog.h"
#include "DeepreyBathy_pi.h"
#include "ImportExportManager.h"
#include "BathymetryDataManager.h"
#include "PbgRenderCacheManager.h"
#include "RealTimePointFilter.h"
#include "DpTideManager.h"

enum {
  ID_DV_OPACITY = wxID_HIGHEST + 1,
  ID_DV_IMPORT,
  ID_DV_EXPORT,
  ID_DV_CLEAR,
  ID_DV_STATUS_TIMER,
  ID_DV_TIDE_OFFSET
};

DeepreyBathyPrefsDialog::DeepreyBathyPrefsDialog(wxWindow *parent,
                                         DeepreyBathy_pi *plugin, bool modeless)
    : wxDialog(parent, wxID_ANY,
               modeless ? _("Deeprey Bathymetry")
                        : _("Deeprey Bathymetry — Preferences"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_plugin(plugin),
      m_modeless(modeless),
      m_statusTimer(this, ID_DV_STATUS_TIMER) {
  m_io = std::make_unique<ImportExportManager>();

  BuildControls();
  SyncFromPlugin();
  RefreshDatabaseInfo();
  RefreshStatus();
  RefreshTideStatus();

  if (m_modeless) {
    Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnCloseButton, this, wxID_CLOSE);
    // Escape is routed to a cancel-ish button id by wxDialog's char hook; catch
    // it too, or the default handler hides the window behind our back and the
    // toolbar button stays checked.
    Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnCloseButton, this, wxID_CANCEL);
    // No id: the five setting checkboxes are the only ones in the dialog, and
    // each one applies the whole control state anyway.
    Bind(wxEVT_CHECKBOX, &DeepreyBathyPrefsDialog::OnLiveSetting, this);
  } else {
    Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnOk, this, wxID_OK);
    Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnCancel, this, wxID_CANCEL);
  }

  Bind(wxEVT_CLOSE_WINDOW, &DeepreyBathyPrefsDialog::OnClose, this);
  Bind(wxEVT_SLIDER, &DeepreyBathyPrefsDialog::OnOpacity, this, ID_DV_OPACITY);
  Bind(wxEVT_SPINCTRLDOUBLE, &DeepreyBathyPrefsDialog::OnTideOffset, this,
       ID_DV_TIDE_OFFSET);
  Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnImport, this, ID_DV_IMPORT);
  Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnExport, this, ID_DV_EXPORT);
  Bind(wxEVT_BUTTON, &DeepreyBathyPrefsDialog::OnClear, this, ID_DV_CLEAR);
  Bind(wxEVT_TIMER, &DeepreyBathyPrefsDialog::OnStatusTimer, this,
       ID_DV_STATUS_TIMER);
}

DeepreyBathyPrefsDialog::~DeepreyBathyPrefsDialog() { m_statusTimer.Stop(); }

// The status line polls the injector, so the timer runs only while the window
// is on screen — a hidden panel must not keep polling for the whole session.
bool DeepreyBathyPrefsDialog::Show(bool show) {
  if (show) {
    SyncFromPlugin();
    RefreshDatabaseInfo();
    RefreshStatus();
    if (!m_statusTimer.IsRunning()) m_statusTimer.Start(1000);
  } else {
    m_statusTimer.Stop();
  }
  return wxDialog::Show(show);
}

void DeepreyBathyPrefsDialog::SyncFromPlugin() {
  m_origOverlay = m_plugin->IsBathymetryEnabled(0);
  m_origContours = m_plugin->IsContoursEnabled();
  m_origOpacity = m_plugin->GetOpacity();
  m_origRecord = m_plugin->IsRecordingEnabled();
  m_origFilter = m_plugin->AreFiltersEnabled();
  m_origTide = m_plugin->IsTideCorrectionEnabled();
  m_origTideOffset = m_plugin->GetTideDatumOffset();
  PushStateToControls();
}

void DeepreyBathyPrefsDialog::BuildControls() {
  wxFont dialogFont = GetOCPNGUIScaledFont_PlugIn(_T("Dialog"));
  SetFont(dialogFont);

  auto *top = new wxBoxSizer(wxVERTICAL);
  const int border = 6;

  m_chkOverlay = new wxCheckBox(this, wxID_ANY, _("Show bathymetry overlay"));
  top->Add(m_chkOverlay, 0, wxALL, border);

  m_chkContours = new wxCheckBox(this, wxID_ANY, _("Show contour lines"));
  top->Add(m_chkContours, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  auto *opacityRow = new wxBoxSizer(wxHORIZONTAL);
  opacityRow->Add(new wxStaticText(this, wxID_ANY, _("Opacity")), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
  m_sldOpacity = new wxSlider(this, ID_DV_OPACITY, 100, 10, 100);
  opacityRow->Add(m_sldOpacity, 1, wxALIGN_CENTER_VERTICAL);
  m_lblOpacity = new wxStaticText(this, wxID_ANY, _T("100 %"));
  opacityRow->Add(m_lblOpacity, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, border);
  top->Add(opacityRow, 0, wxEXPAND | wxALL, border);

  // --- Recording ---
  top->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, border);
  top->Add(new wxStaticText(this, wxID_ANY, _("Recording")), 0,
           wxLEFT | wxRIGHT, border);

  m_chkRecord =
      new wxCheckBox(this, wxID_ANY, _("Record depth soundings from NMEA"));
  top->Add(m_chkRecord, 0, wxALL, border);

  m_chkFilter =
      new wxCheckBox(this, wxID_ANY, _("Filter out bad depth readings"));
  top->Add(m_chkFilter, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  m_lblStatus = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_lblStatus, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  // --- Tide ---
  top->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, border);
  top->Add(new wxStaticText(this, wxID_ANY, _("Tide")), 0, wxLEFT | wxRIGHT,
           border);

  m_chkTide = new wxCheckBox(
      this, wxID_ANY, _("Reduce soundings to chart datum (tide correction)"));
  m_chkTide->SetToolTip(
      _("Subtracts the tide height at the nearest tide station from every "
        "recorded sounding, so passes made at different states of tide agree. "
        "Requires OpenCPN's tide data to be installed."));
  top->Add(m_chkTide, 0, wxALL, border);

  auto *tideRow = new wxBoxSizer(wxHORIZONTAL);
  tideRow->Add(new wxStaticText(this, wxID_ANY, _("Datum offset (m)")), 0,
               wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
  m_spinTideOffset = new wxSpinCtrlDouble(
      this, ID_DV_TIDE_OFFSET, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxSP_ARROW_KEYS, -DpTideManager::MAX_DATUM_OFFSET,
      DpTideManager::MAX_DATUM_OFFSET, 0.0, 0.1);
  m_spinTideOffset->SetDigits(2);
  m_spinTideOffset->SetToolTip(
      _("Fixed water-level adjustment, subtracted from every sounding along "
        "with the tide height. Use it when the tide station's datum differs "
        "from the chart datum. This is NOT the transducer offset — that comes "
        "from the DPT sentence and is already applied."));
  tideRow->Add(m_spinTideOffset, 0, wxALIGN_CENTER_VERTICAL);
  top->Add(tideRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  m_lblTideStatus = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_lblTideStatus, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  // --- Data ---
  top->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, border);
  top->Add(new wxStaticText(this, wxID_ANY, _("Data")), 0, wxLEFT | wxRIGHT,
           border);

  m_lblDbPath = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_lblDbPath, 0, wxALL, border);
  m_lblDbInfo = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_lblDbInfo, 0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  auto *dataRow = new wxBoxSizer(wxHORIZONTAL);
  dataRow->Add(new wxButton(this, ID_DV_IMPORT, _("Import XYZ...")), 0,
               wxRIGHT, border);
  dataRow->Add(new wxButton(this, ID_DV_EXPORT, _("Export XYZ...")), 0,
               wxRIGHT, border);
  dataRow->Add(new wxButton(this, ID_DV_CLEAR, _("Clear...")), 0);
  top->Add(dataRow, 0, wxALL, border);

  top->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, border);
  top->Add(new wxStaticText(this, wxID_ANY,
                            _("Deeprey Bathymetry — Deeprey Research Ltd")),
           0, wxLEFT | wxRIGHT | wxBOTTOM, border);

  // Nothing to revert in the modeless panel, so Cancel would be a lie.
  auto *buttons =
      CreateStdDialogButtonSizer(m_modeless ? wxCLOSE : (wxOK | wxCANCEL));
  top->Add(buttons, 0, wxEXPAND | wxALL, border);

  SetSizerAndFit(top);
  Centre();
}

void DeepreyBathyPrefsDialog::PushStateToControls() {
  m_chkOverlay->SetValue(m_origOverlay);
  m_chkContours->SetValue(m_origContours);
  m_chkRecord->SetValue(m_origRecord);
  m_chkFilter->SetValue(m_origFilter);
  m_chkTide->SetValue(m_origTide);
  m_spinTideOffset->SetValue(m_origTideOffset);

  int pct = static_cast<int>(m_origOpacity * 100.0f + 0.5f);
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  m_sldOpacity->SetValue(pct);
  m_lblOpacity->SetLabel(wxString::Format(_T("%d %%"), pct));
}

void DeepreyBathyPrefsDialog::ApplyControlsToPlugin() {
  const bool overlay = m_chkOverlay->GetValue();
  m_plugin->SetBathymetryEnabled(0, overlay);
  m_plugin->SetBathymetryEnabled(1, overlay);
  m_plugin->SetContoursEnabled(m_chkContours->GetValue());
  m_plugin->SetOpacity(m_sldOpacity->GetValue() / 100.0f);
  m_plugin->SetRecordingEnabled(m_chkRecord->GetValue());
  m_plugin->SetFiltersEnabled(m_chkFilter->GetValue());
  m_plugin->SetTideCorrectionEnabled(m_chkTide->GetValue());
  m_plugin->SetTideDatumOffset(m_spinTideOffset->GetValue());
  RefreshTideStatus();
}

void DeepreyBathyPrefsDialog::RefreshStatus() {
  RealtimePointInjector *injector = m_plugin->GetInjector();
  if (!injector) {
    m_lblStatus->SetLabel(_("Status: not initialised"));
    return;
  }

  const auto stats = injector->GetStatistics();
  wxString state = injector->IsRecordingEnabled() ? _("Recording") : _("Idle");
  m_lblStatus->SetLabel(wxString::Format(
      _("Status: %s — %zu points this session, %zu rejected"), state,
      stats.totalPointsInjected, stats.filterStats.totalProcessed -
                                     stats.filterStats.totalAccepted));
}

// "Enabled" and "actually correcting" are not the same thing — no tide data
// installed, or no station within range, leaves recording running with depths
// that are NOT reduced to datum. Say so plainly rather than let the checkbox
// imply the survey is corrected when it is not.
void DeepreyBathyPrefsDialog::RefreshTideStatus() {
  const DpTideManager *tide = m_plugin->GetTideManager();
  if (!tide) {
    m_lblTideStatus->SetLabel(_("Tide: not initialised"));
    return;
  }

  if (!tide->IsEnabled()) {
    m_lblTideStatus->SetLabel(_("Tide correction off — depths as measured"));
    return;
  }

  if (!tide->HasStation()) {
    m_lblTideStatus->SetLabel(
        _("Tide: no station data — depths NOT reduced to datum"));
    return;
  }

  m_lblTideStatus->SetLabel(wxString::Format(
      _("Tide: %s (%.1f nm) — %+.2f m"),
      wxString::FromUTF8(tide->GetStationName().c_str()),
      tide->GetStationDistanceNm(), tide->GetCurrentTideHeight()));
}

void DeepreyBathyPrefsDialog::RefreshDatabaseInfo() {
  if (!m_io) return;
  m_lblDbInfo->SetLabel(wxString::Format(
      _("%.1f MB  ·  %zu points  ·  %s"), m_io->GetDatabaseSizeMB(),
      m_io->GetTotalPointCount(), m_io->GetLastModifiedTime()));

  BathymetryRenderer *renderer = m_plugin->GetRenderer();
  if (renderer && renderer->GetDataManager()) {
    m_lblDbPath->SetLabel(
        renderer->GetDataManager()->GetDatabase().GetDatabasePath());
  }
}

void DeepreyBathyPrefsDialog::OnStatusTimer(wxTimerEvent &) {
  RefreshStatus();
  // Station selection and the tide sample cache both move on their own, so this
  // has to be polled rather than pushed.
  RefreshTideStatus();
}

void DeepreyBathyPrefsDialog::OnTideOffset(wxSpinDoubleEvent &) {
  m_plugin->SetTideDatumOffset(m_spinTideOffset->GetValue());
  RefreshTideStatus();
  // Modal mode defers persistence to OK, so that Cancel can still revert.
  if (m_modeless) m_plugin->SaveConfig();
}

void DeepreyBathyPrefsDialog::OnOpacity(wxCommandEvent &) {
  const int pct = m_sldOpacity->GetValue();
  m_lblOpacity->SetLabel(wxString::Format(_T("%d %%"), pct));
  // Applied live so the slider is meaningful while dragging. SetOpacity forces
  // the chart refresh itself.
  m_plugin->SetOpacity(pct / 100.0f);
  // Modal mode defers persistence to OK, so that Cancel can still revert.
  if (m_modeless) m_plugin->SaveConfig();
}

void DeepreyBathyPrefsDialog::OnLiveSetting(wxCommandEvent &) {
  ApplyControlsToPlugin();
  m_plugin->SaveConfig();
  m_plugin->DpRequestRefresh();
}

void DeepreyBathyPrefsDialog::OnCloseButton(wxCommandEvent &) { Close(); }

void DeepreyBathyPrefsDialog::OnClose(wxCloseEvent &event) {
  if (!m_modeless) {
    event.Skip();
    return;
  }
  // Hide, never destroy: the plugin owns this panel and reuses it across
  // toolbar clicks. Skipping here would let wxWidgets delete it underneath.
  Show(false);
  m_plugin->OnControlPanelClosed();
}

void DeepreyBathyPrefsDialog::OnOk(wxCommandEvent &) {
  ApplyControlsToPlugin();
  m_plugin->SaveConfig();
  m_plugin->DpRequestRefresh();
  EndModal(wxID_OK);
}

void DeepreyBathyPrefsDialog::OnCancel(wxCommandEvent &) {
  m_plugin->SetBathymetryEnabled(0, m_origOverlay);
  m_plugin->SetBathymetryEnabled(1, m_origOverlay);
  m_plugin->SetContoursEnabled(m_origContours);
  m_plugin->SetOpacity(m_origOpacity);
  m_plugin->SetRecordingEnabled(m_origRecord);
  m_plugin->SetFiltersEnabled(m_origFilter);
  m_plugin->SetTideCorrectionEnabled(m_origTide);
  m_plugin->SetTideDatumOffset(m_origTideOffset);
  m_plugin->DpRequestRefresh();
  EndModal(wxID_CANCEL);
}

// Import must pause the persistence thread and clear BOTH caches afterwards,
// otherwise SQLite trips over a stale snapshot and the display keeps showing
// pre-import data.
void DeepreyBathyPrefsDialog::OnImport(wxCommandEvent &) {
  wxFileDialog fd(this, _("Import XYZ file"), wxEmptyString, wxEmptyString,
                  _("XYZ files (*.xyz)|*.xyz|All files (*.*)|*.*"),
                  wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (fd.ShowModal() != wxID_OK) return;

  AsyncPersistenceManager *persistence = m_plugin->GetPersistenceManager();
  if (persistence) persistence->FlushAndPause();

  {
    wxProgressDialog progress(_("Importing"), _("Reading points..."), 100, this,
                              wxPD_APP_MODAL | wxPD_AUTO_HIDE |
                                  wxPD_CAN_ABORT | wxPD_ELAPSED_TIME);
    const bool ok = m_io->ImportXYZFile(fd.GetPath(), &progress);

    if (persistence) persistence->FlushNow();

    if (ok) {
      BathymetryRenderer *renderer = m_plugin->GetRenderer();
      if (renderer) {
        if (renderer->GetDataManager()) renderer->GetDataManager()->ClearCache();
        if (renderer->GetRenderCache()) renderer->GetRenderCache()->ClearCache();
      }
    }
  }

  if (persistence) persistence->ResumeFlushing();

  RefreshDatabaseInfo();
  m_plugin->DpRequestRefresh();
}

void DeepreyBathyPrefsDialog::OnExport(wxCommandEvent &) {
  wxFileDialog fd(this, _("Export XYZ file"), wxEmptyString, _T("bathymetry.xyz"),
                  _("XYZ files (*.xyz)|*.xyz|All files (*.*)|*.*"),
                  wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (fd.ShowModal() != wxID_OK) return;

  wxProgressDialog progress(_("Exporting"), _("Writing points..."), 100, this,
                            wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT |
                                wxPD_ELAPSED_TIME);
  m_io->ExportAllToXYZ(fd.GetPath(), &progress);
}

void DeepreyBathyPrefsDialog::OnClear(wxCommandEvent &) {
  if (wxMessageBox(_("Delete all recorded bathymetry data?\n\n"
                     "This cannot be undone."),
                   _("Clear bathymetry data"), wxYES_NO | wxICON_WARNING,
                   this) != wxYES) {
    return;
  }

  AsyncPersistenceManager *persistence = m_plugin->GetPersistenceManager();
  if (persistence) persistence->FlushAndPause();

  m_io->ClearDatabase();

  BathymetryRenderer *renderer = m_plugin->GetRenderer();
  if (renderer) {
    if (renderer->GetDataManager()) renderer->GetDataManager()->ClearCache();
    if (renderer->GetRenderCache()) renderer->GetRenderCache()->ClearCache();
  }

  if (persistence) persistence->ResumeFlushing();

  RefreshDatabaseInfo();
  m_plugin->DpRequestRefresh();
}
