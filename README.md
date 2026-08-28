# OpenScan-Swabian

An [OpenScan](https://github.com/openscan-lsm/OpenScanLib) device module that
adapts a Swabian Instruments Time Tagger as a **detector** for laser-scanning
microscopy TCSPC (time-correlated single-photon counting) acquisitions,
usable from [Micro-Manager](https://micro-manager.org/) via OpenScan's LSM
device.

The module reads raw event tags from the Time Tagger (sync, photon, and line
clock channels), correlates photons against sync pulses, bins them per pixel
using a [libtcspc](https://github.com/marktsuchida/libtcspc) processing
pipeline, and delivers each frame to OpenScanLib as a conventional 16-bit
image.

## Repository layout

- `src/TimeTagger.cpp`, `src/TimeTaggerSettings.cpp`, `src/TimeTaggerPrivate.h`
  — the `OScDev_DeviceImpl` device module itself: device lifecycle
  (`Open`/`Close`/`Arm`/`Start`/`Stop`/`IsRunning`/`Wait`) and the settings
  described below.
- `src/EventPipeline.cpp`/`.h` — the libtcspc processing graph: decodes raw
  tags, pairs sync/photon detections, derives per-pixel timing windows from
  the line clock, bins photons into per-pixel histograms, and delivers
  frames.
- `src/fake_timetagger/` — a from-scratch, header-only fake of the vendor
  Time Tagger C++ SDK, used for `simulate=true` builds (see below). Its goal
  is to be a faithful stand-in: code written against it should build and run
  unmodified against the real SDK headers. It also synthesizes plausible
  sync/photon/line-clock activity so the pipeline has something to process
  without real hardware attached.
- `tools/dump_tags.py` — decodes a raw Time Tagger tag dump (the format
  written by the SDK's `Dump` measurement, and by this module's own "Save Raw
  Data" setting) into human-readable text.

## Building

Requires [Meson](https://mesonbuild.com/) (>=1.9.0) and Ninja, and an MSVC or
`clang-cl` toolchain (this project is Windows-only, matching the Time Tagger
SDK). From a Visual Studio developer command prompt (or after running
`vcvars64.bat`):

```
meson setup builddir
ninja -C builddir
```

This produces `OpenScanSwabian.osdev` (the OpenScan device module, loadable
by OpenScanLib/Micro-Manager).

### Simulate vs. real hardware

By default the module builds against a real Time Tagger and the vendor SDK.
This expects the vendor SDK installed at its default location,
`C:/Program Files/Swabian Instruments/Time Tagger/driver`.

To build against the fake SDK in `src/fake_timetagger/` instead — no
hardware or vendor SDK installation needed, useful for developing and
testing the pipeline itself:

```
meson setup builddir -Dsimulate=true
ninja -C builddir
```

No source changes are needed to switch between the two — `#include
<TimeTagger.h>` resolves to one or the other purely via the include path
meson selects, based on the `simulate` option.

### Installing into Micro-Manager

Copy the built `OpenScanSwabian.osdev` into your Micro-Manager installation's
device adapter directory alongside the other OpenScan device modules
(`OpenScan.dll`, `OpenScan-NIDAQ.osdev`, etc.), then add the device
(`Swabian Time Tagger`) as a detector in your OpenScan `.cfg` configuration —
see the OpenScan/Micro-Manager documentation for how detector/scanner/clock
roles are assembled into an LSM device.

## Settings

These are exposed as standard MicroManager device properties (device
`Swabian Time Tagger`, prefixed with the model name in the property browser).

### Channels

The Time Tagger's raw channel numbers (positive = rising edge, negative =
falling edge) that this module expects each signal to be wired to:

- **Sync Channel** (default `2`) — the laser sync / reference clock input.
- **Photon Channel** (default `3`) — the detector (e.g. PMT) pulse input.
  Both edges of this channel are used: the module measures pulse width by
  pairing each rising detection with its matching falling detection.
- **Line Clock Channel** (default `1`) — a per-scan-line marker from the
  scanner. Used to derive per-pixel timing windows: each line-clock tick
  starts a run of `width` pixel windows, each `1e12 / pixelRate` picoseconds
  wide, where `width` and `pixelRate` come from the current OpenScan
  acquisition (ROI width, pixel rate), not from a setting here.

### Timing

- **Sync Delay** (ps, default `0`) — a fixed offset applied to sync
  detections before correlating them with photons (`delay()` ahead of the
  sync/photon pairing stage). Compensates for a known, fixed timing offset
  between the sync and photon paths (e.g. cable length differences).
- **Max Photon Pulse Width** (ps, default `100000`) — the maximum allowed
  separation between a photon channel's rising and falling edges for them to
  be treated as one pulse. Should comfortably exceed the detector's real
  pulse width, but stay short enough to avoid accidentally pairing across two
  separate pulses.
- **Max Diff Time** (ps, default `15000`) — the maximum allowed time between
  a sync tick and a photon detection for them to be correlated as "this
  photon resulted from this sync pulse." This is the parameter that actually
  bounds the histogram's meaningful time range (see "Histogram binning"
  below) — it should be set based on the laser's sync period and the
  fluorophore's expected decay time, **not** based on the pixel dwell time.
  Those are unrelated: pixel dwell time is about how long the scanner stays
  at one spatial position, while Max Diff Time is about how long after a
  single laser pulse a photon can plausibly arrive and still belong to that
  pulse. In a typical setup many sync pulses occur within one pixel's dwell
  time, each independently contributing photons to that pixel's histogram.

### Histogram binning

- **Histogram Bins** (default `256`) — the per-pixel TCSPC histogram has this many bins, covering difftimes `[0, Max Diff Time)`.

  > [!NOTE]
  > The bin width of the TCSPC histogram is derived as `ceil(Max Diff Time / Histogram Bins)`, so
  > the histogram's range fully covers **Max Diff Time** and avoids photon loss.
- **Cumulative** (default `false`) — if enabled, the per-pixel histogram
  accumulates across the whole acquisition instead of resetting every frame.
- **Save Histograms** (default `false`) — when enabled, the full per-pixel
  histogram (Histogram Bins bins/pixel) is additionally computed and
  written to `histogram_debug.bin` (next to the executable) once per frame,
  for offline inspection. This is off by default because computing and
  dumping it roughly doubles per-event processing cost and writes a large
  file (`width * height * Histogram Bins * 2` bytes) every frame — leave it
  off for normal/live acquisitions and enable it only when you need to
  inspect the underlying histogram data.

### Live image

Only the total photon count per pixel (summed across all histogram bins,
independent of Histogram Bins) is sent to OpenScanLib/Micro-Manager as the
live/displayed image — the frame callback contract only supports one 16-bit
sample per pixel. 

### File output

- **Save Raw Data** (default `false`) — when enabled, writes every raw tag
  received during the acquisition to a file in the vendor SDK's raw dump
  format (16-byte records; decode with `tools/dump_tags.py`).
- **File Name Prefix** (default `OpenScan-Swabian`) — path prefix for saved
  files; may include a directory (e.g. `C:/Data/myexperiment`) or be a bare
  name (relative to Micro-Manager's working directory). Each acquisition's
  actual filename is `<prefix>_NNNN.raw`, where `NNNN` (0000-9999) is the
  smallest index not already in use.

## Simulate mode details

When built with `-Dsimulate=true`, `src/fake_timetagger/` synthesizes
activity on three fixed channels so the pipeline has realistic-looking data
to process without hardware: a line clock, a sync channel, and gated Poisson
photon noise correlated to the sync channel. These are fixed, hardcoded
constants (`kSimulatedLineClockChannel`, `kSimulatedSyncChannel`,
`kSimulatedPhotonChannel`, and their rates, in
`src/fake_timetagger/include/TimeTagger.h`) rather than derived from the
current OpenScan acquisition's settings — the fake has no access to
OpenScan-specific concepts like pixel rate or ROI, by design (see that
file's comments for the full rationale). In practice this means the
simulated channel numbers and rates need to be kept roughly in step with the
Sync/Photon/Line Clock Channel settings and the acquisition's pixel rate for
the simulated data to look meaningful; see the comments alongside those
constants for the current assumptions and how to recompute them if you
change the acquisition's ROI or pixel rate.
