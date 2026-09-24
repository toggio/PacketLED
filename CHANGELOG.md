# Changelog

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
