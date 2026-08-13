/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef FLOAT_GRID_H
#define FLOAT_GRID_H

#include <vector>
#include <cmath>
#include <limits>
#include <memory>
#include "DpPbg.h"
#include "RclExtensions.h"


class FloatGrid {
public:
    friend class PbgSeedsReaderWriter;
        
    FloatGrid(int dimension = DpPbg::PIXEL_TILE_SIZE);
    
    void AddPoint(int x, int y, float value);
    void SetValue(int x, int y, float value);
    float GetValue(int x, int y) const;
    void Merge(const FloatGrid& sourceGrid);
    void FillWithNoData();
    void FillWithZeroData();
    void RecomputeMinMax();

    void SetMinMax(float minValue, float maxValue) {
        m_min = minValue;
        m_max = maxValue;
    }
    
    float GetMin() const { return m_min; }
    float GetMax() const { return m_max; }
    int GetDimension() const { return m_dimension; }
    const std::vector<float>& GetData() const { return m_data; }
    std::vector<float>& GetData() { return m_data; }
    
    bool IsEmpty() const;
    
    void CopyFromChildren(const FloatGrid* topLeft,
                         const FloatGrid* topRight,
                         const FloatGrid* bottomLeft,
                         const FloatGrid* bottomRight);
    
    void ExtractPoints3D(const Rcl& tile,  std::vector<Position3D>& points) const;
    void RasterizePoints(const std::vector<Position3D>& points, const Rcl& tile);
    void GenerateBorderValues2(int overlap);

    static std::shared_ptr<FloatGrid> ExtractFromParent(
        std::shared_ptr<FloatGrid> parentGrid,
        const Rcl& parentTile,
        const Rcl& childTile);

    void CopyFromParent(std::shared_ptr<FloatGrid> parentGrid,  
                       const Rcl& parentTile,  
                       const Rcl& childTile);
    
    inline void SetValueDirect(int index, float value) {
        m_data[index] = value;
    }
    
    inline int PointToIndex(int x, int y) const {
        return x + y * m_dimension;
    }

private:
    std::vector<float> m_data;
    int m_dimension;
    float m_min;
    float m_max;
    
    static float DepthDataInterpolator(float topLeftValue, float topRightValue,
                                      float bottomLeftValue, float bottomRightValue,
                                      float& min, float& max);
};

#endif