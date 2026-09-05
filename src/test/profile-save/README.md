# Flight-profile save regression checks

From the repository root:

```sh
node --test src/html/scripts/test-flight-profiles.mjs
clang++ -std=c++17 -Wall -Wextra -Werror -I src/test/profile-save \
  src/test/profile-save/test-writer.cpp -o /tmp/ptero-test-profile-writer
/tmp/ptero-test-profile-writer
clang++ -std=c++17 -Wall -Wextra -Werror \
  src/test/profile-save/test-throttle-frequency.cpp \
  -o /tmp/ptero-test-throttle-frequency
/tmp/ptero-test-throttle-frequency
clang++ -std=c++17 -Wall -Wextra -Werror \
  src/test/profile-save/test-ferocity-shape.cpp \
  -o /tmp/ptero-test-ferocity-shape
/tmp/ptero-test-ferocity-shape
```

The JS tests compile and execute the actual CoffeeScript panel with mocked
network responses. The C++ tests exercise the production `PteroConfigWriter`
with an in-memory filesystem: open failure, short write, corruption/truncation
on close, read/reopen failure, and rename failure must all retain the old file.
The test filesystem is not a replacement for power-cut testing on LittleFS.

## Receiver acceptance check

With servos disconnected and WebUI channel override disabled:

1. Save distinct values in slots 0, 1, and 2. Switch the editor between them;
   the values must stay saved, independently of the TX-active slot.
2. Change a second field in each slot. Reload the page and check that the first
   field was not reverted by a stale browser snapshot.
3. Move CH7 low/middle/high and check the active profile number and parameters.
4. Power-cycle into normal RX mode without first entering WiFi. Confirm the
   TX-selected profile uses the saved values.
5. With a test filesystem/device, interrupt a save before its rename. The live
   file should remain the previous configuration; a leftover `.tmp` is ignored.

HTTP success now requires verified file replacement. A failed save returns
`saved: false` and explicitly warns that RAM settings may already have changed.
The editor retains failed drafts per slot (including after switching profiles)
for retry via **Save Configuration**; only confirmed saves update its saved cache.
Backup imports use the same protected write path, but imports of other receiver
settings are not an all-or-nothing transaction.

The throttle-frequency test verifies the new per-profile coupling endpoints,
continuous interpolation, input clamps, and monotonic full-coupling response.
The ferocity-shape test verifies the cosine-compatible zero, continuous
plateau-to-pyramidal mixing, rounded reversal, and asymmetric anticipation.
