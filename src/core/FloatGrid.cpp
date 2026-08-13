/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "FloatGrid.h"
#include <algorithm>
#include <cstring>

FloatGrid::FloatGrid(int dimension) 
    : m_dimension(dimension)
    , m_min(DpPbg::NO_DATA_VALUE)
    , m_max(DpPbg::NO_DATA_VALUE) {
    m_data.resize(dimension * dimension, DpPbg::NO_DATA_VALUE);
}

void FloatGrid::AddPoint(int x, int y, float value) {
    if (x >= 0 && x < m_dimension && y >= 0 && y < m_dimension) {
        int index = PointToIndex(x, y);
        
        m_data[index] = value;
        
        if (value != DpPbg::NO_DATA_VALUE) {
            if (m_min == DpPbg::NO_DATA_VALUE || value < m_min) m_min = value;
            if (m_max == DpPbg::NO_DATA_VALUE || value > m_max) m_max = value;
        }
    }
}

void FloatGrid::SetValue(int x, int y, float value) {
    if (x >= 0 && x < m_dimension && y >= 0 && y < m_dimension) {
        int index = PointToIndex(x, y);
        m_data[index] = value;
        
        if (value != DpPbg::NO_DATA_VALUE) {
            if (m_min == DpPbg::NO_DATA_VALUE || value < m_min) m_min = value;
            if (m_max == DpPbg::NO_DATA_VALUE || value > m_max) m_max = value;
        }
    }
}

float FloatGrid::GetValue(int x, int y) const {
    if (x >= 0 && x < m_dimension && y >= 0 && y < m_dimension) {
        return m_data[PointToIndex(x, y)];
    }
    return DpPbg::NO_DATA_VALUE;
}

void FloatGrid::CopyFromChildren(const FloatGrid* topLeft,
                                               const FloatGrid* topRight, 
                                               const FloatGrid* bottomLeft,
                                               const FloatGrid* bottomRight) {
    FillWithNoData();
    int overlap = DpPbg::TEXTURE_OVERLAP;
    const int halfSize = (m_dimension - 2 * overlap) / 2;
    if (bottomLeft) {
        for (int py = 0; py < halfSize; py++) {
            for (int px = 0; px < halfSize; px++) {
                float sum = 0.0f;
                int count = 0;
                
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int cx = overlap + px * 2 + dx;
                        int cy = overlap + py * 2 + dy;
                        float val = bottomLeft->GetValue(cx, cy);
                        if (val != DpPbg::NO_DATA_VALUE) {
                            sum += val;
                            count++;
                        }
                    }
                }
                
                if (count > 0) {
                    SetValue(overlap + px, overlap + py, sum / count);
                } else {
                    SetValue(overlap + px, overlap + py, DpPbg::NO_DATA_VALUE);
                }
            }
        }
    }
    
    if (bottomRight) {
        for (int py = 0; py < halfSize; py++) {
            for (int px = 0; px < halfSize; px++) {
                float sum = 0.0f;
                int count = 0;
                
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int cx = overlap + px * 2 + dx;
                        int cy = overlap + py * 2 + dy;
                        float val = bottomRight->GetValue(cx, cy);
                        if (val != DpPbg::NO_DATA_VALUE) {
                            sum += val;
                            count++;
                        }
                    }
                }
                
                if (count > 0) {
                    SetValue(overlap + halfSize + px, overlap + py, sum / count);
                } else {
                    SetValue(overlap + halfSize + px, overlap + py, DpPbg::NO_DATA_VALUE);
                }
            }
        }
    }
    
    if (topLeft) {
        for (int py = 0; py < halfSize; py++) {
            for (int px = 0; px < halfSize; px++) {
                float sum = 0.0f;
                int count = 0;
                
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int cx = overlap + px * 2 + dx;
                        int cy = overlap + py * 2 + dy;
                        float val = topLeft->GetValue(cx, cy);
                        if (val != DpPbg::NO_DATA_VALUE) {
                            sum += val;
                            count++;
                        }
                    }
                }
                
                if (count > 0) {
                    SetValue(overlap + px, overlap + halfSize + py, sum / count);
                } else {
                    SetValue(overlap + px, overlap + halfSize + py, DpPbg::NO_DATA_VALUE);
                }
            }
        }
    }
    
    if (topRight) {
        for (int py = 0; py < halfSize; py++) {
            for (int px = 0; px < halfSize; px++) {
                float sum = 0.0f;
                int count = 0;
                
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int cx = overlap + px * 2 + dx;
                        int cy = overlap + py * 2 + dy;
                        float val = topRight->GetValue(cx, cy);
                        if (val != DpPbg::NO_DATA_VALUE) {
                            sum += val;
                            count++;
                        }
                    }
                }
                
                if (count > 0) {
                    SetValue(overlap + halfSize + px, overlap + halfSize + py, sum / count);
                } else {
                    SetValue(overlap + halfSize + px, overlap + halfSize + py, DpPbg::NO_DATA_VALUE);
                }
            }
        }
    }
    
    RecomputeMinMax();
   
}

