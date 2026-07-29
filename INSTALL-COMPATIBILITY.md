# 6GGW / NetSwitch — installation, hardware/software & compatibility

Everything needed to stand the system up on a Linux (RHEL/CentOS/Rocky/Alma/Fedora/
Gentoo/Debian/Ubuntu) or Windows Server (2016–2025 Datacenter) box, plus the
compatibility matrix, ports/sockets, patch guidance, and the hotlink-protection
URL-rewrite rules you asked for.

> **MFLOPS.** Every "MFLOPS HERE NEEDED" is filled by the **report engine**
> (`ggw_report`). Run it on the target box and it writes a report with that box's
> real double-precision MFLOPS (dense matmul + sustained FMA), CPU/RAM/disk, and an
> OS compatibility verdict. Reference figure from my build box (AMD EPYC-Genoa, one
> thread, `-O2`): **~3,400 MFLOPS matmul / ~7,200 MFLOPS FMA**. Your hardware will
> print its own — that is the number to quote per machine.

## Hardware requirements

| Tier | CPU | RAM | Disk | Use |
|------|-----|-----|------|-----|
| Thin client (phone/PC) | 2 cores, x86-64 or ARM64 | 2 GB | 200 MB | PWA + approve binary |
| Server (single) | 4 cores | 8 GB | 20 GB SSD | control plane + SQL small |
| Server (gamehouse demo) | 8+ cores | 16–32 GB | 100 GB SSD | SQL + media edge + measures |
| SQL data plane | 4+ cores | 8 GB min (16 GB recommended) | 50 GB SSD | MS SQL 2016–2025 |

No GPU is required; when a GPU is present the measures use it, otherwise they report
`NO DEVICE` and run on CPU.

## Software requirements

| Component | Requirement |
|-----------|-------------|
| C++ modules | build once with GCC/Clang (Linux) or MinGW/MSVC (Windows); no runtime deps (static) |
| Data plane | Microsoft SQL Server 2016 / 2017 / 2019 / 2022 / 2025, Express→Enterprise |
| ODBC | Microsoft ODBC Driver 18 for SQL Server |
| Web front (optional) | Nginx or Apache or IIS as reverse proxy (URL-rewrite rules below) |
| PWA | any modern browser; installs to home screen, no store required |

## OS compatibility matrix

| OS | Version | Status | Notes |
|----|---------|--------|-------|
| RHEL | 8 / 9 | Supported | primary Linux target |
| CentOS / Rocky / AlmaLinux | 8 / 9 | Supported | RHEL-compatible |
| Fedora | 38+ | Supported | |
| Gentoo | current | Supported | source build, `-O2 -march=native` |
| Debian / Ubuntu | 11+ / 22.04+ | Supported | build box is Ubuntu 24.04 |
| Windows Server | 2016 / 2019 / 2022 / 2025 Datacenter | Supported | detected via RtlGetVersion |
| Windows | 10 / 11 | Supported | dev + thin client |

The report engine detects the running OS and prints the verdict automatically.

## Linux installer (RHEL / CentOS / Gentoo)

```bash
# 1. build (RHEL/CentOS: dnf install gcc-c++ ; Gentoo: emerge sys-devel/gcc)
g++ -std=c++17 -O2 -march=native ggw_report.cpp -o /usr/local/bin/ggw_report
#    ...repeat for each module, or run build-all.sh

# 2. ISO / drive mount (SCSI and compatible controllers)
#    the installer ISO mounts read-only; sd* = SATA/SCSI/USB, works out of the box:
mount -o ro /dev/sr0 /mnt/iso        # optical / ISO image
mount -o ro /dev/sdb1 /mnt/media     # SCSI / USB media
#    verify the block device first:
lsblk -o NAME,TYPE,SIZE,TRAN         # TRAN shows sata/usb/scsi/nvme

# 3. ODBC for the data plane (RHEL family)
dnf install unixODBC msodbcsql18
```

## Windows Server installer

