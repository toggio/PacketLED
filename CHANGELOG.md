# Changelog

## 1.1.0

- New examples: LightMeter, to see how much light arrives and choose LEDs,
  resistor and distance, and BeamBreak, a light barrier with synchronous
  detection.
- library.json for the PlatformIO registry.
- Fixed: two boards sending at the same time could both fail all their
  attempts, because the pause before a retry was spent deaf and was much
  shorter than a frame. A device now listens before transmitting, and during
  a longer random pause (20-219 ms) before each retry, so the side that
  starts again first is received by the other.
- Fixed: after one rejected frame, a program that prints for each frame could
  keep mistaking mains hum for SYNCs, rejecting one 5-6 ms "frame" after the
  other, even in the dark. A SYNC seen only in part is now accepted only within
  50 ms of the device's own transmission, which is where it is needed.
- Documentation: both LED pins must work as outputs (GPIO34-39 of the classic
  ESP32 cannot be used); begin() changes the attenuation of every ADC channel;
  endPacket() returning false means "not confirmed", not "not received";
  three attempts per packet, not three retransmissions. PJON AnalogSampling
  added to the credits as a close precedent.
- New host tests: both boards calling send() at the same time, and hum
  after the pauses of a program that prints for every frame.

## 1.0.2

- Fixed a regression in 1.0.1: when packets were sent back to back, about
  every other one needed a retransmission. A receiver getting back to listening
  after its ACK could see the next SYNC only in part, and 1.0.1 rejected it.
  A SYNC whose start was not seen is now accepted from 5 ms up.
- A device now waits 10 ms after the last frame it received before
  transmitting, so the other side sees the next SYNC from its start.
- If listening is interrupted while light is on, the end of that pulse is no
  longer trusted.
- Library description: tested up to 2.5 m with clear narrow-beam LEDs.

## 1.0.1

- Mains hum is no longer mistaken for a SYNC: the SYNC threshold stays above
  the measured resting noise, and pulses shorter than 17 ms are ignored.
  This introduced the back-to-back regression fixed in 1.0.2.
- New noiseLevel() method.
- If begin() fails, the previous settings are kept.

## 1.0.0

- First release.
