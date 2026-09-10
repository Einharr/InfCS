#include "test.h"
#include "../input/dualsense.h"
#include "../input/pad.h"
#include "../core/runtime.h"
using namespace cp::input;

TEST(dualsense_usb_and_bt_have_same_controls)
{
    uint8_t usb[64]={1,120,130,140,150,200,230,0,0x28,0x31,3};
    uint8_t bt[10]={1,120,130,140,150,0x28,0x31,3,200,230};
    SonyState a,b;
    CHECK(decodeDualSense(usb,sizeof usb,a)); CHECK(decodeDualSense(bt,sizeof bt,b));
    for(int i=0;i<6;++i) CHECK_EQ(a.axes[i],b.axes[i]);
    CHECK_EQ(a.buttons,b.buttons); CHECK_EQ(a.pov,b.pov);
    CHECK_EQ(a.axes[3],200); CHECK_EQ(a.axes[4],230);
    uint8_t padded[78]={}; for(int i=0;i<10;++i) padded[i]=bt[i];
    CHECK(decodeDualSense(padded,sizeof padded,b)); CHECK_EQ(b.axes[4],230);
    CHECK(!decodeDualSense(bt,9,b)); CHECK(!decodeDualSense(nullptr,0,b));
}

TEST(dualsense_extended_crc_and_independent_triggers)
{
    uint8_t d[78]={0x31,0,128,128,128,128,255,255,0,8,0,0};
    uint32_t crc=0xffffffff;
    auto add=[&](uint8_t b) { crc^=b; for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xedb88320u:0); };
    add(0xa1); for(int i=0;i<74;++i) add(d[i]); crc=~crc;
    for(int i=0;i<4;++i) d[74+i]=uint8_t(crc>>(i*8));
    SonyState s; CHECK(decodeDualSense(d,78,s));
    CHECK_EQ(s.axes[3],255); CHECK_EQ(s.axes[4],255);
    d[6]=0; CHECK(!decodeDualSense(d,78,s));
    CHECK_EQ(s.axes[3],255); // rejected packet leaves last good state intact
}

TEST(pad_defaults_allow_auto_and_unseen_axes_do_not_move)
{
    cp::Config cfg; CHECK_EQ(cfg.triggerAxisL,-1); CHECK_EQ(cfg.triggerAxisR,-1);
    Pad p; CHECK(p.triggerAxesAuto());
    for(int i=0;i<6;++i) p.setAxisRange(i*4,0,65535);
    CHECK(!p.autoDetectTriggerAxes()); CHECK_EQ(p.leftStick().len(),0.f);
    DiEvent b[16]={}; uint32_t n=0; p.feed(b,n,16,false); CHECK_EQ(n,0u);
}

TEST(pad_movement_diagonal_release_and_ui)
{
    Pad p; p.setTriggerAxes(DI_RX,DI_RY);
    p.setAxisRange(DI_X,0,65535); p.setAxisRange(DI_Y,0,65535);
    DiEvent b[16]={{DI_X,65535,0,0,0},{DI_Y,0,0,0,0}}; uint32_t n=2;
    p.feed(b,n,16,false); CHECK_EQ(n,4u);
    CHECK_EQ(b[2].ofs,48u+18); CHECK_EQ(b[3].ofs,48u+21);
    p.setUiMode(true); n=0; p.feed(b,n,16,false);
    CHECK_EQ(n,2u); CHECK_EQ(b[0].data,0u); CHECK_EQ(b[1].data,0u);
    p.setUiMode(false); b[0]={DI_X,32767,0,0,0}; b[1]={DI_Y,32767,0,0,0}; n=2;
    p.feed(b,n,16,false); CHECK_EQ(n,2u);
}

TEST(pad_trigger_release_retries_after_full_buffer)
{
    Pad p; p.setTriggerAxes(DI_RX,DI_RY); p.setAxisRange(DI_RX,0,65535);
    DiEvent b[4]={{DI_RX,65535,0,0,0}}; uint32_t n=1;
    p.feed(b,n,4,false); CHECK_EQ(n,2u);
    b[0]={DI_RX,0,0,0,0}; n=1; p.feed(b,n,1,false); CHECK_EQ(n,1u);
    n=0; p.feed(b,n,4,false); CHECK_EQ(n,1u); CHECK_EQ(b[0].data,0u);
    CHECK_EQ(b[0].ofs,48u+31-1);
}
