# Clock-Locked Tones ("Clock Spurs")

The capture chain (AtomS3 Lite + Unit Mini PDM / SPM1423) carries steady pure
tones at exact fractions of the sample rate. Which tones appear is re-chosen
every time the I2S channel starts. They start as an ultrasonic tone in the
mic's own output, which the ESP32-S3's hardware PDM-to-PCM filter folds into
the audio band (see [Cause](#cause)). The firmware removes them with a
periodic canceller. This page records what is known, what the mitigation
does, and how to re-measure.

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

## Cause

Measured October 2026 at 48 kHz. The mic's PDM output always carries a steady
ultrasonic tone, and the ESP32-S3's hardware PDM-to-PCM filter doesn't reject
it well enough. The filter folds it to 48 kHz − f, which puts it in the audio
band.

### What was measured

The test used temporary diagnostic firmware. It added an endpoint that captured
the mic's raw 1-bit stream with the hardware filter bypassed
(`I2S_PDM_DATA_FMT_RAW`, PDM clock 3.072 MHz), plus a runtime canceller bypass.
That firmware is not in the tree. The raw stream was decoded on a PC. The
decoder was an exact integer CIC filter decimating by 64, which can't create
tones. Each capture was a fresh clock start. Normal hardware-filtered captures
were interleaved with them, with the canceller bypassed. Both paths were
analysed on identical 2907-sample (17 × 171) segments. They have the same
absolute scale: about −106 dBFS floor per bin near 5 kHz.

| | Hardware PDM2PCM path | Raw bitstream, decoded on the PC |
| --- | --- | --- |
| 3/19 or 1/9 × fs line | 10 of 10 starts, at −77 or −82 dBFS | 0 of 8 starts, at the floor (about −107 dBFS) |
| Ultrasonic tone | (filtered) | 14 of 14 captures: fclk/76 = 40.42 kHz (7 of 11 quiet starts) or fclk/72 = 42.67 kHz (4 of 11) |

The ultrasonic tone accounts for every property listed above:

- **Exact fractions of fs.** fs = fclk/64, so the folded tone sits at
  fs × (1 − 64/N). N = 76 gives 3/19 × fs (7578.9 Hz, mode A), and N = 72 gives
  1/9 × fs (5333.3 Hz, mode B).
- **The companion lines.** The tone has a 3rd harmonic, 8.4–9.5 dB down (121.26
  or 128 kHz). It folds to 9/19 × fs (= 27/57, a mode A companion) or 1/3 × fs.
  The 2nd harmonic is 38–47 dB down. Strong odd harmonics with weak even ones
  fit a square-ish wave.
- **Re-chosen at each I2S start.** Each clock start settles on N = 72 or N = 76,
  at about the same split as modes A and B.
- **Same amplitude every time.** The tone was −43.9 dBFS in every capture, in
  both modes. The ESP32 filter passes it at −77 or −82 dBFS, which is only
  33–38 dB of rejection. For comparison, a plain 5th-order CIC decimator gives
  about 74 dB at 40 kHz. Espressif doesn't document the filter's stopband. It is
  adequate for a mic whose ultrasonic output is just its own noise, but not for
  this one.
- **Independent of the probe result.** Both PDM slots (clock edges) carry the
  identical tone.

### Noise test

White noise from a speaker raised the in-band level at the mic by about 35 dB,
to −35 dBFS rms. The ultrasonic tone stayed at exactly −43.9 dBFS and the same
frequency. A sigma-delta idle tone would have broken up under that much input.
So this is a fixed periodic signal added inside the mic module. Likely an
on-chip circuit is clocked from a divided PDM clock, but the datasheet doesn't
say, and that part is inference.

### PDM clock sweep

Raw captures were taken at seven PDM clocks, with 2–5 clock starts each. Some
HTTP fetches failed, so some rates have fewer starts. The hardware PCM path
runs the PDM clock at 64 × fs, so four of these clocks are the ones used at
16, 24, 32 and 48 kHz.

| PDM clock (rate) | N, chosen per start | Tone | Folds to (hardware path) |
| --- | --- | --- | --- |
| 1.024 MHz (16 kHz) | 24 or 28 | 42.67 / 36.57 kHz | 1/3 or 2/7 × fs |
| 1.536 MHz (24 kHz) | 40 or 44 | 38.40 / 34.91 kHz | 2/5 or 5/11 × fs |
| 2.048 MHz (32 kHz) | 48 or 56 | 42.67 / 36.57 kHz | 1/3 or 1/7 × fs |
| 2.4 MHz | 56 or 64 | 42.86 / 37.51 kHz | — |
| 2.8 MHz | 64 | 43.75 kHz | — |
| 3.072 MHz (48 kHz) | 72 or 76 | 42.67 / 40.43 kHz | 1/9 or 3/19 × fs |
| 3.2 MHz | 72 or 80 | 44.44 / 40.00 kHz | — |

- **Every line in the [Measurements](#measurements) table is predicted.** At
  each supported rate, the two values of N give exactly the two main lines seen
  there. The one exception is 16/33 at 24 kHz, which is still unexplained.
- **The mic aims for a frequency.** N changes with the clock so the tone always
  lands at 35–44 kHz. A fixed divider would not do that.
- **N is always a multiple of 4.** The tone is fclk/4 divided by a whole number
  M. At startup M lands on one of two values, sometimes adjacent and sometimes
  two apart. There aren't enough starts to work out the selection rule.
- **The level is −43 to −45 dBFS at every clock.**
- **Unrelated lines.** Weak lines at 24.50 and 24.80 kHz, about −84 dBFS, were
  present at every clock. They stay at the same frequency whatever the clock,
  so they don't come from it. At 48 kHz they alias to about 23.2–23.5 kHz, which
  is outside BirdNET's range.

### Ruled out

- **Mic modulator idle tones** (the earlier leading theory). They would change
  with acoustic input.
- **The ESP32-S3 fractional clock divider.** Raw mode uses a different MCLK
  divider and still shows the same 72 and 76 periods.
- **Limit cycles or arithmetic in the ESP32 PDM2PCM filter.** Decoding the raw
  bits outside the chip leaves the ultrasonic tone and no in-band line, so no
  in-band line comes out of the filter's internals.
- **A fixed external interferer.** It would land at different fractions of fs
  at each rate.
- **The mic's supply regulator.** The Unit Mini PDM makes its 3.3 V with an
  HT7533, a linear regulator. It has no clock, so it can't produce a tone that is
  a whole number of PDM clocks long and re-chosen at each start. It could still
  change the tone's level. A low-quiescent-current regulator like this one
  typically has a slow control loop, so current pulses from the mic or ripple
  on the 5 V input would reach the 3.3 V rail at tens of kHz with little
  attenuation.

### Possible sources inside the mic

The evidence points to something in the mic held just above the audio band at
a fixed level, timed from the PDM clock. Ranked by fit:

1. **Deliberate out-of-band dither.** Some sigma-delta designs inject a
   constant ultrasonic signal to stop idle tones from forming, and expect the
   decimator to remove it. That fits every observation.
2. **Charge-pump or bias-generator ripple.** The MEMS bias voltage is made by an
   on-chip charge pump. If the pump runs from a divided PDM clock, its ripple
   could appear in the output at a constant level. Charge pumps normally run
   much faster than 35–44 kHz, so this fits less well.
3. **Other on-chip clocked logic.** An example is clock-rate detection whose
   activity couples into the front end. Choosing between two divider values at
   each start looks like a startup measurement of the clock against an internal
   oscillator.

**A supply check would separate 1 from 2.** With an oscilloscope or a 192 kHz
audio interface, look at the Unit Mini PDM's 3.3 V rail at the mic's decoupling
capacitor, or better, at the current through a 10 Ω shunt in the Grove 5 V
line. Then check whether a 35–44 kHz line on the supply moves with the mode:
- At 48 kHz, a 7579 Hz line in the stream (canceller bypassed) means N = 76, so
  the tone is at 40.43 kHz. A 5333 Hz line means N = 72, so it is at 42.67 kHz.
- `reset_i2s` re-chooses the mode, so repeat it to see the supply line jump
  between the two.
- Changing the sample rate moves the tone as in the sweep table.

How to read the result:
- **A line that tracks the mode and the rate** means on-chip switching, so
  candidate 2.
- **No line** fits dither, but it could also be a well-decoupled internal pump.

Controls:
- Compare the ESP32's 3.3 V rail.
- Stop the clock, which puts the mic to sleep. The line should disappear.

To rule out the HT7533 completely, power the mic from a clean external 3.3 V
supply. If the tone stays at −44 dBFS with the same N, the regulator plays no
part.

### Not yet measured

- **The 1 kHz grid** is too weak to see in 61 ms raw captures. Its source is
  still unknown.
- **A second Unit Mini PDM or another mic model.** Without one, it isn't known
  whether this is a design property of the SPM1423 or a fault in this unit.

### Consequences for mitigation

The tone is already in the mic's output, so it can't be removed by changing
how the ESP32 is configured. There are two ways to stop it at the source:

- Decimate the raw PDM in firmware with a filter that rejects 28 kHz and above
  by at least 50 dB. The tone is at 35 kHz or above at every rate, well clear of
  the audio band.
- Use a different mic.

Otherwise the periodic canceller below remains the right tool.

### Raw PDM format (ESP32-S3, ESP-IDF 5.5)

- `sample_rate_hz` is the PDM clock.
- Even with `I2S_SLOT_MODE_MONO`, DMA delivers two interleaved 16-bit slots,
  one per clock edge. At 3.072 MHz that is 768 kB/s.
- Each slot is a little-endian uint16 with the bits MSB-first in time.
- An exact CIC filter decimating by 64 gives the same dBFS scale as the
  hardware path.
- With 4 DMA descriptors the largest contiguous heap block is about 48 kB,
  which is about 61 ms of audio per capture.

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