void FloatGrid::Merge(const FloatGrid& sourceGrid) {
    if (sourceGrid.m_dimension != m_dimension) {
        return;
    }
    
    float newMin = m_min;
    float newMax = m_max;
    
    for (int i = 0; i < m_dimension * m_dimension; i++) {
        float sourceValue = sourceGrid.m_data[i];
        float currentValue = m_data[i];
        float mergedValue = DpPbg::NO_DATA_VALUE;
        
        if (sourceValue != DpPbg::NO_DATA_VALUE && currentValue != DpPbg::NO_DATA_VALUE) {
            mergedValue = (sourceValue + currentValue) / 2.0f;
        } else if (sourceValue != DpPbg::NO_DATA_VALUE) {
            mergedValue = sourceValue;
        } else if (currentValue != DpPbg::NO_DATA_VALUE) {
            mergedValue = currentValue;
        }
        
        m_data[i] = mergedValue;
        
        if (mergedValue != DpPbg::NO_DATA_VALUE) {
            if (newMin == DpPbg::NO_DATA_VALUE || mergedValue < newMin) newMin = mergedValue;
            if (newMax == DpPbg::NO_DATA_VALUE || mergedValue > newMax) newMax = mergedValue;
        }
    }
    
    m_min = newMin;
    m_max = newMax;
}

void FloatGrid::FillWithNoData() {
    std::fill(m_data.begin(), m_data.end(), DpPbg::NO_DATA_VALUE);
    m_min = DpPbg::NO_DATA_VALUE;
    m_max = DpPbg::NO_DATA_VALUE;
}

void FloatGrid::FillWithZeroData() {
    std::fill(m_data.begin(), m_data.end(), 0.0f);
    m_min = 0.0f;
    m_max = 0.0f;
}

void FloatGrid::RecomputeMinMax() {
    m_min = DpPbg::NO_DATA_VALUE;
    m_max = DpPbg::NO_DATA_VALUE;
    
    for (int i = 0; i < m_dimension * m_dimension; i++) {
        float value = m_data[i];
        
        if (value != DpPbg::NO_DATA_VALUE && value != 0.0f) {
            if (m_min == DpPbg::NO_DATA_VALUE || value < m_min) m_min = value;
            if (m_max == DpPbg::NO_DATA_VALUE || value > m_max) m_max = value;
        }
    }
    
    if (m_min == DpPbg::NO_DATA_VALUE && m_max == DpPbg::NO_DATA_VALUE) {
        m_min = DpPbg::NO_DATA_VALUE;
        m_max = DpPbg::NO_DATA_VALUE;
    }
}

float FloatGrid::DepthDataInterpolator(float topLeftValue, float topRightValue,
                                       float bottomLeftValue, float bottomRightValue,
                                       float& min, float& max) {
    int validCount = 0;
    float sum = 0.0f;
    
    if (topLeftValue != DpPbg::NO_DATA_VALUE) {
        sum += topLeftValue;
        validCount++;
    }
    if (topRightValue != DpPbg::NO_DATA_VALUE) {
        sum += topRightValue;
        validCount++;
    }
    if (bottomLeftValue != DpPbg::NO_DATA_VALUE) {
        sum += bottomLeftValue;
        validCount++;
    }
    if (bottomRightValue != DpPbg::NO_DATA_VALUE) {
        sum += bottomRightValue;
        validCount++;
    }
    
    if (validCount == 0) {
        return DpPbg::NO_DATA_VALUE;
    }
    
    float result = sum / validCount;
    
    if (min == DpPbg::NO_DATA_VALUE || result < min) min = result;
    if (max == DpPbg::NO_DATA_VALUE || result > max) max = result;
    
    return result;
}

