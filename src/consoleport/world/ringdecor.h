// Radial styling: backdrop, rim, highlight wedge.
//
// Split out of TargetRing so the game menu radial (windows::GameBar) looks the same.
// Two rings that look different are two interfaces, not one.
//
// Three sprites in three textures (tools/ringtex.py), all white with alpha: a widget has
// one colour for the whole picture, so they cannot share a file - they would darken
// together.
//
// One wedge sprite serves any number of sectors: UIRenderHelper rotates the widget about
// its centre and UICanvas::Rotate takes TURNS, so the rotation is sector/N. The angle
// travels over FOLLOW_MS along the shortest arc - that motion is what makes it read as
// a radial menu.
#pragma once
#include "../core/node.h"

namespace cp { namespace world {

// One radius for every ring of ours, target rings and game menu alike, whatever they
// hold. A ring that resizes with its item count lands somewhere new every time and the
// thumb has to hunt for the direction again. The value comes from the worst case:
// twelve labels, the chord
static const int RING_RADIUS = 205;

class RingDecor {
public:
    // overlay: the HUD overlay. imageTemplate: an Image off OUR texture - a clone of
    // anything else cannot be given SourceResource at runtime (the crossbar's trap).
    // With no template the decor simply does not appear.
    bool attach(NodePtr overlay, NodePtr imageTemplate);
    void detach();
    bool attached() const { return overlay_ != nullptr && imageTpl_ != nullptr; }

    // Build and lay out at the overlay's centre for the given radius. rim = 0 takes the
    // default colour; the rim is the only instant cue for which ring this is.
    void show(int radius, const char* rim = nullptr);
    void hide();
    bool shown() const { return disc_ != nullptr; }

    // sector < 0: the stick is in the dead zone. The wedge fades but keeps its angle, so
    // the next deflection travels from where it was instead of jumping.
    void update(int sector, int count, float dt);

    float turn() const { return turn_; }        // turns, as the client sees them
    NodePtr wedge() const { return wedge_; }

private:
    NodePtr sprite(const char* name, const char* texture, const char* color, const char* opacity, int size);
    NodePtr overlay_, imageTpl_, disc_, rim_, wedge_;
    int radius_ = 0;
    float turn_ = 0.f, alpha_ = 0.f;
    int turnPainted_ = -1;
};

}} // namespace cp::world
