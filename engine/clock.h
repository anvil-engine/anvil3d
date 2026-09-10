#pragma once

#include <algorithm>

namespace anvil {

// Engine time. Fed real elapsed seconds once per frame; decides how many fixed simulation ticks to run.
// Pure (no OS calls) so it is deterministic under test and replay.
class Clock {
public:
  // Source servers default to a 15 ms tick; the server DLL may request another interval.
  explicit Clock(double tickInterval = 0.015) : tickInterval_(tickInterval) {}

  // Returns the number of simulation ticks to run this frame.
  int advance(double realDelta) {
    realDelta = std::max(realDelta, 0.0);
    realTime_ += realDelta;
    // Clamp hitches (debugger breaks, loading) so the simulation does not try to catch up for seconds.
    frameTime_ = paused ? 0.0 : std::min(realDelta, kMaxFrameDelta) * timescale;
    accumulator_ += frameTime_;
    int ticks = 0;
    while (accumulator_ >= tickInterval_) {
      accumulator_ -= tickInterval_;
      ++ticks;
    }
    tickCount_ += ticks;
    return ticks;
  }

  double realTime() const { return realTime_; }
  double frameTime() const { return frameTime_; }
  double tickInterval() const { return tickInterval_; }
  long long tickCount() const { return tickCount_; }
  double simTime() const { return tickCount_ * tickInterval_; }
  // Fraction of the next tick already elapsed; used for client interpolation.
  double interpolation() const { return accumulator_ / tickInterval_; }

  bool paused = false;
  double timescale = 1.0;

private:
  static constexpr double kMaxFrameDelta = 0.25;
  double tickInterval_;
  double realTime_ = 0, frameTime_ = 0, accumulator_ = 0;
  long long tickCount_ = 0;
};

} // namespace anvil
