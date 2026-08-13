/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "RealTimePointFilter.h"
#include <wx/log.h>
#include <cmath>
#include <sstream>
#include <wx/tokenzr.h>

RealTimePointFilter::RealTimePointFilter()
    : m_rangeEnabled(false)
    , m_repetitionEnabled(false)
    , m_speedEnabled(false)
    , m_spikeEnabled(false)
    , m_minDepth(0.3)
    , m_maxDepth(200.0)
    , m_maxConsecutive(5)
    , m_lastDepth(0.0)
    , m_consecutiveCount(0)
    , m_hasLastDepth(false)
    , m_maxSpeedKnots(15.0)
    , m_hasLastPosition(false)
    , m_lastCalculatedSpeed(0.0)
    , m_maxVariationPercent(50.0)
    , m_historySize(5)
{
}

RealTimePointFilter::~RealTimePointFilter() {
}

RealTimePointFilter::FilterResult RealTimePointFilter::ShouldAcceptPoint(
    const Position3D& point, double currentSpeedKnots) {
    
    FilterResult result;
    result.accepted = true;
    result.reason = NONE;
    result.rejectedValue = -point.z;
    
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.totalProcessed++;

    // Master filter bypass — when disabled, accept all points unfiltered
    if (!m_masterEnabled) {
        m_stats.totalAccepted++;
        return result;
    }

    if (m_rangeEnabled && !CheckRangeFilter(-point.z, result)) {
        UpdateStatistics(result);
        return result;
    }
    
    if (m_repetitionEnabled && !CheckRepetitionFilter(-point.z, result)) {
        UpdateStatistics(result);
        return result;
    }
    
    if (m_speedEnabled && !CheckSpeedFilter(point, currentSpeedKnots, result)) {
        UpdateStatistics(result);
        return result;
    }
    
    if (m_spikeEnabled && !CheckSpikeFilter(-point.z, result)) {
        UpdateStatistics(result);
        return result;
    }
    
    m_stats.totalAccepted++;
    
    if (m_spikeEnabled) {
        m_depthHistory.push_back(-point.z);
        if (m_depthHistory.size() > m_historySize) {
            m_depthHistory.pop_front();
        }
    }
    
    m_lastPosition = point;
    m_lastTimestamp = wxGetLocalTimeMillis();
    m_hasLastPosition = true;
    
    return result;
}

bool RealTimePointFilter::CheckRangeFilter(double depth, FilterResult& result) {
    if (depth < m_minDepth || depth > m_maxDepth) {
        result.accepted = false;
        result.reason = OUT_OF_RANGE;
        result.description = wxString::Format(
            "Depth %.1fm outside range [%.1f, %.1f]", 
            depth, m_minDepth, m_maxDepth).ToStdString();
        return false;
    }
    return true;
}

bool RealTimePointFilter::CheckRepetitionFilter(double depth, FilterResult& result) {
    if (!m_hasLastDepth) {
        m_lastDepth = depth;
        m_hasLastDepth = true;
        m_consecutiveCount = 1;
        return true;
    }
    
    if (depth == m_lastDepth) {
        m_consecutiveCount++;
        
        if (m_consecutiveCount >= m_maxConsecutive) {
            result.accepted = false;
            result.reason = REPEATED_VALUE;
            result.description = wxString::Format(
                "Depth %.1fm repeated %d times (max: %d)", 
                depth, m_consecutiveCount, m_maxConsecutive).ToStdString();
            return false;
        }
    } else {
        m_lastDepth = depth;
        m_consecutiveCount = 1;
    }
    
    return true;
}

