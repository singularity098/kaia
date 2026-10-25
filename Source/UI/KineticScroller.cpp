#include "KineticScroller.h"
#include "../Model/UserFiles.h"

namespace kaia
{

namespace
{
    constexpr int kDragThreshold = 8;           // px before a press becomes a drag
    constexpr double kVelocityWindow = 0.1;     // s of drag history used for the fling speed
    constexpr double kPauseBeforeRelease = 0.08; // s: holding still before letting go = no fling
    constexpr double kStopSpeed = 12.0;         // px/s
    constexpr double kWheelGlideTime = 0.06;    // s: time constant of the wheel glide

    double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
}

KineticScroller::KineticScroller (juce::Viewport& vp, juce::Component& dragSurface, juce::ScrollBar* bar)
    : viewport (vp), surface (dragSurface), scrollBar (bar),
      vblank (&dragSurface, [this] (double t) { frame (t); })
{
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    surface.addMouseListener (this, true);
    if (scrollBar != nullptr)
        scrollBar->addListener (this);
}

KineticScroller::~KineticScroller()
{
    surface.removeMouseListener (this);
    if (scrollBar != nullptr)
        scrollBar->removeListener (this);
}

float KineticScroller::flingFromSettings()
{
    juce::SharedResourcePointer<SharedSettings> settings;
    return (float) settings->get().getDoubleValue ("flingStrength", 0.6);
}

double KineticScroller::maxPosition() const
{
    auto* content = viewport.getViewedComponent();
    return content == nullptr ? 0.0 : (double) juce::jmax (0, content->getHeight() - viewport.getViewHeight());
}

void KineticScroller::start (Mode m)
{
    if (mode == Mode::idle)
        position = target = (double) viewport.getViewPositionY();
    mode = m;
    lastFrameTime = now();
}

void KineticScroller::stop()
{
    mode = Mode::idle;
    velocity = 0.0;
}

void KineticScroller::scrollTo (int y)
{
    stop();
    position = target = juce::jlimit (0.0, maxPosition(), (double) y);
    apply();
}

void KineticScroller::apply()
{
    auto y = juce::roundToInt (position);
    if (y != viewport.getViewPositionY())
        viewport.setViewPosition (viewport.getViewPositionX(), y);
    syncScrollBar();
}

void KineticScroller::syncScrollBar()
{
    auto* content = viewport.getViewedComponent();
    if (scrollBar == nullptr || content == nullptr)
        return;
    // (ScrollBar notifies its listeners asynchronously, so changes made here must not notify)
    scrollBar->setRangeLimits (0.0, (double) content->getHeight(), juce::dontSendNotification);
    scrollBar->setCurrentRange ((double) viewport.getViewPositionY(), (double) viewport.getViewHeight(), juce::dontSendNotification);
    scrollBar->setSingleStepSize (36.0);
}

void KineticScroller::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    stop();
    position = target = newRangeStart;
    viewport.setViewPosition (viewport.getViewPositionX(), juce::roundToInt (newRangeStart));
}

//==============================================================================
void KineticScroller::wheel (const juce::MouseWheelDetails& w)
{
    auto delta = (double) (w.isReversed ? -w.deltaY : w.deltaY) * 14.0 * 26.0;   // about two rows per wheel notch
    if (std::abs (delta) < 0.01 || pressed)
        return;

    if (w.isSmooth)
    {
        // Touchpads already send small, smooth steps: follow them (applied on the next frame)
        if (mode != Mode::follow)
        {
            stop();
            start (Mode::follow);
        }
        target = juce::jlimit (0.0, maxPosition(), target - delta);
        return;
    }

    // Mouse wheel notches glide to the new position
    if (mode != Mode::glide)
    {
        stop();
        start (Mode::glide);
    }
    target = juce::jlimit (0.0, maxPosition(), target - delta);
}

void KineticScroller::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Child components can hand the same event on more than once
    if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isAltDown()
         || std::exchange (lastWheelTime, e.eventTime) == e.eventTime)
        return;
    wheel (w);
}

