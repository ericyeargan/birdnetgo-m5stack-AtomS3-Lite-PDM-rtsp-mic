# Clock-Locked Tones ("Clock Spurs")

The capture chain (AtomS3 Lite + Unit Mini PDM / SPM1423) carries steady pure
tones at exact fractions of the sample rate. Which tones appear is re-chosen
every time the I2S channel starts. The firmware removes them with a periodic
canceller. This page records what is known, what the mitigation does, and how
to re-measure.

## Symptom

A constant whistle in the stream, for example 5333 Hz or 7579 Hz at 48 kHz.
It sits 35–60 dB above the (very low) noise floor of a quiet scene, so it is
plainly audible on headphones and visible in any spectrogram. After a reboot or
an I2S restart it may be gone, or replaced by a tone at a different frequency.

History: commit `265aebe` added a fixed two-stage notch at 3/19 × fs
(7578.95 Hz). A different tone reappeared after a reboot because the tone had
moved to 1/9 × fs, outside the notch. The notch was replaced by the canceller
described below in `4b28162`.

## Measurements

All captures were taken from the RTSP stream with ffmpeg and analysed offline.
The modes were surveyed across 8–13 I2S starts per rate. At 48 kHz the 3/19
line was measured before the notch existed; the other 48 kHz lines were
measured with the notch in place. At other rates nothing was filtering the
tones. "dB" is the level above the local noise floor; "counts" is the
amplitude in 16-bit PCM counts.

| Rate | Mode (one per I2S start) | Lines (× fs) | Frequencies | Level |
| --- | --- | --- | --- | --- |
| 48 kHz | A (8 of 13 starts; 7 of 10 in a later run) | 9/57 = 3/19, plus 16/57, 25/57, 27/57 | 7578.9; 13473.7, 21052.6, 22736.8 Hz | 3/19: 55–62 dB; others 13–21 dB |
| 48 kHz | B (5 of 13; 3 of 10) | 1/9 | 5333.3 Hz | 46–55 dB, 2.5 counts (−82 dBFS) |
| 48 kHz | every start | 1/3 | 16000 Hz | 13–18 dB |
| 32 kHz | 5 of 8 starts | 1/3 | 10666.7 Hz | 44–46 dB, 0.66 counts |
| 32 kHz | 2 of 8 | 1/7 | 4571.4 Hz | 36–44 dB, 0.66 counts |
| 32 kHz | 1 of 8 | none | — | — |
| 24 kHz | 5 of 8 | 2/5 | 9600 Hz | 34–37 dB, 0.30 counts |
| 24 kHz | 3 of 8 | 5/11 with 16/33 | 10909.1, 11636.4 Hz | 38–40 dB, 0.45 counts |
| 16 kHz | per start | 2/7 or 1/3 | 4571.4 or 5333.3 Hz | 16–24 dB, 0.05–0.07 counts |
| all | every start | exact 1 kHz multiples | e.g. 10, 14, 18, 20 kHz | 12–18 dB |

Properties that hold for every line measured:

- **Locked to the sample count.** The 1/9 line measured 0.00 ppm from exact and
  held its phase within about 1° over 25 s. The 3/19 line matched to 0.13 ppm.
- **Pure.** There is no 2nd harmonic. Within a mode, the amplitude is the same on
  every start.
- **Chosen when the I2S clock starts.** A plain `/api/action/reset_i2s` re-rolls
  the mode, with no reboot needed.
- **Independent of the probe result.** The start-up probe picks a PDM slot and
  clock edge, but the same slot/edge produced different modes.
- **Not produced by firmware DSP.** The firmware filters run in float32, which
  can't produce a self-sustaining tone like this.

## Cause (not confirmed)

The tones come from the capture hardware, not from sound, and not from the
firmware. Which chip generates them is still open.

- **Most likely: sigma-delta idle tones in the mic.** A 1-bit PDM modulator with
  near-silent input can fall into a repeating pattern locked to its clock. That
  fits exact fractions of fs, a pattern picked when the clock starts, and
  independence from the sampling edge.
- **The always-present 1/3 × fs line at 48 kHz has a likely explanation.** The
  ESP32-S3 makes the 48 kHz PDM clock as 160 MHz ÷ (6 + 49/96). That division
  isn't a whole number, so the clock period jitters in a pattern that repeats
  every 96 MCLK cycles, which is exactly 3 samples. That predicts a fixed line at
  fs/3. No such line was seen at 24 kHz, where the same reasoning also predicts
  one.
- **Disproven: the mic locks onto that 3-sample divider pattern.** That idea
  predicted every period would be a multiple of 3. The 48 kHz periods (9 and 57)
  fit, but 24 kHz (5 and 33) and 32 kHz (7) don't.
- **Ruled out: a fixed external interferer.** It would land at different
  fractions of fs at each rate, not at small-denominator fractions at every rate.

Two hardware tests would settle where the tones come from:

1. Hold loud broadband noise (hiss, running water) at the mic while a strong mode
   is active. If the line smears or vanishes, it's the mic's modulator; if it
   stays put, it's interference added to the signal.
