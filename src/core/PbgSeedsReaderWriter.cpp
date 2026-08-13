/******************************************************************************
 Project: Deeprey Bathymetry

 Copyright (C) 2024-2026 Deeprey Research Ltd
 SPDX-License-Identifier: GPL-2.0-or-later
 *****************************************************************************/

#include "PbgSeedsReaderWriter.h"
#include <cstring>
#include <algorithm>
#include <wx/zstream.h>
#include <wx/mstream.h>
#include "DpPbg.h"

std::vector<uint8_t> PbgSeedsReaderWriter::Serialize(const FloatGrid& grid) {
    std::vector<uint8_t> result;
    
    result.push_back(VERSION);
    
    float min = grid.GetMin();
    float max = grid.GetMax();
    
    if (min == DpPbg::NO_DATA_VALUE) {
        min = 0.0f;
        max = 0.0f;
    }
    
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&min), 
                  reinterpret_cast<uint8_t*>(&min) + sizeof(float));
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&max), 
                  reinterpret_cast<uint8_t*>(&max) + sizeof(float));
    
    uint16_t resolution = grid.GetDimension();
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&resolution), 
                  reinterpret_cast<uint8_t*>(&resolution) + sizeof(uint16_t));
    
    result.push_back(0);
    
    std::vector<uint8_t> sparseData;
    Spacialize(grid, sparseData);
    
    uint16_t validPointCount = sparseData.size() / 6;
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&validPointCount), 
                  reinterpret_cast<uint8_t*>(&validPointCount) + sizeof(uint16_t));
    
    std::vector<uint8_t> compressed;
    if (!ZipBuffer(sparseData, compressed)) {
        return std::vector<uint8_t>();
    }
    
    int32_t compressedSize = compressed.size();
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&compressedSize), 
                  reinterpret_cast<uint8_t*>(&compressedSize) + sizeof(int32_t));
    
    result.insert(result.end(), compressed.begin(), compressed.end());
    
    return result;
}

bool PbgSeedsReaderWriter::Deserialize(const std::vector<uint8_t>& data, FloatGrid& grid) {
    if (data.size() < DpPbg::PBG_MIN_HEADER_SIZE) {
        return false;
    }
    
    const uint8_t* ptr = data.data();
    const uint8_t* endPtr = ptr + data.size();
    
    if (*ptr++ != VERSION) {
        return false;
    }
    
    float min = *reinterpret_cast<const float*>(ptr); ptr += sizeof(float);
    float max = *reinterpret_cast<const float*>(ptr); ptr += sizeof(float);
    uint16_t resolution = *reinterpret_cast<const uint16_t*>(ptr); ptr += sizeof(uint16_t);
    ptr++; // skip overlap
    uint16_t validPointCount = *reinterpret_cast<const uint16_t*>(ptr); ptr += sizeof(uint16_t);
    int32_t compressedSize = *reinterpret_cast<const int32_t*>(ptr); ptr += sizeof(int32_t);
    
    grid = FloatGrid(resolution);
    
    if (validPointCount == 0) {
        std::fill(grid.m_data.begin(), grid.m_data.end(), DpPbg::NO_DATA_VALUE);
        grid.m_min = DpPbg::NO_DATA_VALUE;
        grid.m_max = DpPbg::NO_DATA_VALUE;
        return true;
    }
    
    if (ptr + compressedSize > endPtr) {
        return false;
    }
    
    thread_local std::vector<uint8_t> uncompressed;
    uncompressed.reserve(65536);
    
    size_t expectedSize = validPointCount * 6;
    
    wxMemoryInputStream memIn(ptr, compressedSize);
    wxZlibInputStream zlibIn(memIn, wxZLIB_ZLIB);
    
    uncompressed.resize(expectedSize);
    zlibIn.Read(uncompressed.data(), expectedSize);
    
    if (zlibIn.GetLastError() != wxSTREAM_NO_ERROR || zlibIn.LastRead() != expectedSize) {
        return false;
    }
    
    std::fill(grid.m_data.begin(), grid.m_data.end(), DpPbg::NO_DATA_VALUE);
    
    const uint8_t* srcPtr = uncompressed.data();
    const uint8_t* srcEnd = srcPtr + expectedSize;
    const int dim = resolution;
    const int dimSquared = dim * dim;
    const int dimMinus1 = dim - 1;
    
    const int batchSize = 8;
    const uint8_t* batchEnd = srcEnd - (batchSize * 6);
    
    while (srcPtr <= batchEnd) {
        for (int b = 0; b < batchSize; ++b) {
            uint16_t index = *reinterpret_cast<const uint16_t*>(srcPtr); srcPtr += 2;
            float value = *reinterpret_cast<const float*>(srcPtr); srcPtr += 4;
            
            if (value > DpPbg::SEED_DISCRIMINENT_OFFSET) {
                value -= DpPbg::INTERPOLATED_OFFSET;
            }
            
            if (index < dimSquared) {
                int x = index % dim;
                int y = dimMinus1 - (index / dim);
                grid.m_data[x + y * dim] = value;
            }
        }
    }
    
    while (srcPtr < srcEnd) {
        uint16_t index = *reinterpret_cast<const uint16_t*>(srcPtr); srcPtr += 2;
        float value = *reinterpret_cast<const float*>(srcPtr); srcPtr += 4;
        
        if (value > DpPbg::SEED_DISCRIMINENT_OFFSET) {
            value -= DpPbg::INTERPOLATED_OFFSET;
        }
        
        if (index < dimSquared) {
            int x = index % dim;
            int y = dimMinus1 - (index / dim);
            grid.m_data[x + y * dim] = value;
        }
    }
    
    grid.m_min = min;
    grid.m_max = max;
    
    return true;
}

