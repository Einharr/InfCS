#pragma once
#include <cstddef>
#include <cstdint>

namespace cp { namespace input {
// Canonical Sony axes: X,Y,Z,Rx,Ry,Rz. Independent L2/R2 in every format.
struct SonyState {
    uint8_t axes[6] = {128,128,128,0,0,128};
    uint32_t buttons = 0;
    uint32_t pov = 0xffffffffu;
};
bool decodeDualSense(const uint8_t* data, size_t size, SonyState& state);
}} 
