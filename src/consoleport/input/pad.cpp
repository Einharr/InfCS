#include "pad.h"
#include <cmath>
#include <cstring>

namespace cp { namespace input {

using namespace gen;

float Stick::len() const { return std::sqrt(x * x + y * y); }

Pad::Pad() : profile_(&PROFILES[0]) {}

void Pad::setProfile(const char* name)
{
    profile_ = &PROFILES[0];
    for (int i = 0; i < PROFILE_COUNT; ++i) if (std::strcmp(PROFILES[i].name, name) == 0) profile_ = &PROFILES[i];
}

void Pad::setAxisRange(int ofs, int32_t lo, int32_t hi)
{
    int i = ofs / 4; if (i < 0 || i >= 8 || lo == hi) return;
    range_[i].lo = lo; range_[i].hi = hi; range_[i].known = true;
}

void Pad::forget()
{
    st_ = PadState(); prev_ = PadState();
    for (bool& v : seen_) v=false;
    for (bool& v : moveDown_) v=false;
    trigDown_[0] = trigDown_[1] = false;
    for (bool& d : dpadDown_) d = false;
    for (bool& d : keyDup_) d = false;
    for (float& h : held_) h = 0;
}

// Fallback only: a layout is never inferred from axes we have not actually seen.
void Pad::setTriggerAxes(int lo, int ro)
{
    if (lo < 0 || ro < 0 || lo > DI_RZ || ro > DI_RZ || lo%4 || ro%4) return;
    trigOfs_[0]=lo; trigOfs_[1]=ro; autoAxes_=false;
    trigDown_[0]=trigDown_[1]=false;
    setButton(JOYB_TRIGGER_L2,false); setButton(JOYB_TRIGGER_R2,false);
}

bool Pad::autoDetectTriggerAxes()
{
    const int rx = DI_RX / 4, ry = DI_RY / 4;
    if (!range_[rx].known || !range_[ry].known || !seen_[rx] || !seen_[ry]) return false;
    if (!atEdge(rx) && !atEdge(ry)) {
        trigOfs_[0] = DI_Z; trigOfs_[1] = DI_Z;     // a shared axis
    } else if (atEdge(rx) && atEdge(ry)) {
        trigOfs_[0] = DI_RX; trigOfs_[1] = DI_RY;   // one of its own for each
    }
    else return false;
    trigDown_[0]=trigDown_[1]=false;
    setButton(JOYB_TRIGGER_L2,false); setButton(JOYB_TRIGGER_R2,false);
    autoAxes_ = false;
    return true;
}

// Does the axis rest at the edge of its range, within a fifth of the travel?
bool Pad::atEdge(int ai) const
{
    const AxisRange& r = range_[ai];
    const float span = static_cast<float>(r.hi - r.lo);
    if (span <= 0) return false;
    const float f = (static_cast<float>(st_.axis[ai]) - static_cast<float>(r.lo)) / span;
    return f < 0.2f || f > 0.8f;
}

// Deflection from the centre in [-1, 1] - for triggers sharing one axis.
float Pad::deflect(int ai, int32_t raw) const
{
    const AxisRange& r = range_[ai];
    const float half = static_cast<float>(r.hi - r.lo) * 0.5f;
    if (half <= 0) return 0.f;
    const float centre = static_cast<float>(r.lo) + half;
    float d = (static_cast<float>(raw) - centre) / half;
    if (d > 1.f) d = 1.f;
    if (d < -1.f) d = -1.f;
    return d;
}

float Pad::travel(int i, int32_t raw) const
{
    const AxisRange& r = range_[i];
    if (!r.known) return 0;
    float f = static_cast<float>(raw - r.lo) / static_cast<float>(r.hi - r.lo);
    return trigRestHigh_ ? 1.f - f : f;
}

float Pad::axisNorm(int i) const
{
    const AxisRange& r = range_[i];
    if (!r.known || !seen_[i]) return 0;
    float mid = (r.lo + r.hi) * 0.5f; float half = (r.hi - r.lo) * 0.5f;
    float v = (st_.axis[i] - mid) / half;
    return v < -1 ? -1 : v > 1 ? 1 : v;
}

void Pad::emit(DiEvent* buf, uint32_t& count, uint32_t capacity, int joyb, bool down, const DiEvent& like)
{
    if (count >= capacity) return;   // no room - we stay silent, just as DI itself does
    if (!uiMode_) {
        if (down) sentSynthetic_ |= 1u<<joyb; else sentSynthetic_ &= ~(1u<<joyb);
    }
    DiEvent& e = buf[count++];
    e.ofs = DI_BUTTON0 + joyb; e.data = down ? 0x80u : 0u; e.stamp = like.stamp; e.seq = like.seq; e.appData = 0;
}

// POV in degrees*100: 0 up, 9000 right, 18000 down, 27000 left, diagonals between.
static void povToDpad(uint32_t pov, bool out[4])
{
    out[0] = out[1] = out[2] = out[3] = false;
    if (pov == DI_POV_CENTERED || (pov & 0xFFFF) == 0xFFFF) return;
    int a = static_cast<int>(pov % 36000);
    out[0] = a >= 31500 || a <= 4500;           // UP
    out[2] = a >= 4500 && a <= 13500;           // RIGHT   (index 3 in the JOYB order below)
    out[1] = a >= 13500 && a <= 22500;          // DOWN
    out[3] = a >= 22500 && a <= 31500;          // LEFT
}

uint32_t Pad::feed(DiEvent* buf, uint32_t& count, uint32_t capacity, bool peek)
{
    if (!buf) return 0;
    if (peek) return 0;
    uint32_t given = count;
    // Intercepted buttons are cut before parsing: we keep their state ourselves
    // (setButton still runs), the client sees nothing.
    if (suppressBtn_) {
        uint32_t keep = 0;
        for (uint32_t i = 0; i < given; ++i) {
            const DiEvent& e = buf[i];
            bool drop = false;
            if (e.ofs >= DI_BUTTON0 && e.ofs < DI_BUTTON0 + 32) {
                const int jb = static_cast<int>(e.ofs - DI_BUTTON0);
                for (int b2 = 0; b2 < B_COUNT && !drop; ++b2)
                    if ((suppressBtn_ >> b2) & 1u) {
                        const Button lb = static_cast<Button>(b2);
                        if (joyb(lb) == jb) drop = true;
                        if ((lb == B_L2 && jb == JOYB_TRIGGER_L2) || (lb == B_R2 && jb == JOYB_TRIGGER_R2)) drop = true;
                    }
                if (drop) setButton(jb, (e.data & 0x80) != 0);   // we track the state but do not pass the event on
            }
            if (!drop) buf[keep++] = buf[i];
        }
        given = keep; count = keep;
    }
    for (uint32_t i = 0; i < given; ++i) {
        const DiEvent& e = buf[i];
        if (e.ofs >= DI_BUTTON0 && e.ofs < DI_BUTTON0 + 32) {
            setButton(static_cast<int>(e.ofs - DI_BUTTON0), (e.data & 0x80) != 0);
        } else if (e.ofs < DI_POV0 && (e.ofs % 4) == 0) {
            int ai = static_cast<int>(e.ofs / 4);
            seen_[ai] = true;
            st_.axis[ai] = static_cast<int32_t>(e.data);
            // Triggers -> buttons 30/31, always, whatever the profile says. It used
            // to be off for ds4, where the device reports them as buttons 6 and 7
            // itself. Measured in game 8 Sep 2026: the device is fine, DirectInput
            // does report those buttons, but the CLIENT drops them - while 0..5 and
            // 8..11 work. So 30/31 are needed even where the triggers are buttons.
            const bool shared = triggersShareAxis();
            const bool isTrigAxis = e.ofs == static_cast<uint32_t>(trigOfs_[0])
                                 || e.ofs == static_cast<uint32_t>(trigOfs_[1]);
            if (!autoAxes_ && isTrigAxis && range_[ai].known && !extTrig_) {
                if (shared) {
                    // Shared axis: rest in the centre, and the sign says which
                    // trigger - L2 towards the maximum, R2 towards the minimum.
                    const float d = deflect(ai, st_.axis[ai]);
                    for (int t = 0; t < 2; ++t) {
                        const float v = t == 0 ? d : -d;
                        const bool now = trigDown_[t] ? v > trigOff_ : v >= trigOn_;
                        if (now != trigDown_[t]) {
                            trigDown_[t] = now;
                            const int jb = t == 0 ? JOYB_TRIGGER_L2 : JOYB_TRIGGER_R2;
                            setButton(jb, now); emit(buf, count, capacity, jb, now, e);
                        }
                    }
                } else {
                    const int t = e.ofs == static_cast<uint32_t>(trigOfs_[0]) ? 0 : 1;
                    const float tr = travel(ai, st_.axis[ai]);
                    const bool now = trigDown_[t] ? tr > trigOff_ : tr >= trigOn_;
                    if (now != trigDown_[t]) {
                        trigDown_[t] = now;
                        const int jb = t == 0 ? JOYB_TRIGGER_L2 : JOYB_TRIGGER_R2;
                        setButton(jb, now); emit(buf, count, capacity, jb, now, e);
                    }
                }
            }
        } else if (e.ofs == DI_POV0) {
            st_.pov = e.data;
            bool d[4]; povToDpad(e.data, d);
            static const int jb[4] = {JOYB_DPAD_UP, JOYB_DPAD_DOWN, JOYB_DPAD_RIGHT, JOYB_DPAD_LEFT};
            // povToDpad's order: 0 UP, 1 DOWN, 2 RIGHT, 3 LEFT
            for (int k = 0; k < 4; ++k) if (d[k] != dpadDown_[k]) { dpadDown_[k] = d[k]; setButton(jb[k], d[k]); emit(buf, count, capacity, jb[k], d[k], e); }
        }
    }
    // Movement goes on stock press/release commands, never the broken
    // CM_moveLongitudinal. Only axes we have seen can move anything, and a transition
    // is retried if the buffer was full.
    const float xy[4]={-axisNorm(1),axisNorm(1),-axisNorm(0),axisNorm(0)};
    DiEvent like = lastLike_;
    if (given) { like = buf[given-1]; lastLike_ = like; seqBump_ = 0; }
    else { like.seq = lastLike_.seq + (++seqBump_); }   // a number of our own, but not zero
    // Triggers from XInput: same hysteresis as the axis, but the values are
    // independent, so L2+R2 is finally distinguishable.
    if (extTrig_) {
        for (int t = 0; t < 2; ++t) {
            const bool now = trigDown_[t] ? extTrav_[t] > trigOff_ : extTrav_[t] >= trigOn_;
            if (now != trigDown_[t] && count < capacity) {
                trigDown_[t] = now;
                const int jb = t == 0 ? JOYB_TRIGGER_L2 : JOYB_TRIGGER_R2;
                setButton(jb, now); emit(buf, count, capacity, jb, now, like);
            }
        }
    }
    for (int k=0;k<4;++k) {
        bool next=!uiMode_ && (moveDown_[k] ? xy[k]>0.18f : xy[k]>=0.25f);
        if (next!=moveDown_[k] && count<capacity) {
            moveDown_[k]=next; emit(buf,count,capacity,18+k,next,like);
        }
    }
    // The right stick is the camera, and the client has no axial camera command - only
    // cameraYaw*/cameraPitch*, which are press/release. So the deflection becomes holds
    // of buttons 22..25, the same trick as movement. Which axes the stick arrives on
    // depends on the layout: Rx/Ry with merged triggers, Z/Rz natively (rightStick()).
    const int rxi = triggersShareAxis() ? 3 : 2, ryi = triggersShareAxis() ? 4 : 5;
    const float cam[4]={-axisNorm(rxi),axisNorm(rxi),-axisNorm(ryi),axisNorm(ryi)};
    for (int k=0;k<4;++k) {
        // Camera suppression releases the hold the way UI mode does: next goes false
        // and the transition sends the client a release.
        bool next=!uiMode_ && !suppressCam_ && (camDown_[k] ? cam[k]>0.18f : cam[k]>=0.25f);
        if (next!=camDown_[k] && count<capacity) {
            camDown_[k]=next; emit(buf,count,capacity,22+k,next,like);
        }
    }
    if (!uiMode_) {
        // A full buffer must not lose a modifier's release for good.
        for (int jb=26;jb<32;++jb) {
            const bool wanted=st_.down(jb), sent=(sentSynthetic_&(1u<<jb))!=0;
            if (wanted!=sent) emit(buf,count,capacity,jb,wanted,like);
        }
        // DirectInput fallback, merged triggers: only the right stick turns the camera.
        if (!autoAxes_ && triggersShareAxis()) {
            for (uint32_t i=0;i<given;++i) {
                if (buf[i].ofs==DI_Z) buf[i].ofs=DI_RX; // merged trigger: no camera binding
                else if (buf[i].ofs==DI_RX) {
                    float v=deflect(3,static_cast<int32_t>(buf[i].data));
                    if (std::fabs(v)<0.12f) v=0;
                    const AxisRange& target=range_[2];
                    buf[i].ofs=DI_Z;
                    buf[i].data=uint32_t(target.lo+(v+1)*0.5f*(target.hi-target.lo));
                }
            }
        }
        return 0;
    }
    // UI mode eats the input but still has to release whatever already reached the game.
    uint32_t eaten=count; count=0;
    for(int jb=18;jb<32 && count<capacity;++jb) if(sentSynthetic_&(1u<<jb)) {
        emit(buf,count,capacity,jb,false,like); sentSynthetic_&=~(1u<<jb);
    }
    return eaten;
}

bool Pad::feedKey(int scancode, bool down)
{
    static const int keys[6] = {KEY_DPAD_UP, KEY_DPAD_DOWN, KEY_DPAD_LEFT, KEY_DPAD_RIGHT, KEY_L2, KEY_R2};
    static const int jb[6] = {JOYB_DPAD_UP, JOYB_DPAD_DOWN, JOYB_DPAD_LEFT, JOYB_DPAD_RIGHT, JOYB_TRIGGER_L2, JOYB_TRIGGER_R2};
    for (int k = 0; k < 6; ++k) if (keys[k] == scancode) {
        if (keyDup_[k] != down) { keyDup_[k] = down; setButton(jb[k], down); }
        return uiMode_;
    }
    return false;
}

bool Pad::logicalDown(const PadState& s, Button b) const
{
    if (s.down(joyb(b))) return true;
    if (b == B_L2) return s.down(JOYB_TRIGGER_L2);
    if (b == B_R2) return s.down(JOYB_TRIGGER_R2);
    return false;
}

bool Pad::down(Button b) const     { return logicalDown(st_, b); }
bool Pad::pressed(Button b) const  { return logicalDown(st_, b) && !logicalDown(prev_, b); }
bool Pad::released(Button b) const { return !logicalDown(st_, b) && logicalDown(prev_, b); }

unsigned Pad::modBits() const
{
    unsigned m = 0;
    if (down(B_L1)) m |= MODBIT_L1;
    if (down(B_L2)) m |= MODBIT_L2;
    if (down(B_R2)) m |= MODBIT_R2;
    return m;
}

static Stick deadzone(float x, float y, float dz)
{
    Stick s; float l = std::sqrt(x * x + y * y);
    if (l < dz) return s;
    float k = (l - dz) / (1.f - dz) / l; s.x = x * k; s.y = y * k; return s;
}
Stick Pad::leftStick() const  { return deadzone(axisNorm(0), axisNorm(1), 0.2f); }
Stick Pad::rightStick() const { return triggersShareAxis() ? deadzone(axisNorm(3), axisNorm(4), 0.2f) : deadzone(axisNorm(2), axisNorm(5), 0.2f); }

void Pad::endFrame(float dt)
{
    for (int b = 0; b < B_COUNT; ++b) held_[b] = down(static_cast<Button>(b)) ? held_[b] + dt : 0.f;
    prev_ = st_;
}

Mod activeMod(const Pad& p)
{
    bool l = p.down(B_L2), r = p.down(B_R2);
    return l && r ? MOD_M3 : l ? MOD_L2 : r ? MOD_R2 : MOD_NONE;
}

}} // namespace cp::input