2. Disconnect or ground the PDM DATA line. If the lines persist, the source is
   on the ESP32 side.

A web search (October 2026) found no reports of this behaviour for the SPM1423,
the Unit Mini PDM, or ESP32-S3 PDM capture. Most users run this mic at 16 kHz,
where the lines are under 0.1 LSB.

## Mitigation: periodic canceller

A line at p/q × fs repeats exactly every q samples, and so every N samples
whenever q divides N. The canceller keeps an exponential average of the input
at each position modulo N and subtracts it:

```text
avg[phase] += (x - avg[phase]) * rate      // rate = N / (tau * fs)
y = x - avg[phase]
phase = (phase + 1) % N
```

Sound never repeats to the exact sample over seconds, so it averages away.
Only clock-locked components build up. The result is a notch at every
k × fs/N, each about 1/(π·τ) ≈ 0.16 Hz wide at τ = 2 s. At N = 171 that removes
about 14 Hz of the 24 kHz band in total, which is far too little for BirdNET to
notice.

Implementation (`src/esp32_rtsp_mic_birdnetgo.ino`, search for `clock-spur`):

- **Periods.** `CLOCK_SPUR_CANCEL_PERIODS` maps each rate to the lcm of the
  denominators seen there:

  | Rate | Period N | Covers |
  | --- | --- | --- |
  | 48 kHz | 171 = lcm(9, 57) | 1/9, n/57 (incl. 3/19), 1/3 |
  | 32 kHz | 21 | 1/3, 1/7 |
  | 24 kHz | 165 = lcm(5, 33) | 2/5, 5/11, 16/33 |
  | 16 kHz | 21 | 2/7, 1/3 |

  Rates without an entry bypass the canceller. A `static_assert` keeps every
  period within the shared 171-float buffer.
- **Position in the chain.** After the high-pass filter, before the noise
  suppressor, gain, limiter and AGC, so none of those react to the tones.
  It runs whether or not the high-pass filter is enabled.
- **Settings handoff.** The period and averaging rate are calculated in
  `updateHighpassCoeffs()` and handed to Core 1 under `hpfMux` with the HPF
  coefficients. Core 1 resets its averages when the period or rate changes, and
  when the input switches into or out of unsigned-PDM normalisation, because the
  signal scale changes then.
- **Dropped samples.** The position count has to advance with every captured
  sample. When Core 1 stalls (for example on a socket write while a client
  reconnects), the I2S driver silently discards whole DMA buffers. The
  `on_recv_q_ovf` callback adds each dropped buffer's size to
  `i2sDmaDroppedSamples`, and Core 1 advances the position by that amount
  before the next read. Without this, the tones came back for about 6 s after
  every overflow. The total is shown as `i2s_dma_dropped_samples` in
  `/api/audio_status`.
- **Cost.** 684 bytes of static RAM and one multiply-add per sample.

Measured on the device (`4b28162`), 4–10 I2S starts per rate: 10 s after each
start, every surveyed line was within −10.6 to +6.6 dB of the noise floor (the
floor's own scatter is about ±5 dB). Before the canceller the lines were 16–60
dB above it. Lock held through overflows of up to 56,832 dropped samples.

## Limitations

- **Settling time.** The tones are audible for about 8 s after each I2S start
  while the averages settle.
- **Unseen modes.** A mode whose denominator doesn't divide the period passes
  through. Each period is based on 8–13 starts per rate, so rarer modes may
  exist. The 32 kHz 1/7 mode was in the survey but didn't occur during
  verification.
- **The 1 kHz grid isn't covered.** Removing it would need N to be a multiple of
  fs/1000 as well (2736 at 48 kHz: about 11 KB of RAM and about 220 Hz of total
  notch width).
- **Lines that drift aren't covered.** Only tones exactly locked to the sample
  clock are removed. Mains hum (for example 240 Hz) and other drifting tones get
  through.

## Re-measuring and adding a period

The canceller hides the lines it covers, so survey at a rate with no table
entry, or temporarily remove that rate's entry. Then:

```bash
# Re-roll the mode (state-changing endpoints need this header)
curl -X POST -H "X-Requested-With: birdnetgo-webui" http://atoms3mic.local/api/action/reset_i2s

# Capture 30 s once the stream is back
ffmpeg -rtsp_transport tcp -i rtsp://atoms3mic.local:8554/audio -t 30 -c:a pcm_s16le capture.wav

# List the lines sitting exactly on p/q * fs (needs numpy and scipy)
python3 scripts/find_locked_tones.py capture.wav
```

Repeat for at least 8 starts. To change the sample rate, POST `key=rate` and
`value=<Hz>` to `/api/set` with the same header. Set N to the lcm of every
denominator seen (and keep it at or below `CLOCK_SPUR_CANCEL_MAX_PERIOD`, or
raise that). Ignore large-denominator matches (above about 100): they are
usually harmonics of real sounds that happen to land near a fraction.

To check a new period, capture straight after a restart. Run the script on the
part from 10 s onward (trim it with `ffmpeg -ss 10`); the covered lines should
be gone.
