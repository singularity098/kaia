#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kaia
{

// Vertical scrolling for a Viewport that feels right on touch screens and with a mouse wheel:
//  * dragging anywhere scrolls (Linux delivers touches as mouse events); a drag that starts
//    sideways is left alone, so the item under it can be dragged elsewhere,
//  * letting go while moving flings the list on, slowing down with friction (setFling),
//  * wheel notches glide, touchpad scrolling follows the fingers.
// Movement is applied once per display frame (VBlankAttachment), so fast input never paints
// more often than the screen refreshes. The viewport's own drag scrolling must be off.
class KineticScroller : private juce::MouseListener,
                        private juce::ScrollBar::Listener
{
public:
    // Mouse events are taken from `dragSurface` and all its children; `bar` (optional) is kept
    // in step and can be dragged.
    KineticScroller (juce::Viewport& viewport, juce::Component& dragSurface, juce::ScrollBar* bar);
    ~KineticScroller() override;

    // 0 = the list stops when let go, 1 = long glides. Shared setting "flingStrength".
    void setFling (float strength) noexcept      { fling = juce::jlimit (0.0f, 1.0f, strength); }
    static float flingFromSettings();

    void wheel (const juce::MouseWheelDetails&);
    void scrollTo (int y);                       // jumps (no animation)
    void stop();
    bool isMoving() const noexcept               { return mode != Mode::idle; }

    // True while the current press scrolls the list, or when it only stopped a moving one:
    // such presses must not count as clicks.
    bool pressIsScrolling() const noexcept      { return dragScrolling || pressStoppedMotion; }

    void syncScrollBar();

private:
    enum class Mode { idle, follow, glide, fling };

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void frame (double timestampSeconds);
    void start (Mode m);
    double maxPosition() const;
    void apply();

    juce::Viewport& viewport;
    juce::Component& surface;
    juce::ScrollBar* scrollBar;
    juce::VBlankAttachment vblank;

    Mode mode = Mode::idle;
    double position = 0.0, target = 0.0, velocity = 0.0, lastFrameTime = 0.0;
    float fling = 0.6f;

    // The current press
    bool pressed = false, dragScrolling = false, dragDecided = false, pressStoppedMotion = false;
    double dragStartPosition = 0.0;
    struct Sample { double time, y; };
    std::array<Sample, 8> samples {};
    int numSamples = 0;
    juce::Time lastWheelTime;
};

}
