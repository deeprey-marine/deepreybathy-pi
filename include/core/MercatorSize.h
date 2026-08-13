/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef MERCATOR_SIZE_H
#define MERCATOR_SIZE_H

#include "Rcl.h"
#include "Position.h"

class MercatorSize {
public:
    MercatorSize() : m_width(0.0), m_height(0.0) {}
    MercatorSize(double width, double height) : m_width(width), m_height(height) {}
    
    double Width() const { return m_width; }
    double Height() const { return m_height; }
    
    static MercatorSize FromRcl(const Rcl& rcl) {
        Position tl = rcl.GetPositionTopLeft();
        Position br = rcl.GetPositionBottomRight();
        
        double width = br.X() - tl.X();
        double height = tl.Y() - br.Y();
        
        return MercatorSize(width, height);
    }
    
private:
    double m_width;
    double m_height;
};

#endif