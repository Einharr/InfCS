#include "dualsense.h"

namespace cp { namespace input {
static uint32_t crcByte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int i=0; i<8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    return crc;
}
bool decodeDualSense(const uint8_t* d, size_t n, SonyState& state)
{
    if (!d || !n) return false;
    size_t base, buttons, triggers;
    if (d[0] == 1 && (n == 10 || n == 78)) {
        base=1; buttons=5; triggers=8;
    } else if (d[0] == 1 && n == 64) {
        base=1; buttons=8; triggers=5;
    } else if (d[0] == 0x31 && n == 78) {
        uint32_t crc = crcByte(0xffffffffu, 0xa1);
        for (size_t i=0; i<n-4; ++i) crc=crcByte(crc,d[i]);
        uint32_t expected = uint32_t(d[74]) | uint32_t(d[75])<<8 |
                            uint32_t(d[76])<<16 | uint32_t(d[77])<<24;
        if ((~crc) != expected) return false;
        base=2; buttons=9; triggers=6;
    } else return false;
    SonyState s;
    s.axes[0]=d[base]; s.axes[1]=d[base+1];
    s.axes[2]=d[base+2]; s.axes[5]=d[base+3];
    s.axes[3]=d[triggers]; s.axes[4]=d[triggers+1];
    s.buttons=(d[buttons]>>4) | uint32_t(d[buttons+1])<<4 |
              uint32_t(d[buttons+2]&3)<<12;
    // Digital trigger bits also cover firmware that reports zero pressure.
    if (!s.axes[3] && (s.buttons & (1u<<6))) s.axes[3]=255;
    if (!s.axes[4] && (s.buttons & (1u<<7))) s.axes[4]=255;
    const int hat=d[buttons]&15;
    s.pov=hat<8 ? uint32_t(hat*4500) : 0xffffffffu;
    state=s;
    return true;
}
}}