// ===== PbgSeedsReaderWriter.cpp =====
void PbgSeedsReaderWriter::Spacialize(const FloatGrid& grid,
                                      std::vector<uint8_t>& outRaw)
{
    const int dim = grid.GetDimension();
    outRaw.clear();
    outRaw.reserve(dim * dim * DpPbg::PBG_POINT_DATA_SIZE);  // Pire cas

    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            float v = grid.GetValue(x, y);
            if (v != DpPbg::NO_DATA_VALUE) {
                uint16_t idx = static_cast<uint16_t>(x + (dim - 1 - y) * dim);
                
                outRaw.insert(outRaw.end(),
                              reinterpret_cast<uint8_t*>(&idx),
                              reinterpret_cast<uint8_t*>(&idx) + sizeof(uint16_t));
                outRaw.insert(outRaw.end(),
                              reinterpret_cast<uint8_t*>(&v),
                              reinterpret_cast<uint8_t*>(&v) + sizeof(float));
            }
        }
    }
}

bool PbgSeedsReaderWriter::ZipBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    try {
        wxMemoryOutputStream memOut;
        {
            // wxZLIB_ZLIB emits zlib *with* headers, which is the on-disk tile
            // blob format. Raw deflate here would silently produce blobs the
            // reader cannot decompress.
            wxZlibOutputStream zlibOut(memOut, wxZ_DEFAULT_COMPRESSION, wxZLIB_ZLIB);
            zlibOut.Write(in.data(), in.size());
        } // Scope matters: the stream must be closed to flush the compressor.
        
        size_t compressedSize = memOut.GetSize();
        out.resize(compressedSize);
        memOut.CopyTo(out.data(), compressedSize);
        
        return true;
    }
    catch (...) {
        return false;
    }
}

bool PbgSeedsReaderWriter::UnzipBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expectedSize) {

    if (expectedSize == 0) {
        out.clear();
        return true;
    }

    wxMemoryInputStream memIn(in.data(), in.size());
    wxZlibInputStream   zlibIn(memIn, wxZLIB_ZLIB);

    out.resize(expectedSize);
    zlibIn.Read(out.data(), expectedSize);

    return zlibIn.GetLastError() == wxSTREAM_NO_ERROR &&
           zlibIn.LastRead()    == expectedSize;

}