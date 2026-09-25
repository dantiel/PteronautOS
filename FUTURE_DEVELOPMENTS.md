# PteronautOS — Future Developments

> *Perfected since the Mesozoic. The pterosaur never hurried — it flew.*

PteronautOS is the only ornithopter firmware that should ever be needed. What
follows is not a schedule but a votive list — capabilities still awaiting their
wings, each a self-contained idea waiting for a contributor.

## Flight

- **Phone control without a handset** — fly directly from a phone over WiFi/UDP.
  The onboard gyro closes the inner loop, so a jittery link is tolerable.
- **DIY smartphone TX module** — a small ESP32 + SX1280 board re-emits phone
  input as CRSF over 2.4 GHz, keeping ELRS range and latency without a pricey
  handset.
- **Autopilot setpoints** — send attitude, flap-rate, or heading instead of raw
  stick; the gyro does the fast work.

## Sensing & navigation

- **GPS** — position hold, return-to-home, geofence, and flight logging.
- **Barometer** — barometric altitude for altitude hold and gentle climb/descent.
- **Richer telemetry** — battery, altitude, position, IMU attitude, and link
  quality streamed back to the radio or the phone app.

## Motion & behaviour

- **Preprogrammed flight patterns** — scripted manoeuvres (figure-eights, circles,
  waypoint routes) from onboard sequences.
- **Smooth profile transitions** — interpolate between flight-profile values so
  wing kinematics morph gracefully instead of stepping.

---

> *A summoning, not a queue.* If you'd like to contribute, these are solutions
> awaiting implementation. Pick one, and it ceases to be "future".
