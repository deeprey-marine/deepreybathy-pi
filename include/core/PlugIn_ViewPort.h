/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PLUGIN_VIEWPORT_H
#define PLUGIN_VIEWPORT_H

#ifndef STANDALONE_BUILD
#error "This header should only be included in STANDALONE_BUILD mode"
#endif

#include <wx/wx.h>


class PlugIn_ViewPort {
public:
    double clat;
    double clon;
    double view_scale_ppm;
    double skew;
    double rotation;
    float chart_scale;
    int pix_width;
    int pix_height;
    wxRect rv_rect;
    bool b_quilt;
    int m_projection_type;
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
    bool bValid;
};


#endif