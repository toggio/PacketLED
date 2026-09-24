// Simulated optical channel for testing PacketLED on a PC.
//
// Each node has its own clock (offset + drift in ppm, 32-bit micros() with
// wrap-around). The light emitted by a node is a list of on/off events in true time.
//
// Sensor model, fitted to measurements on ESP32 (ADC at 0 dB):
//   reading = gain * (full-light time in the window, us) + ambient + mains flicker
//             - ADC dead zone (~100 LSB), + noise (~5 LSB + 3%), clipped at 4095
//   gain: ~4.5 LSB/us at 2 cm, ~1.1 at 3 cm, ~0.5 at 5 cm, ~0.35 at 6 cm
//   a measurement costs ~150 us on top of the window (pin setup, ~87 us ADC read)
#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "../../src/LedPhy.h"

struct Channel {
  struct Ev {
    double t;
    bool on;
  };
  std::vector<Ev> ev;  // sorted by time

  void clear() { ev.clear(); }
  void push(double t, bool on) {
    if (!ev.empty() && t < ev.back().t) t = ev.back().t;  // different on/off latencies
    ev.push_back({t, on});
  }
  // Time with the LED on within [a, b).
  double litTime(double a, double b) const {
    double sum = 0, t = a;
    bool s = false;
    size_t i = 0;
    for (; i < ev.size() && ev[i].t <= a; ++i) s = ev[i].on;
    for (; i < ev.size() && ev[i].t < b; ++i) {
      if (s) sum += ev[i].t - t;
      t = ev[i].t;
      s = ev[i].on;
    }
    if (s) sum += b - t;
    return sum;
  }
};

struct SensorModel {
  double gain = 1.1;           // LSB per us of full light
  double deadZone = 100;       // LSB lost near 0 V
  double ambient = 0;          // LSB per us of ambient light
  double hum = 0;              // mains flicker amplitude (LSB per us, 50 Hz)
  double pulses = 0;           // square-wave flicker (LSB per us while on)
  double pulseOnUs = 16000, pulsePeriodUs = 20000;
  double noiseLsb = 5, noiseRel = 0.03;
  double setupUs = 45;         // call -> start of integration
  double sampleDelayUs = 25;   // the ADC samples a little after the read call
  double readUs = 87;
  double offUs = 10;
  double ledOnLat = 24, ledOffLat = 12, ledCallUs = 26;
};

class SimPhy : public LedPhy {
 public:
  SimPhy(Channel &tx, Channel &rx, const SensorModel &m, uint32_t seed)
      : tx_(tx), rx_(rx), m_(m), rng_(seed) {}

  void setClock(double trueNow, double offsetUs, double ppm) {
    now_ = trueNow;
    offset_ = offsetUs;
    ppm_ = ppm;
  }
  double trueNow() const { return now_; }
  void setTrueNow(double t) { now_ = t; }
  // Extra ambient light from true time `at` on (a lamp switched on).
  void setAmbientStep(double at, double lsbPerUs) {
    stepAt_ = at;
    stepAmbient_ = lsbPerUs;
  }

  uint32_t micros() override {
    now_ += 0.2;  // cost of a polling loop iteration
    return local(now_);
  }
  void idle() override { now_ += 1000; }
  void delayMs(uint32_t ms) override { now_ += ms * 1000.0; }
  void ledOn() override {
    tx_.push(now_ + m_.ledOnLat, true);
    now_ += m_.ledCallUs;
  }
  void ledOff() override {
    tx_.push(now_ + m_.ledOffLat, false);
    now_ += m_.ledCallUs;
  }
  uint16_t integrate(uint32_t windowUs, uint32_t &startUs) override {
    const double start = now_ + m_.setupUs + jitter_(rng_);
    startUs = local(start);
    const double end = start + windowUs + m_.sampleDelayUs;
    const double span = end - start;
    const double humNow = m_.hum * span * 0.5 * (1 + std::sin(6.2831853 * 50e-6 * start));
    const double ambient = m_.ambient + (start >= stepAt_ ? stepAmbient_ : 0);
    double v = m_.gain * rx_.litTime(start, end) + ambient * span + humNow - m_.deadZone;
    if (m_.pulses > 0) v += m_.pulses * pulseOnTime(start, end);
    if (v < 0) v = 0;
    v += gauss_(rng_) * (m_.noiseLsb + m_.noiseRel * v);
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    now_ = start + windowUs + m_.readUs + m_.offUs;
    return (uint16_t)std::lround(v);
  }
  uint32_t random32() override { return (uint32_t)rng_(); }

 private:
  // Time within [a, b) during which the square-wave flicker is on.
  double pulseOnTime(double a, double b) const {
    double sum = 0;
    for (double k = std::floor(a / m_.pulsePeriodUs); k * m_.pulsePeriodUs < b; k += 1) {
      const double on0 = k * m_.pulsePeriodUs, on1 = on0 + m_.pulseOnUs;
      const double lo = on0 > a ? on0 : a, hi = on1 < b ? on1 : b;
      if (hi > lo) sum += hi - lo;
    }
    return sum;
  }

  uint32_t local(double t) const { return (uint32_t)(int64_t)std::floor(offset_ + t * (1.0 + ppm_ * 1e-6)); }

  Channel &tx_;
  Channel &rx_;
  SensorModel m_;
  std::mt19937 rng_;
  std::normal_distribution<double> gauss_{0.0, 1.0};
  std::uniform_real_distribution<double> jitter_{0.0, 3.0};
  double now_ = 0, offset_ = 0, ppm_ = 0;
  double stepAt_ = 1e300, stepAmbient_ = 0;
};
