/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Tide correction — reduce recorded soundings to chart datum

 Copyright (C) 2024-2026 Deeprey Research Ltd

 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "DpTideManager.h"

#include "ocpn_plugin.h"

#include <wx/log.h>

#include <cmath>
#include <cstring>

DpTideManager::DpTideManager(std::shared_ptr<HostApi121> api121)
    : m_api121(std::move(api121)) {
  if (!m_api121) {
    // Only happens if the host is older than the API version this plugin
    // declares, which OpenCPN should refuse to load in the first place.
    wxLogMessage(
        "deepreybathy: no HostApi121 — tide correction unavailable, soundings "
        "will not be reduced to datum");
  }
}

void DpTideManager::UpdatePosition(double lat, double lon) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_vesselLat = lat;
  m_vesselLon = lon;
  m_positionValid = true;
}

void DpTideManager::Update() {
  if (!m_enabled.load()) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_positionValid) return;

  // Gated on m_searchDone rather than on m_stationIndex: a failed search is
  // still a search. Testing the index instead would re-query the host on every
  // position fix for as long as no station is in range.
  if (!m_searchDone ||
      DistanceNm(m_vesselLat, m_vesselLon, m_stationSelectLat,
                 m_stationSelectLon) > RESELECT_DISTANCE_NM) {
    SelectNearestStation(m_vesselLat, m_vesselLon);
  }

  const time_t now = std::time(nullptr);
  if (m_stationIndex >= 0 && (now - m_lastComputeTime) >= RECOMPUTE_INTERVAL_SEC) {
    ComputeTideHeight();
  }
}

void DpTideManager::SetEnabled(bool enabled) {
  // Only on an actual transition. The prefs dialog re-applies every setting on
  // any checkbox change, so logging unconditionally puts a line in the OpenCPN
  // log for every click anywhere in the panel.
  if (m_enabled.exchange(enabled) == enabled) return;
  wxLogMessage("deepreybathy: tide correction %s",
               enabled ? "enabled" : "disabled");
}

bool DpTideManager::IsEnabled() const { return m_enabled.load(); }

void DpTideManager::SetDatumOffset(double meters) {
  if (std::isnan(meters)) meters = 0.0;
  if (meters < -MAX_DATUM_OFFSET) meters = -MAX_DATUM_OFFSET;
  if (meters > MAX_DATUM_OFFSET) meters = MAX_DATUM_OFFSET;

  std::lock_guard<std::mutex> lock(m_mutex);
  m_datumOffset = meters;
}

double DpTideManager::GetDatumOffset() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_datumOffset;
}

double DpTideManager::GetTideCorrection() {
  if (!m_enabled.load()) return 0.0;

  std::lock_guard<std::mutex> lock(m_mutex);
  // The datum offset stands on its own: it is a fixed water-level adjustment
  // and stays valid even when no station is available.
  return InterpolatedHeightLocked() + m_datumOffset;
}

bool DpTideManager::HasStation() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stationIndex >= 0 && m_heightValid;
}

std::string DpTideManager::GetStationName() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stationName;
}

double DpTideManager::GetStationDistanceNm() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stationDistNm;
}

double DpTideManager::GetCurrentTideHeight() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return InterpolatedHeightLocked();
}

double DpTideManager::InterpolatedHeightLocked() const {
  if (!m_heightValid || m_t0 == 0) return 0.0;

  const time_t now = std::time(nullptr);
  const double dt = static_cast<double>(m_t1 - m_t0);
  double t = 0.0;
  if (dt > 0.0) {
    t = static_cast<double>(now - m_t0) / dt;
    // Clamped to [0, 2] rather than [0, 1]: a late Update() leaves the samples
    // stale for a moment, and one interval of extrapolation along a tide curve
    // is far more accurate than freezing at h1.
    if (t < 0.0) t = 0.0;
    if (t > 2.0) t = 2.0;
  }
  return m_h0 + t * (m_h1 - m_h0);
}

void DpTideManager::SelectNearestStation(double lat, double lon) {
  // m_mutex held by caller.

  // Recorded before any early return, so that water with no usable station
  // nearby costs one host query per RESELECT_DISTANCE_NM travelled rather than
  // one per position fix.
  m_stationSelectLat = lat;
  m_stationSelectLon = lon;
  m_searchDone = true;

  PlugIn_TideStation station;
  std::memset(&station, 0, sizeof(station));

  if (!m_api121 || !m_api121->GetNearestTideStation(lat, lon, &station)) {
    if (m_stationIndex >= 0 || m_heightValid) {
      wxLogMessage("deepreybathy: no tide station found near %.4f, %.4f", lat,
                   lon);
    }
    m_stationIndex = -1;
    m_stationName.clear();
    m_stationDistNm = 0.0;
    m_heightValid = false;
    return;
  }

  const double dist = DistanceNm(lat, lon, station.lat, station.lon);
  // name is a fixed char[90] that the host is not required to terminate.
  const std::string name(station.name,
                         ::strnlen(station.name, sizeof(station.name)));

  if (dist > MAX_STATION_DISTANCE_NM) {
    wxLogMessage(
        "deepreybathy: nearest tide station '%s' is %.1f nm away (limit %.0f "
        "nm) — soundings will not be reduced to datum",
        name.c_str(), dist, MAX_STATION_DISTANCE_NM);
    m_stationIndex = -1;
    m_stationName.clear();
    m_stationDistNm = 0.0;
    m_heightValid = false;
    return;
  }

  m_stationIndex = station.index;
  m_stationName = name;
  m_stationDistNm = dist;
  // Force a recompute against the new station rather than carrying the old
  // station's samples until the next 30 s boundary.
  m_lastComputeTime = 0;
  m_heightValid = false;
  wxLogMessage("deepreybathy: tide station '%s' selected (%.1f nm away)",
               m_stationName.c_str(), dist);
}

void DpTideManager::ComputeTideHeight() {
  // m_mutex held by caller.
  const time_t now = std::time(nullptr);

  float h0 = 0.0f, h1 = 0.0f;
  const bool ok = m_api121 &&
                  m_api121->GetTideHeight(m_stationIndex, now, &h0) &&
                  m_api121->GetTideHeight(m_stationIndex,
                                          now + RECOMPUTE_INTERVAL_SEC, &h1);

  // Stamped whether or not the query succeeded, so a station with no harmonics
  // is retried once per interval instead of on every position fix.
  m_lastComputeTime = now;

  if (!ok) {
    // Only on the transition, or a station without tide data would log twice a
    // minute for the whole session.
    if (m_heightValid) {
      wxLogMessage(
          "deepreybathy: tide height unavailable for station '%s' — soundings "
          "are no longer being reduced to datum",
          m_stationName.c_str());
    }
    m_heightValid = false;
    return;
  }

  m_t0 = now;
  m_h0 = h0;
  m_t1 = now + RECOMPUTE_INTERVAL_SEC;
  m_h1 = h1;
  m_heightValid = true;
}

double DpTideManager::DistanceNm(double lat1, double lon1, double lat2,
                                 double lon2) {
  constexpr double DEG_TO_RAD = M_PI / 180.0;
  constexpr double EARTH_RADIUS_NM = 3440.065;

  const double dLat = (lat2 - lat1) * DEG_TO_RAD;
  const double dLon = (lon2 - lon1) * DEG_TO_RAD;
  const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                   std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) *
                       std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return EARTH_RADIUS_NM * c;
}