void KineticScroller::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    pressStoppedMotion = mode == Mode::fling || mode == Mode::glide;
    stop();
    dragScrolling = dragDecided = false;
    dragStartPosition = position = target = (double) viewport.getViewPositionY();
    numSamples = 0;
}

void KineticScroller::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed || e.mods.isPopupMenu())
        return;

    auto offset = e.getScreenPosition() - e.getMouseDownScreenPosition();
    if (! dragDecided)
    {
        if (std::abs (offset.x) < kDragThreshold && std::abs (offset.y) < kDragThreshold)
            return;
        // A sideways drag is left to the item under the mouse (dragging it onto a pad)
        dragDecided = true;
        dragScrolling = std::abs (offset.y) >= std::abs (offset.x);
        if (dragScrolling)
            start (Mode::follow);
    }
    if (! dragScrolling)
        return;

    target = juce::jlimit (0.0, maxPosition(), dragStartPosition - (double) offset.y);
    if (mode != Mode::follow)
        start (Mode::follow);

    auto t = e.eventTime.toMilliseconds() / 1000.0;
    if (numSamples == (int) samples.size())
    {
        std::move (samples.begin() + 1, samples.end(), samples.begin());
        --numSamples;
    }
    samples[(size_t) numSamples++] = { t, (double) e.getScreenY() };
}

void KineticScroller::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    if (! dragScrolling)
        return;

    // Speed over the last moments of the drag; none if the finger stopped before letting go
    double speed = 0.0;
    auto releaseTime = e.eventTime.toMilliseconds() / 1000.0;
    if (numSamples >= 2 && releaseTime - samples[(size_t) numSamples - 1].time < kPauseBeforeRelease)
    {
        auto& last = samples[(size_t) numSamples - 1];
        int first = numSamples - 1;
        while (first > 0 && last.time - samples[(size_t) first - 1].time <= kVelocityWindow)
            --first;
        auto dt = last.time - samples[(size_t) first].time;
        if (dt > 0.008)
            speed = -(last.y - samples[(size_t) first].y) / dt;    // finger up = content moves down
    }

    position = target;
    velocity = speed * (0.8 + 0.8 * fling);
    if (fling > 0.0f && std::abs (velocity) > 60.0)
        mode = Mode::fling;
    else
        mode = Mode::follow;    // the last drag position is applied on the next frame, then idle
    lastFrameTime = now();
    // dragScrolling stays set until the next press so the owner's mouseUp can see it
}

//==============================================================================
void KineticScroller::frame (double timestamp)
{
    if (mode == Mode::idle)
        return;

    auto dt = juce::jlimit (0.0, 0.05, timestamp - lastFrameTime);
    lastFrameTime = timestamp;
    auto maxY = maxPosition();

    switch (mode)
    {
        case Mode::follow:
            position = target = juce::jlimit (0.0, maxY, target);
            if (! pressed)
                mode = Mode::idle;
            break;

        case Mode::glide:
        {
            target = juce::jlimit (0.0, maxY, target);
            position += (target - position) * (1.0 - std::exp (-dt / kWheelGlideTime));
            if (std::abs (target - position) < 0.5)
            {
                position = target;
                mode = Mode::idle;
            }
            break;
        }

        case Mode::fling:
        {
            // Friction: the speed falls off exponentially; stronger fling = longer time constant
            auto timeConstant = 0.08 * std::pow (15.0, (double) fling);    // 0.08 .. 1.2 s
            position += velocity * dt;
            velocity *= std::exp (-dt / timeConstant);
            if (position <= 0.0 || position >= maxY)
            {
                position = juce::jlimit (0.0, maxY, position);
                velocity = 0.0;
            }
            target = position;
            if (std::abs (velocity) < kStopSpeed)
                mode = Mode::idle;
            break;
        }

        case Mode::idle:
            break;
    }

    apply();
}

}
