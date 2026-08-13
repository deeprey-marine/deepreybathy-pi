/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <algorithm>
#include <numeric>
#include <wx/wx.h>
#include "Position.h"

class RealTimePointFilter {
public:
    enum RejectReason {
        NONE = 0,
        OUT_OF_RANGE,
        REPEATED_VALUE,
        SPEED_TOO_HIGH,
        SPIKE_DETECTED
    };

    struct FilterResult {
        bool accepted;
        RejectReason reason;
        std::string description;
        double rejectedValue;
    };

    struct Statistics {
        size_t totalProcessed = 0;
        size_t totalAccepted = 0;
        
        struct FilterStats {
            size_t rejected = 0;
            double lastRejectedValue = 0.0;
            wxLongLong lastRejectedTime = 0;
            
            double GetRejectionRate(size_t total) const {
                return total > 0 ? (rejected * 100.0 / total) : 0.0;
            }
        };
        
        FilterStats rangeStats;
        FilterStats repetitionStats;
        FilterStats speedStats;
        FilterStats spikeStats;
        
        double GetAcceptanceRate() const {
            return totalProcessed > 0 ? (totalAccepted * 100.0 / totalProcessed) : 100.0;
        }
        
        FilterStats GetStatsForReason(RejectReason reason) const {
            switch(reason) {
                case OUT_OF_RANGE: return rangeStats;
                case REPEATED_VALUE: return repetitionStats;
                case SPEED_TOO_HIGH: return speedStats;
                case SPIKE_DETECTED: return spikeStats;
                default: return FilterStats();
            }
        }
    };

    RealTimePointFilter();
    ~RealTimePointFilter();

    FilterResult ShouldAcceptPoint(const Position3D& point, double currentSpeedKnots = -1.0);

    // Range Filter Configuration
    void SetRangeFilterEnabled(bool enabled) { m_rangeEnabled = enabled; }
    bool IsRangeFilterEnabled() const { return m_rangeEnabled; }
    void SetMinDepth(double minDepth) { m_minDepth = minDepth; }
    double GetMinDepth() const { return m_minDepth; }
    void SetMaxDepth(double maxDepth) { m_maxDepth = maxDepth; }
    double GetMaxDepth() const { return m_maxDepth; }

    // Repetition Filter Configuration
    void SetRepetitionFilterEnabled(bool enabled) { m_repetitionEnabled = enabled; }
    bool IsRepetitionFilterEnabled() const { return m_repetitionEnabled; }
    void SetMaxConsecutiveRepetitions(int max) { m_maxConsecutive = max; }
    int GetMaxConsecutiveRepetitions() const { return m_maxConsecutive; }

    // Speed Filter Configuration
    void SetSpeedFilterEnabled(bool enabled) { m_speedEnabled = enabled; }
    bool IsSpeedFilterEnabled() const { return m_speedEnabled; }
    void SetMaxSpeedKnots(double maxSpeed) { m_maxSpeedKnots = maxSpeed; }
    double GetMaxSpeedKnots() const { return m_maxSpeedKnots; }

    // Spike Filter Configuration
    void SetSpikeFilterEnabled(bool enabled) { m_spikeEnabled = enabled; }
    bool IsSpikeFilterEnabled() const { return m_spikeEnabled; }
    void SetMaxVariationPercent(double percent) { m_maxVariationPercent = percent; }
    double GetMaxVariationPercent() const { return m_maxVariationPercent; }
    void SetHistorySize(size_t size);
    size_t GetHistorySize() const { return m_historySize; }

    // Statistics
    Statistics GetStatistics() const;
    void ResetStatistics();
    void ResetFilterStates();

    // Master filter gate - when OFF, all points pass through unfiltered
    void SetMasterEnabled(bool enabled) { m_masterEnabled = enabled; }
    bool IsMasterEnabled() const { return m_masterEnabled; }

    // Enable/Disable all filters
    void SetAllFiltersEnabled(bool enabled);

private:
    bool CheckRangeFilter(double depth, FilterResult& result);
    bool CheckRepetitionFilter(double depth, FilterResult& result);
    bool CheckSpeedFilter(const Position3D& point, double currentSpeedKnots, FilterResult& result);
    bool CheckSpikeFilter(double depth, FilterResult& result);

    double CalculateSpeed(const Position3D& p1, const Position3D& p2, wxLongLong timeDiff);
    double CalculateMedian(const std::deque<double>& values) const;

    void UpdateStatistics(const FilterResult& result);

private:
    // Master filter gate
    bool m_masterEnabled{false};  // Default OFF — all points pass unfiltered

    // Filter enabled states
    bool m_rangeEnabled;
    bool m_repetitionEnabled;
    bool m_speedEnabled;
    bool m_spikeEnabled;

    // Range filter parameters
    double m_minDepth;
    double m_maxDepth;

    // Repetition filter parameters
    int m_maxConsecutive;
    double m_lastDepth;
    int m_consecutiveCount;
    bool m_hasLastDepth;

    // Speed filter parameters
    double m_maxSpeedKnots;
    Position3D m_lastPosition;
    wxLongLong m_lastTimestamp;
    bool m_hasLastPosition;
    double m_lastCalculatedSpeed;

    // Spike filter parameters
    double m_maxVariationPercent;
    size_t m_historySize;
    std::deque<double> m_depthHistory;

    // Statistics
    mutable std::mutex m_statsMutex;
    Statistics m_stats;

    // Constants
    static constexpr double METERS_TO_NAUTICAL_MILES = 0.000539957;
    static constexpr double MS_TO_HOURS = 1.0 / 3600000.0;
};