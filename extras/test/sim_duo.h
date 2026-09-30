// Two PacketLED nodes running at the same time on one simulated clock.
//
// Each node runs in its own thread, but only one thread runs at a time. A node
// may run ahead of the other by up to kLookaheadUs; before reading the light
// that reached it, it waits until the other node has caught up, so every
// reading sees all the light that was actually emitted. This lets both nodes
// call the blocking API (send(), parsePacket()) concurrently, as on hardware.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "sim_phy.h"

class DuoClock {
 public:
  static constexpr double kLookaheadUs = 2000;

  double now(int id) const { return t_[id]; }

  // Called by node `id` while it holds the turn.
  void advance(int id, double us) {
    t_[id] += us;
    if (t_[id] > t_[1 - id] + kLookaheadUs) yield(id);
  }

  // Waits until the other node has reached time t (or has finished).
  void waitOther(int id, double t) {
    while (!done_[1 - id] && t_[1 - id] < t) yield(id);
  }

  void run(const std::function<void()> &node0, const std::function<void()> &node1) {
    std::thread a([&] { body(0, node0); });
    std::thread b([&] { body(1, node1); });
    a.join();
    b.join();
  }

 private:
  void body(int id, const std::function<void()> &fn) {
    {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait(lock, [&] { return turn_ == id; });
    }
    fn();
    std::unique_lock<std::mutex> lock(m_);
    done_[id] = true;
    turn_ = 1 - id;
    cv_.notify_all();
  }

  void yield(int id) {
    std::unique_lock<std::mutex> lock(m_);
    if (done_[1 - id]) return;
    turn_ = 1 - id;
    cv_.notify_all();
    cv_.wait(lock, [&] { return turn_ == id || done_[1 - id]; });
    turn_ = id;
  }

  std::mutex m_;
  std::condition_variable cv_;
  int turn_ = 0;
  double t_[2] = {0, 0};
  bool done_[2] = {false, false};
};

class DuoPhy : public LedPhy {
 public:
  DuoPhy(DuoClock &clock, int id, Channel &tx, Channel &rx, const SensorModel &m, uint32_t seed)
      : clock_(clock), id_(id), tx_(tx), rx_(rx), m_(m), rng_(seed) {}

  void setClock(double offsetUs, double ppm) {
    offset_ = offsetUs;
    ppm_ = ppm;
  }
  double now() const { return clock_.now(id_); }
  void sleepUs(double us) { clock_.advance(id_, us); }

  uint32_t micros() override {
    clock_.advance(id_, 0.5);
    return local(now());
  }
  void idle() override { clock_.advance(id_, 1000); }
  void delayMs(uint32_t ms) override { clock_.advance(id_, ms * 1000.0); }
  void ledOn() override {
    tx_.push(now() + m_.ledOnLat, true);
    clock_.advance(id_, m_.ledCallUs);
  }
  void ledOff() override {
    tx_.push(now() + m_.ledOffLat, false);
    clock_.advance(id_, m_.ledCallUs);
  }
  uint16_t integrate(uint32_t windowUs, uint32_t &startUs) override {
    clock_.advance(id_, m_.setupUs + jitter_(rng_));
    const double start = now();
    startUs = local(start);
    clock_.advance(id_, windowUs + m_.sampleDelayUs);
    const double end = now();
    clock_.waitOther(id_, end);
    double v = m_.gain * rx_.litTime(start, end) + m_.ambient * (end - start) - m_.deadZone;
    if (v < 0) v = 0;
    v += gauss_(rng_) * (m_.noiseLsb + m_.noiseRel * v);
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    clock_.advance(id_, m_.readUs - m_.sampleDelayUs + m_.offUs);
    return (uint16_t)std::lround(v);
  }
  uint32_t random32() override { return (uint32_t)rng_(); }

 private:
  uint32_t local(double t) const { return (uint32_t)(int64_t)std::floor(offset_ + t * (1.0 + ppm_ * 1e-6)); }

  DuoClock &clock_;
  int id_;
  Channel &tx_;
  Channel &rx_;
  SensorModel m_;
  std::mt19937 rng_;
  std::normal_distribution<double> gauss_{0.0, 1.0};
  std::uniform_real_distribution<double> jitter_{0.0, 3.0};
  double offset_ = 0, ppm_ = 0;
};
