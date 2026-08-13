/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#ifndef PBG_SEEDS_READER_WRITER_H
#define PBG_SEEDS_READER_WRITER_H
#include <vector>
#include <cstdint>
#include "FloatGrid.h"

class PbgSeedsReaderWriter {
public:
    static std::vector<uint8_t> Serialize(const FloatGrid& grid);
    static bool Deserialize(const std::vector<uint8_t>& data, FloatGrid& grid);
    
    static bool UnzipBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expectedSize);
    
private:
    static constexpr uint8_t VERSION = DpPbg::PBG_FORMAT_VERSION;
    
    static void Spacialize(const FloatGrid& grid, std::vector<uint8_t>& outRaw);
    static bool ZipBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
};
#endif