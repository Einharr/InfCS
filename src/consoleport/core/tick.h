// The frame tick: in the client it comes from QoL's runGameLoopOnce hook
// (0x004237C0), in the tests by hand with a given dt. Subscribers are our own
// subsystems (input, flyouts, cursor), called in registration order.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace cp {

class Tick {
public:
    typedef std::function<void(float dt)> Fn;
    static Tick& get() { static Tick t; return t; }
    int  add(Fn fn) { subs_.push_back(Sub{nextId_, fn}); return nextId_++; }
    void remove(int id) { for (size_t i = 0; i < subs_.size(); ++i) if (subs_[i].id == id) { subs_.erase(subs_.begin() + i); return; } }
    // dt in seconds, clamped so a pause or a lag spike cannot race the animations
    void run(float dt) { if (dt < 0) dt = 0; if (dt > 0.25f) dt = 0.25f; time_ += dt; std::vector<Sub> snap = subs_; for (Sub& s : snap) s.fn(dt); }
    double time() const { return time_; }
    void reset() { subs_.clear(); time_ = 0; nextId_ = 1; }
private:
    struct Sub { int id; Fn fn; };
    std::vector<Sub> subs_; double time_ = 0; int nextId_ = 1;
};

// Plain linear travel towards a target over a fixed time (ConsolePort's UIFade).
struct Tween {
    float value = 0, from = 0, to = 0, duration = 0, elapsed = 0;
    void set(float v) { value = from = to = v; elapsed = duration = 0; }
    void go(float target, float seconds) { if (target == to && duration > 0) return; from = value; to = target; duration = seconds; elapsed = 0; if (seconds <= 0) value = target; }
    bool active() const { return duration > 0 && elapsed < duration; }
    void step(float dt) { if (!active()) { value = to; return; } elapsed += dt; float k = elapsed >= duration ? 1.f : elapsed / duration; value = from + (to - from) * k; if (k >= 1.f) duration = 0; }
};

} // namespace cp