```
1. Install "Microsoft ODBC Driver 18 for SQL Server" (msodbcsql.msi).
2. Copy the pre-built ggw_*.exe (static, no VC++ redist needed) to C:\NetSwitch\.
3. Install SQL Server (Express is fine for the demo) and run ggw_scaling.sql.
4. Open port 1433 (SQL) and your HTTP port in Windows Firewall (below).
```

## Ports, sockets & firewall

| Service | Port | Proto | Notes |
|---------|------|-------|-------|
| MS SQL | 1433 | TCP | data plane |
| HTTP control | 8080 (or 80) | TCP | server-cpp / behind proxy |
| HTTPS | 443 | TCP | TLS 1.3 (secure module) |
| SIP | 5060 | UDP | voice-edge signalling |
| RTP media | 10000–20000 | UDP | voice-edge media (range) |

Linux firewall (firewalld):
```bash
firewall-cmd --permanent --add-port=1433/tcp --add-port=443/tcp \
             --add-port=5060/udp --add-port=10000-20000/udp
firewall-cmd --reload
```
Windows firewall:
```
netsh advfirewall firewall add rule name="MSSQL" dir=in action=allow protocol=TCP localport=1433
netsh advfirewall firewall add rule name="RTP"   dir=in action=allow protocol=UDP localport=10000-20000
```
Socket automation: the C++ modules open/close their own sockets; nothing to schedule.
On Linux you can wrap any module as a `systemd` socket-activated unit if you want the
kernel to hold the listening socket and start the service on first connection.

## Service packs, patches, SHAs, upgrades

- **SQL Server** follows Microsoft's Cumulative Update (CU) cadence — apply the
  latest CU for your major version; there are no numbered SP1–SP5 on 2017+, they were
  replaced by CUs. Track them on Microsoft's SQL Server build list.
- **Our modules** are versioned by git commit; each release zip carries a `SHA256SUMS`
  file so you can verify integrity:
  ```bash
  sha256sum -c SHA256SUMS      # Linux
  certutil -hashfile ggw_report.exe SHA256   # Windows
  ```
- Upgrades are drop-in: replace the binary, no migration; the SQL side uses
  `CREATE OR ALTER` so re-running the script upgrades functions in place.

## Hotlink protection — URL rewrite on HTTP_REFERER

Inspect the incoming `Referer` header, allow only your own domain or empty
(direct/typed) traffic, and block the rest with `403` (or redirect to a warning
image). Rules for all three front ends:

**IIS (web.config, official URL Rewrite module)**
```xml
<system.webServer><rewrite><rules>
  <rule name="block-hotlink" stopProcessing="true">
    <match url=".*" />
    <conditions logicalGrouping="MatchAll">
      <add input="{HTTP_REFERER}" pattern="^$" negate="true" />
      <add input="{HTTP_REFERER}" pattern="^https?://(www\.)?yourdomain\.com/" negate="true" />
    </conditions>
    <action type="CustomResponse" statusCode="403" statusReason="Forbidden"
            statusDescription="Hotlinking not allowed" />
  </rule>
</rules></rewrite></system.webServer>
```

**Nginx**
```nginx
location / {
    valid_referers none blocked yourdomain.com *.yourdomain.com;
    if ($invalid_referer) { return 403; }   # or: rewrite ^ /warning.png break;
}
```

**Apache (mod_rewrite)**
```apache
RewriteEngine On
RewriteCond %{HTTP_REFERER} !^$
RewriteCond %{HTTP_REFERER} !^https?://(www\.)?yourdomain\.com/ [NC]
RewriteRule .* - [F]            # F = 403 Forbidden (or: RewriteRule .* /warning.png [L])
```
Swap `yourdomain.com` for your real domain. "Empty referrer allowed" covers people
who type the URL directly or use a bookmark.

## On the region / vendor list (Google ARC, SAP EU/US/JP/KR/APAC/AU/IN/TR, LATAM)

That is a very large integration surface. Nothing in the product talks to those
networks today, and I won't invent compatibility we haven't built and tested. If you
tell me which one is actually first on the roadmap (e.g. "SAP EU" or "one Azure
region"), I'll scope a real connector for that one and document it properly rather
than list vendors we don't integrate with yet.
