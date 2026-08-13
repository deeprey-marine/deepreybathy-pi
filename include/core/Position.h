/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef POSITION_H
#define POSITION_H

#include <cmath>
#include "DpPbg.h"

struct Position3D {
    float x;
    float y;
    float z;
    
    Position3D() : x(0.0f), y(0.0f), z(0.0f) {}
    
    template<typename T1, typename T2, typename T3>
    Position3D(T1 x_, T2 y_, T3 z_) 
        : x(static_cast<float>(x_))
        , y(static_cast<float>(y_))
        , z(static_cast<float>(z_)) {}
};

class Position {
public:
    Position() : m_x(0.0), m_y(0.0) {}
    Position(double x, double y) : m_x(x), m_y(y) {}
    
    double X() const { return m_x; }
    double Y() const { return m_y; }
    
    static Position FromLatLon(double lat, double lon) {
        double x, y;
        LatLonToMercator(lat, lon, x, y);
        return Position(x, y);
    }
    
    static Position FromMercator(double x, double y) {
        return Position(x, y);
    }
    
    void ToLatLon(double& lat, double& lon) const {
        MercatorToLatLon(m_x, m_y, lat, lon);
    }
    
    static double MercatorEquatorialSizeInMeter() {
        return DpPbg::MERCATOR_WORLD_SIZE;
    }
    
private:
    double m_x;
    double m_y;
    

    
    static void LatLonToMercator(double lat, double lon, double& x, double& y) {
        x = lon * DpPbg::PI / 180.0 * DpPbg::EARTH_RADIUS;
        
        if (lat > DpPbg::MAX_LATITUDE) lat = DpPbg::MAX_LATITUDE;
        if (lat < -DpPbg::MAX_LATITUDE) lat = -DpPbg::MAX_LATITUDE;
        
        double latRad = lat * DpPbg::PI / 180.0;
        y = DpPbg::EARTH_RADIUS * log(tan(DpPbg::PI / 4.0 + latRad / 2.0));
    }
    
    static void MercatorToLatLon(double x, double y, double& lat, double& lon) {
        lon = x / DpPbg::EARTH_RADIUS * 180.0 / DpPbg::PI;
        lat = atan(sinh(y / DpPbg::EARTH_RADIUS)) * 180.0 / DpPbg::PI;
    }
};

#endif