std::shared_ptr<FloatGrid> FloatGrid::ExtractFromParent(
    std::shared_ptr<FloatGrid> parentGrid,
    const Rcl& parentTile,
    const Rcl& childTile) {
    
    auto result = std::make_shared<FloatGrid>(DpPbg::PIXEL_TILE_SIZE);
    result->CopyFromParent(parentGrid, parentTile, childTile);
    return result;
}

bool FloatGrid::IsEmpty() const {
    return (m_max == DpPbg::NO_DATA_VALUE && m_min == DpPbg::NO_DATA_VALUE) || 
            (m_max == 0.0f && m_min == 0.0f);
}

void FloatGrid::CopyFromParent(
    std::shared_ptr<FloatGrid> parentGrid,
    const Rcl& parentTile,
    const Rcl& childTile) {
    
    FillWithNoData();
    
    int levelDiff = childTile.level() - parentTile.level();
    int factor = 1 << levelDiff;
    
    int relRow = childTile.row() - (parentTile.row() * factor);
    int relCol = childTile.column() - (parentTile.column() * factor);
    
    int subSize = DpPbg::PIXEL_TILE_SIZE / factor;
    int startX = relCol * subSize;
    int startY = relRow * subSize;
    
    for (int y = 0; y < DpPbg::PIXEL_TILE_SIZE; y++) {
        for (int x = 0; x < DpPbg::PIXEL_TILE_SIZE; x++) {
            float parentX = startX + ((float)x * subSize / DpPbg::PIXEL_TILE_SIZE);
            float parentY = startY + ((float)y * subSize / DpPbg::PIXEL_TILE_SIZE);
            
            int x0 = (int)parentX;
            int y0 = (int)parentY;
            int x1 = std::min(x0 + 1, DpPbg::PIXEL_TILE_SIZE - 1);
            int y1 = std::min(y0 + 1, DpPbg::PIXEL_TILE_SIZE - 1);
            
            float fx = parentX - x0;
            float fy = parentY - y0;
            
            float v00 = parentGrid->GetValue(x0, y0);
            float v10 = parentGrid->GetValue(x1, y0);
            float v01 = parentGrid->GetValue(x0, y1);
            float v11 = parentGrid->GetValue(x1, y1);
            
            if (v00 == DpPbg::NO_DATA_VALUE && v10 == DpPbg::NO_DATA_VALUE &&
                v01 == DpPbg::NO_DATA_VALUE && v11 == DpPbg::NO_DATA_VALUE) {
                SetValue(x, y, DpPbg::NO_DATA_VALUE);
                continue;
            }
            
            float validSum = 0;
            int validCount = 0;
            if (v00 != DpPbg::NO_DATA_VALUE) { validSum += v00; validCount++; }
            if (v10 != DpPbg::NO_DATA_VALUE) { validSum += v10; validCount++; }
            if (v01 != DpPbg::NO_DATA_VALUE) { validSum += v01; validCount++; }
            if (v11 != DpPbg::NO_DATA_VALUE) { validSum += v11; validCount++; }
            
            if (validCount == 0) {
                SetValue(x, y, DpPbg::NO_DATA_VALUE);
                continue;
            }
            
            float avgValue = validSum / validCount;
            if (v00 == DpPbg::NO_DATA_VALUE) v00 = avgValue;
            if (v10 == DpPbg::NO_DATA_VALUE) v10 = avgValue;
            if (v01 == DpPbg::NO_DATA_VALUE) v01 = avgValue;
            if (v11 == DpPbg::NO_DATA_VALUE) v11 = avgValue;
            
            float value = v00 * (1 - fx) * (1 - fy) +
                         v10 * fx * (1 - fy) +
                         v01 * (1 - fx) * fy +
                         v11 * fx * fy;
            
            SetValue(x, y, value);
        }
    }

    
}

