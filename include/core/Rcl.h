/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef RCL_H
#define RCL_H

#include "Position.h"
#include <cmath>
#include <functional>
#include <vector>
#include "DpPbg.h"

inline double MercatorHeight(const Position& topLeft, const Position& bottomRight) {
    return topLeft.Y() - bottomRight.Y(); // >0
}

inline double MercatorWidth(const Position& topLeft, const Position& bottomRight) {
    return bottomRight.X() - topLeft.X(); // >0
}

inline double GridCoordRange(int dim) { 
    return double(dim - 1);
}

class Rcl {
public:
    Rcl() : m_row(0), m_column(0), m_level(0) {}
    Rcl(int row, int column, int level) : m_row(row), m_column(column), m_level(level) {}
    
    int Row() const { return m_row; }
    int Column() const { return m_column; }
    int Level() const { return m_level; }
    
    int row() const { return m_row; }
    int column() const { return m_column; }
    int level() const { return m_level; }
    
    bool operator==(const Rcl& other) const {
        return m_row == other.m_row && 
               m_column == other.m_column && 
               m_level == other.m_level;
    }
    
    bool operator!=(const Rcl& other) const {
        return !(*this == other);
    }
    
    Rcl ResolveAntiMeridian() const {
        int numColumns = GetNumberColumnsAtLevel(m_level);
        int col = m_column;
        
        while (col < 0) col += numColumns;
        while (col >= numColumns) col -= numColumns;
        
        return Rcl(m_row, col, m_level);
    }
    
   bool Contains(const Position& pos) const {
    Position topLeft = GetPositionTopLeft();
    Position bottomRight = GetPositionBottomRight();
    
    return pos.X() >= topLeft.X() && pos.X() <= bottomRight.X() &&
           pos.Y() >= bottomRight.Y() && pos.Y() <= topLeft.Y();
}
    
   Position GetPositionTopLeft() const {
    double worldSize = Position::MercatorEquatorialSizeInMeter();
    int numTiles = GetNumberColumnsAtLevel(m_level);
    double tileSize = worldSize / numTiles;
    
    double x = m_column * tileSize - worldSize * 0.5;
    double y = (m_row + 1) * tileSize - worldSize * 0.5;
    
    return Position::FromMercator(x, y);
}
    
   Position GetPositionBottomRight() const {
    double worldSize = Position::MercatorEquatorialSizeInMeter();
    int numTiles = GetNumberColumnsAtLevel(m_level);
    double tileSize = worldSize / numTiles;
    
    double x = (m_column + 1) * tileSize - worldSize * 0.5;
    double y = m_row * tileSize - worldSize * 0.5;
    
    return Position::FromMercator(x, y);
}
    
   Position GetPositionBottomLeft() const {
    double worldSize = Position::MercatorEquatorialSizeInMeter();
    int numTiles = GetNumberColumnsAtLevel(m_level);
    double tileSize = worldSize / numTiles;
    
    double x = m_column * tileSize - worldSize * 0.5;
    double y = m_row * tileSize - worldSize * 0.5;
    
    return Position::FromMercator(x, y);
}
    
    Position GetPositionCenter() const {
        Position tl = GetPositionTopLeft();
        Position br = GetPositionBottomRight();
        
        return Position::FromMercator(
            (tl.X() + br.X()) * 0.5,
            (tl.Y() + br.Y()) * 0.5
        );
    }
    
    double GetPixelScaleMercator() const {
        double worldSize = Position::MercatorEquatorialSizeInMeter();
        int numTiles = GetNumberColumnsAtLevel(m_level);
        double tileSizeMeters = worldSize / numTiles;
        
        return tileSizeMeters / static_cast<double>(DpPbg::WORLD_TILE_SIZE);
    }
    
    void GetMercatorBounds(double& left, double& top, double& right, double& bottom) const {
        Position tl = GetPositionTopLeft();
        Position br = GetPositionBottomRight();
        
        left = tl.X();
        top = tl.Y();
        right = br.X();
        bottom = br.Y();
    }
    
    static int GetNumberColumnsAtLevel(int level) {
        if (level <= 0) return 1;
        return 1 << level;
    }
    
    static int GetNumberRowsAtLevel(int level) {
        return GetNumberColumnsAtLevel(level);
    }

    static Rcl FromKey(const std::string& key) {
        int level, row, col;
        if (sscanf(key.c_str(), "%d_%d_%d", &level, &row, &col) == 3) {
            return Rcl(row, col, level); 
        }
        return Rcl(0, 0, 0);
    }
    

    Rcl Parent() const {
        if (m_level <= 0) return Rcl(0, 0, 0);
        return Rcl(m_row / 2, m_column / 2, m_level - 1);
    }
    
    Rcl ChildNorthwest() const { return Rcl(m_row * 2 + 1, m_column * 2    , m_level + 1); }
    Rcl ChildNortheast() const { return Rcl(m_row * 2 + 1, m_column * 2 + 1, m_level + 1); }
    Rcl ChildSouthwest() const { return Rcl(m_row * 2    , m_column * 2    , m_level + 1); }
    Rcl ChildSoutheast() const { return Rcl(m_row * 2    , m_column * 2 + 1, m_level + 1); }
    
    bool IsAncestorOf(const Rcl& other) const {
        if (other.m_level <= m_level) return false;
        
        Rcl parent = other;
        while (parent.m_level > m_level) {
            parent = parent.Parent();
        }
        
        return parent == *this;
    }
    
    std::vector<Rcl> GetChildren() const {
        return {
            ChildNorthwest(),
            ChildNortheast(),
            ChildSouthwest(),
            ChildSoutheast()
        };
    }

private:
    int m_row;
    int m_column;
    int m_level;
};

struct RclHash {
    std::size_t operator()(const Rcl& rcl) const {
        return std::hash<int>()(rcl.Row()) ^ 
               (std::hash<int>()(rcl.Column()) << 1) ^
               (std::hash<int>()(rcl.Level()) << 2);
    }
};

#endif