/******************************************************************************
 Project: Deeprey Bathymetry
 Purpose: Tide correction — reduce recorded soundings to chart datum

 Copyright (C) 2024-2026 Deeprey Research Ltd

 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef _DP_TIDE_MANAGER_H_
#define _DP_TIDE_MANAGER_H_

#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

class HostApi121;

// Reduces measured depths to chart datum:
//
//     reduced_depth = measured_depth - (tide_height + datum_offset)
//
// Without this, two passes over the same ground at different states of tide
// record different depths and the pyramid averages them into a surface smeared
// by the whole tidal range.
//
// The nearest tide station is picked automatically from the vessel position and
// re-picked once the vessel has moved far enough that a closer one may exist.
// Tide height is sampled from the host on RECOMPUTE_INTERVAL_SEC boundaries and
// interpolated in between, so the per-sounding path costs no host calls.
//
// Requires plugin API 1.21 — GetNearestTideStation/GetTideHeight live on
// HostApi121 and have no equivalent at 1.18/1.19/1.20.
//
// Threading: everything here runs on the main thread today (position fixes and
// the NMEA depth listener both land there), but the mutex is kept so that
// moving depth ingestion onto a worker later cannot silently corrupt the
// sample cache.
class DpTideManager {
public:
  explicit DpTideManager(std::shared_ptr<HostApi121> api121);
  ~DpTideManager() = default;

  DpTideManager(const DpTideManager &) = delete;
  DpTideManager &operator=(const DpTideManager &) = delete;

  // Feed the ownship fix. Cheap — station re-selection happens in Update().
  void UpdatePosition(double lat, double lon);

  // Re-selects the station once the vessel has moved, and refreshes the cached
  // tide samples when they expire. Safe to call at any rate; both jobs are
  // rate-limited internally. Driven from SetPositionFixEx (~1 Hz).
  void Update();

  void SetEnabled(bool enabled);
  bool IsEnabled() const;

  // Fixed water-level offset in metres, added to the tide height and therefore
  // SUBTRACTED from every measured depth along with it. This is a datum
  // adjustment (station datum vs. chart datum), NOT a transducer offset — the
  // transducer offset arrives in the DPT sentence and is applied separately,
  // with the opposite sign.
  void SetDatumOffset(double meters);
  double GetDatumOffset() const;

  // Metres to subtract from a depth measured right now. Returns 0 when
  // disabled. Hot path — called once per accepted sounding.
  double GetTideCorrection();

  // True once a station within MAX_STATION_DISTANCE_NM has been found AND the
  // host has actually returned heights for it. While this is false and the
  // manager is enabled, soundings are still recorded but are NOT reduced to
  // datum, so the UI has to say so.
  bool HasStation() const;
  std::string GetStationName() const;
  double GetStationDistanceNm() const;

  // Interpolated tide height in metres, without the datum offset. For display.
  double GetCurrentTideHeight() const;

  static constexpr double MAX_DATUM_OFFSET = 20.0;  // metres, either sign

private:
  void SelectNearestStation(double lat, double lon);
  void ComputeTideHeight();
  // Caller must hold m_mutex. Shared by GetTideCorrection and
  // GetCurrentTideHeight, which differ only in the datum offset.
  double InterpolatedHeightLocked() const;

  static double DistanceNm(double lat1, double lon1, double lat2, double lon2);

  mutable std::mutex m_mutex;

  std::atomic<bool> m_enabled{false};

  double m_datumOffset{0.0};

  double m_vesselLat{0.0};
  double m_vesselLon{0.0};
  bool m_positionValid{false};

  // Position at which the current station was chosen. The re-selection test is
  // against this rather than against the station itself, so running along a
  // coast keeps handing off to whichever station is nearest.
  double m_stationSelectLat{0.0};
  double m_stationSelectLon{0.0};
  // A search has been run at that position — true even when it found nothing,
  // which is what keeps a stationless area from re-querying the host at 1 Hz.
  bool m_searchDone{false};
  int m_stationIndex{-1};
  std::string m_stationName;
  double m_stationDistNm{0.0};

  // Two host samples bracketing the present, linearly interpolated between. A
  // tide curve over 30 s is straight to well under a centimetre.
  time_t m_t0{0};
  float m_h0{0.0f};
  time_t m_t1{0};
  float m_h1{0.0f};
  time_t m_lastComputeTime{0};
  bool m_heightValid{false};

  std::shared_ptr<HostApi121> m_api121;

  static constexpr double RESELECT_DISTANCE_NM = 5.0;
  static constexpr double MAX_STATION_DISTANCE_NM = 50.0;
  static constexpr int RECOMPUTE_INTERVAL_SEC = 30;
};

#endif  // _DP_TIDE_MANAGER_H_