bool RealTimePointFilter::CheckSpeedFilter(const Position3D& point, 
                                          double currentSpeedKnots, 
                                          FilterResult& result) {
    double speedToCheck = currentSpeedKnots;
    
    if (speedToCheck < 0 && m_hasLastPosition) {
        wxLongLong now = wxGetLocalTimeMillis();
        wxLongLong timeDiff = now - m_lastTimestamp;
        
        if (timeDiff > 0) {
            speedToCheck = CalculateSpeed(m_lastPosition, point, timeDiff);
            m_lastCalculatedSpeed = speedToCheck;
        } else {
            speedToCheck = m_lastCalculatedSpeed;
        }
    }
    
    if (speedToCheck >= 0 && speedToCheck > m_maxSpeedKnots) {
        result.accepted = false;
        result.reason = SPEED_TOO_HIGH;
        result.description = wxString::Format(
            "Speed %.1f knots exceeds maximum %.1f knots", 
            speedToCheck, m_maxSpeedKnots).ToStdString();
        return false;
    }
    
    return true;
}

bool RealTimePointFilter::CheckSpikeFilter(double depth, FilterResult& result) {
    if (m_depthHistory.size() < 3) {
        return true;
    }
    
    double median = CalculateMedian(m_depthHistory);
    double variation = std::abs(depth - median) / median * 100.0;
    
    if (variation > m_maxVariationPercent) {
        result.accepted = false;
        result.reason = SPIKE_DETECTED;
        result.description = wxString::Format(
            "Depth %.1fm varies %.1f%% from median %.1fm (max: %.1f%%)", 
            depth, variation, median, m_maxVariationPercent).ToStdString();
        return false;
    }
    
    return true;
}

double RealTimePointFilter::CalculateSpeed(const Position3D& p1, const Position3D& p2, 
                                          wxLongLong timeDiff) {
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double distanceMeters = std::sqrt(dx * dx + dy * dy);
    
    double distanceNm = distanceMeters * METERS_TO_NAUTICAL_MILES;
    double timeHours = timeDiff.ToDouble() * MS_TO_HOURS;
    
    if (timeHours > 0) {
        return distanceNm / timeHours;
    }
    
    return 0.0;
}

double RealTimePointFilter::CalculateMedian(const std::deque<double>& values) const {
    if (values.empty()) return 0.0;
    
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    
    size_t n = sorted.size();
    if (n % 2 == 0) {
        return (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    } else {
        return sorted[n/2];
    }
}

void RealTimePointFilter::UpdateStatistics(const FilterResult& result) {
    if (result.accepted) return;
    
    wxLongLong now = wxGetLocalTimeMillis();
    
    switch(result.reason) {
        case OUT_OF_RANGE:
            m_stats.rangeStats.rejected++;
            m_stats.rangeStats.lastRejectedValue = result.rejectedValue;
            m_stats.rangeStats.lastRejectedTime = now;
            break;
        case REPEATED_VALUE:
            m_stats.repetitionStats.rejected++;
            m_stats.repetitionStats.lastRejectedValue = result.rejectedValue;
            m_stats.repetitionStats.lastRejectedTime = now;
            break;
        case SPEED_TOO_HIGH:
            m_stats.speedStats.rejected++;
            m_stats.speedStats.lastRejectedValue = result.rejectedValue;
            m_stats.speedStats.lastRejectedTime = now;
            break;
        case SPIKE_DETECTED:
            m_stats.spikeStats.rejected++;
            m_stats.spikeStats.lastRejectedValue = result.rejectedValue;
            m_stats.spikeStats.lastRejectedTime = now;
            break;
        default:
            break;
    }
}

void RealTimePointFilter::SetHistorySize(size_t size) {
    m_historySize = size;
    while (m_depthHistory.size() > m_historySize) {
        m_depthHistory.pop_front();
    }
}

RealTimePointFilter::Statistics RealTimePointFilter::GetStatistics() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

void RealTimePointFilter::ResetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = Statistics();
}

void RealTimePointFilter::ResetFilterStates() {
    m_hasLastDepth = false;
    m_consecutiveCount = 0;
    m_hasLastPosition = false;
    m_depthHistory.clear();
}

void RealTimePointFilter::SetAllFiltersEnabled(bool enabled) {
    m_rangeEnabled = enabled;
    m_repetitionEnabled = enabled;
    m_speedEnabled = enabled;
    m_spikeEnabled = enabled;
}
