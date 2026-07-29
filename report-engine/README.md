# ggw_report — 6GGW / NetSwitch report engine

One self-contained binary. Run it on any target box and it measures the machine and
writes a report you can open on a phone or print to PDF. This is the tool you set up
to "generate this data" — it produces the real **MFLOPS** figure for that machine
plus a full hardware / OS / compatibility inventory. No install, no dependencies.

## Build

```bash
# Linux
g++ -std=c++17 -O2 -march=native ggw_report.cpp -o ggw_report

# Windows (static .exe, no redist needed)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_report.cpp -o ggw_report.exe -static
```

## Run

```bash
./ggw_report                 # measure, write ggw_report.html, print a text summary
./ggw_report --n 768         # larger matmul (steadier MFLOPS on fast boxes)
./ggw_report --out sys.html  # choose the output filename
```

Then open `ggw_report.html` in any browser. To make a PDF: open it and print to PDF
(Ctrl/Cmd+P → Save as PDF), or on a server:

```bash
chromium --headless --print-to-pdf=report.pdf ggw_report.html
# or
wkhtmltopdf ggw_report.html report.pdf
```

## What it measures

| Section | Content |
|---------|---------|
| Compute | MFLOPS two ways: dense N×N matmul (2·N³ FLOPs) and sustained FMA (2 FLOPs each), with reproducibility checksums |
| Processor & memory | CPU brand (cpuid), logical cores, RAM total/available |
| Disk | total / free on the report volume |
| OS & compatibility | distro+version (Linux) or true Windows build (RtlGetVersion), verdict against the supported matrix |

## Notes

- MFLOPS is **measured**, so it varies a little run to run and scales with the box —
  that's the point: quote the number the target machine prints, not a fixed value.
- Read MFLOPS from a **native** run. Under an emulator (e.g. wine) the FMA path is
  slow and not representative.
- The work is checksummed so the optimiser can't delete it; the same binary run twice
  on the same machine reproduces the checksums.
- Supported: RHEL / CentOS / Rocky / Alma / Fedora / Gentoo / Debian / Ubuntu, and
  Windows Server 2016–2025 Datacenter / Windows 10+.