void FloatGrid::GenerateBorderValues2(int overlap)
{
    const int n    = m_dimension;          // 256
    const int last = n - 1;

    if (overlap <= 0 || overlap >= n / 4) {
        return;
    }

    const int row2 = overlap * 2;
    const int row3 = row2 + 1;

    auto valid = [](float v) { return v != DpPbg::NO_DATA_VALUE; };

    for (int x = overlap; x < n - overlap; ++x) {
        float v2 = GetValue(x, row2);
        float v3 = GetValue(x, row3);
        if (valid(v2) && valid(v3)) {
            float g = v3 - v2;
            SetValue(x, overlap - 1, v2 - g);     // ligne 1
            SetValue(x, 0,          v2 - 2 * g);  // ligne 0
        }
    }

    for (int x = overlap; x < n - overlap; ++x) {
        float v2 = GetValue(x, last - row2);
        float v3 = GetValue(x, last - row3);
        if (valid(v2) && valid(v3)) {
            float g = v3 - v2;
            SetValue(x, last - (overlap - 1), v2 + g);     // ligne n‑2
            SetValue(x, last,                 v2 + 2 * g); // ligne n‑1
        }
    }

    for (int y = overlap; y < n - overlap; ++y) {
        float v2 = GetValue(row2, y);
        float v3 = GetValue(row3, y);
        if (valid(v2) && valid(v3)) {
            float g = v3 - v2;
            SetValue(overlap - 1, y, v2 - g);     // col 1
            SetValue(0,            y, v2 - 2 * g); // col 0
        }
    }

    for (int y = overlap; y < n - overlap; ++y) {
        float v2 = GetValue(last - row2, y);
        float v3 = GetValue(last - row3, y);
        if (valid(v2) && valid(v3)) {
            float g = v3 - v2;
            SetValue(last - (overlap - 1), y, v2 + g);     // col n‑2
            SetValue(last,                 y, v2 + 2 * g); // col n‑1
        }
    }
}

void FloatGrid::ExtractPoints3D(const Rcl& tile,  std::vector<Position3D>& points) const {
    points.clear();

    const int gridDim = m_dimension;
    //const int OVERLAP = DpPbg::TEXTURE_OVERLAP;
  

    Position extTopLeft, extBottomRight;

    int overlap = 0;

    if (tile.Level() >= 21) {
       overlap = DpPbg::DATA_OVERLAP * 2;
    }else if (tile.Level() >= 16){
        overlap = DpPbg::DATA_OVERLAP;
    }

    RclExtensions::GetReducedRclPosition(tile, gridDim, overlap, extTopLeft, extBottomRight);

    const double extWidth  = extBottomRight.X() - extTopLeft.X();
    const double extHeight = extTopLeft.Y()     - extBottomRight.Y();

    const double coordRange = double(gridDim - 1);

    for (int y = 0; y < gridDim; ++y)
    {
        const double relY = double(y) / coordRange;
        const double mercY = extBottomRight.Y() + relY * extHeight;

        for (int x = 0; x < gridDim; ++x)
        {
            float v = GetValue(x, y);
            if (v == DpPbg::NO_DATA_VALUE)
                continue;

            const double relX = double(x) / coordRange;
            const double mercX = extTopLeft.X() + relX * extWidth;

            points.emplace_back(mercX, mercY, v);
        }
    }
}

void FloatGrid::RasterizePoints(const std::vector<Position3D>& points,
                                const Rcl& tile) {
    const int dim = m_dimension;
    const double coordRange = dim - 1;

    Position extTL, extBR;
    RclExtensions::GetExtendedRclPosition(tile, extTL, extBR);

    const double w = extBR.X() - extTL.X();
    const double h = extTL.Y() - extBR.Y();

    for (const auto& p : points) {
        double relX = (p.x - extTL.X()) / w;
        double relY = (p.y - extBR.Y()) / h;

        if (relX < 0.0 || relX > 1.0 || relY < 0.0 || relY > 1.0)
            continue;

        int gx = static_cast<int>(relX * coordRange);
        int gy = static_cast<int>(relY * coordRange);
        
        if (gx >= 0 && gx < dim && gy >= 0 && gy < dim) {
            AddPoint(gx, gy, static_cast<float>(p.z));
        }
    }
}

