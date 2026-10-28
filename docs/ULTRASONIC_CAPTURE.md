# Ultrasonic Capture for Bat Detection (Design Study)

This page looks at replacing the Unit Mini PDM mic with an analog microphone and
an I2S ADC, so the stream can carry bat echolocation calls for BirdNET-Go's bat
models. Nothing here is built yet. It records what was learned on 2026-10-20 and
what still has to be measured.

## Goal

Stream at 192 kHz or higher over RTSP to BirdNET-Go, and have the BattyBirdNET
classifiers detect bats.

- BirdNET-Go supports bat detection with the BattyBirdNET regional models. It
  warns when a bat model is assigned to a source captured below 192 kHz. Its
  feature list gives up to 256 kHz; a 2026 nightly raised this to 384 kHz on
  Linux.
- BattyBirdNET itself works at 256 kHz.
- The bat-support notes describe USB microphones. **Not yet verified:** that the
  RTSP input accepts a 192 kHz L16 stream. Check this first with an ffmpeg test
  source before building hardware.

### Is 192 kHz enough?

At 192 kHz the usable band reaches about 85–90 kHz once the ADC's anti-alias
filter rolls off. That covers most North American species: big brown bats
around 25–30 kHz, red and hoary bats around 20–40 kHz, and Myotis sweeps
between about 40 and 80 kHz. European horseshoe bats call above 100 kHz and
need 256 kHz or more. Target 256 kHz if WiFi allows it.

## ESP32-S3 capability

The ESP32-S3 can capture at 192 kHz and above through its standard I2S
interface.

- **I2S speed:** 192 kHz with 32-bit slots in stereo needs a 12.288 MHz bit
  clock, well within the S3's I2S limits in either master or slave mode.
- **Clock source:** the S3 has no audio PLL. As I2S master it divides 160 MHz
  with a fractional divider, which adds jitter and spurs; see
  [CLOCK_SPURS.md](CLOCK_SPURS.md) for how much trouble clock artifacts have
  already caused. **Run the ADC as I2S clock master** from its own 24.576 MHz
  crystal, with the ESP32 as I2S slave.
- **CPU:** 240 MHz at 192 kHz leaves about 1,250 cycles per sample, plenty for
  the high-pass filter, gain and one EQ biquad.
- **RAM:** the AtomS3 Lite has no PSRAM and every buffer grows 4×. Workable,
  but DMA and queue depths need tuning.
- **Pins:** BCLK, WS and DIN, plus SDA and SCL for ADC configuration: 5 pins.
  G1/G2 on the Grove port plus G5–G8 and G38/G39 on the header are enough.

### Firmware changes

- Replace the `i2s_pdm` RX setup with `i2s_std` RX in slave mode.
- Extend `SUPPORTED_PDM_SAMPLE_RATES` (currently 16/24/32/48 kHz) to 192 and
  256 kHz.
- The clock-spur canceller is specific to the PDM path. Bypass it for the ADC,
  or re-survey the new hardware with `scripts/find_locked_tones.py`.
