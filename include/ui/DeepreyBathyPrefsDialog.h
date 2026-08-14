/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Preferences dialog — the plugin's entire settings UI

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef _DEEPREYBATHY_PREFS_DIALOG_H_
#define _DEEPREYBATHY_PREFS_DIALOG_H_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/timer.h>

#include <memory>

class DeepreyBathy_pi;
class ImportExportManager;

// A single window, no notebook. Every other render setting is fixed — see
// docs/OPENSOURCE_PLUGIN_SPEC.md §1.4b for why.
//
// Two modes, one class:
//   modal    — opened from Options → Plugins → Preferences. OK / Cancel, and
//              Cancel reverts to the snapshot taken when the dialog opened.
//   modeless — the toolbar control panel, GRIB-style. Every control applies and
//              persists on the spot, so there is nothing to revert and the only
//              button is Close. The plugin owns the instance and reuses it, so
//              closing hides rather than destroys.
class DeepreyBathyPrefsDialog : public wxDialog {
public:
  DeepreyBathyPrefsDialog(wxWindow *parent, DeepreyBathy_pi *plugin,
                      bool modeless = false);
  ~DeepreyBathyPrefsDialog() override;

  // The panel outlives the values it displays, so it re-reads them from the
  // plugin every time it is shown.
  void SyncFromPlugin();

  bool Show(bool show = true) override;

private:
  void BuildControls();
  void PushStateToControls();
  void ApplyControlsToPlugin();
  void RefreshStatus();
  void RefreshTideStatus();
  void RefreshDatabaseInfo();

  void OnOk(wxCommandEvent &event);
  void OnCancel(wxCommandEvent &event);
  void OnOpacity(wxCommandEvent &event);
  void OnTideOffset(wxSpinDoubleEvent &event);
  void OnImport(wxCommandEvent &event);
  void OnExport(wxCommandEvent &event);
  void OnClear(wxCommandEvent &event);
  void OnStatusTimer(wxTimerEvent &event);
  // Modeless only: any checkbox change applies and persists immediately.
  void OnLiveSetting(wxCommandEvent &event);
  void OnCloseButton(wxCommandEvent &event);
  void OnClose(wxCloseEvent &event);

  DeepreyBathy_pi *m_plugin;
  const bool m_modeless;

  // One long-lived instance. The old API implementation constructed a throwaway
  // per call, re-opening the database each time.
  std::unique_ptr<ImportExportManager> m_io;

  wxCheckBox *m_chkOverlay = nullptr;
  wxCheckBox *m_chkContours = nullptr;
  wxSlider *m_sldOpacity = nullptr;
  wxStaticText *m_lblOpacity = nullptr;
  wxCheckBox *m_chkRecord = nullptr;
  wxCheckBox *m_chkFilter = nullptr;
  wxStaticText *m_lblStatus = nullptr;
  wxCheckBox *m_chkTide = nullptr;
  wxSpinCtrlDouble *m_spinTideOffset = nullptr;
  wxStaticText *m_lblTideStatus = nullptr;
  wxStaticText *m_lblDbPath = nullptr;
  wxStaticText *m_lblDbInfo = nullptr;

  wxTimer m_statusTimer;

  // Snapshot taken on open so Cancel can revert. In modeless mode it is simply
  // the last state read from the plugin — nothing ever reverts to it.
  bool m_origOverlay = false;
  bool m_origContours = false;
  float m_origOpacity = 1.0f;
  bool m_origRecord = false;
  bool m_origFilter = false;
  bool m_origTide = false;
  double m_origTideOffset = 0.0;
};

#endif  // _DEEPREYBATHY_PREFS_DIALOG_H_