- Add a peaking-EQ biquad to flatten the mic resonance (see
  [Microphone](#microphone-infineon-im73a135)).

## Biggest risk: WiFi throughput

| Format | Payload |
| --- | --- |
| 48 kHz, 16-bit mono (today) | 0.77 Mbit/s |
| 192 kHz, 16-bit mono | 3.1 Mbit/s |
| 192 kHz, 24-bit mono | 4.6 Mbit/s |
| 256 kHz, 16-bit mono | 4.1 Mbit/s |

Stay with 16-bit L16. The enclosure install measured RSSI around −72 dBm, where
the S3's real TCP throughput can fall to a few Mbit/s and the stream will hit
"Write timeout" stalls. Measure throughput at the install site and plan on a
better antenna position or a closer access point.

## ADC options

| ADC | Max rate | Mic bias / gain | Notes |
| --- | --- | --- | --- |
| **TI TLV320ADC3120** (2-ch), ADC3140 (4-ch), ADC5120 (higher dynamic range) | 768 kHz | Programmable mic bias, built-in gain, high-pass filter, biquads | **Best fit.** Connects a mic directly, I2C control, can derive its clock from BCLK. QFN package and few hobby breakouts: use TI's eval board or a small custom PCB. |
| TI PCM1863 / PCM1865 | 192 kHz | Gain up to about +32 dB | I2C/SPI control. 192 kHz is the ceiling. |
| TI PCM1820 / PCM1840 | 192 kHz | Line-level input | Pin-configured, no I2C. Needs an external mic preamp. |
| Cirrus WM8782 | 192 kHz | None | Cheap, pin-configured, breakouts exist. Needs a 24.576 MHz master clock and a preamp. |

Not suitable: PCM1808 and PCM1802 (96 kHz max), ES7210 and CS5343 (below
192 kHz).

### Alternative: ultrasonic PDM mic

Knowles makes ultrasonic PDM mics (SPH0641LU4H-1, to about 80 kHz with a
3–4.8 MHz clock). The S3's hardware PDM-to-PCM filter can't produce 192 kHz, but
the firmware already captures raw PDM, so software decimation is possible. It
costs significant CPU and keeps the ESP32 as the clock source, so the analog
mic and ADC as clock master is preferred.

## Microphone: Infineon IM73A135

An analog differential MEMS mic, IP57-rated by Infineon's Sealed Dual Membrane
design: water and dust can't get between the diaphragm and backplate. SNR
73 dB, AOP 135 dB SPL, 1.52–3.0 V supply.

The datasheet's Figure 3, "Typical free field ultrasonic response", shows it is
**more** sensitive to ultrasound than to audible sound:

| Range | Response vs 1 kHz | Meaning |
| --- | --- | --- |
| 0–20 kHz | 0 to +4 dB | Flat audio band |
| ~34 kHz | **+26.5 dB peak** | Resonance of the package's sound port and internal air space |
| 45–80 kHz | +12 to +19 dB | Wide usable plateau, second hump near 76 kHz |
| > 80 kHz | Not published | Falling past 76 kHz; probably usable toward 96 kHz, untested |

Other candidates: Knowles SiSonic analog ultrasonic MEMS (e.g.
SPU0410LR5H-QB, rated to about 80 kHz, with a similar 25–35 kHz peak; check
stock) and the Knowles FG-series electret (FG-23329 family), which needs a
JFET-style bias. Ordinary audio MEMS mics fall off after a 25–30 kHz resonance
and aren't suitable.

### Design consequences

1. **Equalize the 34 kHz peak.** It stands about 10–13 dB above the 45–80 kHz
   plateau, where many common species call. BattyBirdNET was trained on
   flatter detector mics, so leaving it in could shift confidence scores.
   Start with a peaking EQ at about 34 kHz, −12 dB, Q about 8–10, then fit the
   values to a measurement of the actual assembly, not the "typical" curve.
2. **Set ADC gain from ultrasonic test signals.** Ultrasound near 34 kHz
   reaches the ADC 26 dB hotter than audible sound at the same loudness. Gain
   set from birdsong will let close bats and insects clip. The extra
   sensitivity does lift bat calls above the ADC's own noise.
3. **Expect content up to Nyquist.** Sensitivity is still +17 dB at 80 kHz. At
   192 kHz the ADC's anti-alias filter (starting about 86–90 kHz) handles it;
   at 256 kHz more of 80–100 kHz gets through, which helps.
4. **Check the port and PCB stack-up.** The part is believed to be
   bottom-port; confirm in the package drawing. If so, the PCB hole adds to
   the sound path and will move the 34 kHz resonance lower and make it
   sharper. Use a thin board (0.8 mm or flex) with the datasheet's port-hole
   diameter, and make the board's back face the exposed outer surface.
5. **IP57 doesn't keep the port clear.** A drop in the port hole still blocks
   ultrasound until it dries. Mesh, orientation and a rain hood still matter.

## Weatherproofing

At 100 kHz the wavelength is about 3.4 mm, so millimetre-scale features in
front of the mic act as resonators or filters.

- **Port tube resonance.** A hole through the enclosure wall with the mic at
  its inner end resonates at roughly c/4L: a 1.6–2 mm wall puts a resonance at
  about 43–54 kHz, inside the bat band. The mic port must sit flush with the
  outside, with almost no tube.
- **Membrane loss.** Cover films block more as frequency rises, about 6 dB per
  octave. Mass-law estimates for a loose film:

  | Cover | Loss at 50 kHz | Loss at 100 kHz |
  | --- | --- | --- |
  | 10 µm cling film | ~11 dB | ~17 dB |
  | Light ePTFE vent (Gore-type) | ~6 dB | ~12 dB |

  Fine for birdsong, costly for bats. Avoid membranes.
- **Reflections off the box.** Flat surfaces near the mic cause comb-filter
  notches. Mount the mic at an edge or the tip of a short stalk.

### Off-the-shelf reference: Wildlife Acoustics SMM-U2

The standard weatherproof ultrasonic mic for SM4BAT recorders. It survives
direct rain and submersion without permanent damage. It uses a stainless-steel
mesh instead of a membrane; water in the mesh blocks ultrasound until it
evaporates. Wildlife Acoustics recommends horizontal mounting with an
acoustically transparent foam windscreen. It's built for their recorders, so
the connector and powering would need checking before wiring it to our ADC,
and it isn't cheap. Its design approach is the model for the DIY build below.

### Proposed DIY assembly

1. Mic on a small separate PCB outside the main enclosure, at the end of a
   short stalk or cable, angled about 10–20° below horizontal.
2. Conformal coating on everything except the port (mask it while spraying).
3. Hydrophobic stainless mesh and a loose open-cell foam windscreen instead of
   a membrane.
4. A small rain hood or drip edge above the mic, a few centimetres away so it
   doesn't add reflections.
5. Mic board as a cheap plug-in part, replaced each season if needed.

## Bench tests

Sources: a piezo super-tweeter playing an ultrasonic sweep, a 40 kHz
transducer, or jangled keys for a quick check. Capture at 192 kHz and compare
spectra.

1. **RTSP acceptance:** confirm BirdNET-Go ingests a 192 kHz (and 256 kHz) L16
   RTSP stream from an ffmpeg test source.
2. **WiFi throughput** at the install site.
3. **Bare mic board:** how the PCB port changes the curve vs. Figure 3.
4. **Finished assembly** with mesh, foam and hood: the difference from test 3
   is what the weatherproofing costs, and this curve gives the EQ values for
   the firmware.

## Sources

- [BirdNET-Go nightly-20260511-414: bat detection, 384 kHz capture, 192 kHz recommendation](https://newreleases.io/project/github/tphakala/birdnet-go/release/nightly-20260511-414)
- [BirdNET-Go 20261014 release](https://newreleases.io/project/github/tphakala/birdnet-go/release/20261014)
- [BirdNET-Go project listing (up to 256 kHz)](https://ost.ecosyste.ms/projects/123355)
- [TI TLV320ADC3120 product page](https://www.ti.com/product/TLV320ADC3120)
- [Infineon IM73A135 product page](https://new-origin.infineon.com/cms/en/product/sensor/mems-microphones/mems-microphones-for-consumer/im73a135)
- [IM73A135 datasheet (Farnell copy)](https://www.farnell.com/datasheets/3629245.pdf), Figure 3
- [Wildlife Acoustics: About the SMM-U2 Microphone's Weatherproofing](https://answers.wildlifeacoustics.com/r/en-US/About-the-SMM-U2-Microphone-s-Weatherproofing)
- [Wildlife Acoustics FAQ: Can the SMM-U2 be used in the rain?](https://wildlifeacoustics.com/resources/faqs/can-the-smm-u2-be-used-in-the-rain)
