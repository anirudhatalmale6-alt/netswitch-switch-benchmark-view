# DDR / RAM / ROM REPORT 2.3, THREADED - every memory module (HBM too) with maker, part and SERIAL NUMBER, the BIOS ROM,
# the DEVICE SERIALS (CPU id and socket, GPU, motherboard, fans, serial bus, RF unit, antenna, battery),
# each serial written out as octets in binary and sigmoid'(x) of every octet - and now the memory in use, the page
# file and the biggest processes, every query run at the same time on its own thread.
# Run in PowerShell:   powershell -ExecutionPolicy Bypass -File memory-report.ps1
#   -Threads 4        threads to use (default: one per query, at most the CPU threads)
#   -Watch 60         after the report, watch the memory for 60 s, one line a second, saved to memory-watch.csv
#   -SelfTest         test the octet maths and the thread pool without hardware (also on Linux: pwsh -File ...)
# Writes memory-report.txt and memory-report.json beside it; the .json loaded on thermals.html puts the serials in small
# print under the copyright line on every page (kept in that browser only, never sent anywhere).
# AI2ORBIT CO. Network system Pat. pend. Founded in 2018. Copyright 2018-2027. All rights reserved.
param([string]$Out = "memory-report.txt", [int]$Threads = 0, [int]$Watch = 0, [string]$WatchOut = "memory-watch.csv", [switch]$SelfTest)

$INV = [Globalization.CultureInfo]::InvariantCulture
# a serial in hex digits (even length) is read two digits to an octet; any other serial octet by character
function Get-Octets([string]$s) {
  $t = "$s".Trim()
  if ($t -match '^[0-9A-Fa-f]+$' -and $t.Length % 2 -eq 0) { for ($i = 0; $i -lt $t.Length; $i += 2) { [Convert]::ToByte($t.Substring($i, 2), 16) } }
  else { [Text.Encoding]::ASCII.GetBytes($t) }
}
# sigmoid'(x) = s(x)(1 - s(x)), with the octet centred and scaled: x = (octet - 128) / 32
function Get-SigD([double]$b) { $x = ($b - 128) / 32; $s = 1 / (1 + [Math]::Exp(-$x)); $s * (1 - $s) }
function Write-Serial($L, [string]$serial) {
  $o = @(Get-Octets $serial)
  if ($o.Count -eq 0 -or "$serial".Trim() -eq '') { $L.Add("  Serial octets     : none given"); return }
  $L.Add("  Serial octets     : " + (($o | ForEach-Object { '{0:X2}' -f $_ }) -join ' '))
  $L.Add("  Serial in binary  : " + (($o | ForEach-Object { [Convert]::ToString($_, 2).PadLeft(8, '0') }) -join ' '))
  $L.Add("  sigmoid'(x) each  : " + (($o | ForEach-Object { (Get-SigD $_).ToString('0.0000', $INV) }) -join ' '))
  $sum = ($o | Measure-Object -Sum).Sum
  $L.Add("  Octets, sum       : " + $o.Count + " octets, sum " + $sum + ", sigmoid'(mean) " + (Get-SigD ($sum / $o.Count)).ToString('0.0000', $INV))
}
function GB($b) { if ($null -eq $b) { return '-' }; ([double]$b / 1GB).ToString('0.00', $INV) + ' GB' }
function MB($b) { if ($null -eq $b) { return '-' }; ([double]$b / 1MB).ToString('0', $INV) + ' MB' }

# THE THREAD POOL. Each query is a script block run on its own runspace; all start together, the report waits for
# all of them and keeps each one's own time, so the time saved by running them together is measured, not claimed.
# Works in Windows PowerShell 5.1 and PowerShell 7.
function Invoke-Parallel([System.Collections.Specialized.OrderedDictionary]$Jobs, [int]$Max) {
  if ($Max -lt 1) { $Max = [Math]::Max(1, [Math]::Min($Jobs.Count, [Environment]::ProcessorCount)) }
  $pool = [RunspaceFactory]::CreateRunspacePool(1, $Max); $pool.Open()
  $wall = [Diagnostics.Stopwatch]::StartNew(); $run = @()
  foreach ($name in $Jobs.Keys) {
    $ps = [PowerShell]::Create(); $ps.RunspacePool = $pool
    [void]$ps.AddScript({ param($body) $sw = [Diagnostics.Stopwatch]::StartNew(); $err = $null; $data = $null
      try { $data = & ([ScriptBlock]::Create($body)) } catch { $err = ("$_" -split "`n")[0].Trim() }
      [pscustomobject]@{ Data = $data; Ms = $sw.Elapsed.TotalMilliseconds; Error = $err; Thread = [Threading.Thread]::CurrentThread.ManagedThreadId } }).AddArgument($Jobs[$name].ToString())
    $run += [pscustomobject]@{ Name = $name; PS = $ps; Handle = $ps.BeginInvoke() }
  }
  $res = [ordered]@{}
  foreach ($r in $run) { $o = $r.PS.EndInvoke($r.Handle); $res[$r.Name] = $o[0]; $r.PS.Dispose() }
  $wall.Stop(); $pool.Close(); $pool.Dispose()
  [pscustomobject]@{ Results = $res; WallMs = $wall.Elapsed.TotalMilliseconds; Threads = $Max }
}
function Add-Timing($L, $P) {
  $sum = 0; foreach ($k in $P.Results.Keys) { $sum += $P.Results[$k].Ms }
  $ids = @($P.Results.Values | ForEach-Object { $_.Thread } | Sort-Object -Unique).Count
  $L.Add("THREADS: " + $P.Results.Count + " queries on " + $P.Threads + " threads (" + $ids + " used): " + $P.WallMs.ToString('0', $INV) + " ms together, " + $sum.ToString('0', $INV) + " ms one after another")
  foreach ($k in $P.Results.Keys) { $r = $P.Results[$k]; $L.Add("  " + $k.PadRight(18) + ": " + $r.Ms.ToString('0', $INV).PadLeft(6) + " ms, thread " + $r.Thread + $(if ($r.Error) { ", ERROR " + $r.Error } else { "" })) }
}

$L = New-Object System.Collections.Generic.List[string]
if ($SelfTest) {
  $L.Add("SELF TEST")
  foreach ($s in @('1A2B3C4D', 'K4A8G045WC', '')) { $L.Add("Serial '" + $s + "'"); Write-Serial $L $s }
  # four jobs of 400 ms each: on four threads they must finish in well under the 1 600 ms they take one after another
  $jobs = [ordered]@{}; foreach ($i in 1..4) { $jobs["Job $i"] = [ScriptBlock]::Create("Start-Sleep -Milliseconds 400; $i * 10") }
  $P = Invoke-Parallel $jobs 4; Add-Timing $L $P
  $vals = @($P.Results.Values | ForEach-Object { $_.Data }) -join ','
  $ok = ($P.WallMs -lt 1200) -and ($vals -eq '10,20,30,40')
  $L.Add("Thread pool       : " + $(if ($ok) { "PASS - results in order ($vals), " + $P.WallMs.ToString('0', $INV) + " ms for 1600 ms of work" } else { "FAIL - $vals, " + $P.WallMs.ToString('0', $INV) + " ms" }))
  $jobs2 = [ordered]@{ Good = { 1 }; Broken = { throw 'no such class' } }; $P2 = Invoke-Parallel $jobs2 2
  $L.Add("Failed query      : " + $(if ($P2.Results['Broken'].Error -and $P2.Results['Good'].Data -eq 1) { "PASS - one query failing leaves the others" } else { "FAIL" }))
  $L | ForEach-Object { $_ }; if ($ok) { exit 0 } else { exit 1 }
}

# THE QUERIES, all at once
$Q = [ordered]@{
  Modules       = { @(Get-CimInstance Win32_PhysicalMemory) }
  Slots         = { @(Get-CimInstance Win32_PhysicalMemoryArray) }
  Bios          = { Get-CimInstance Win32_BIOS }
  System        = { Get-CimInstance Win32_OperatingSystem }
  MemoryCounters= { Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory }
  PageFile      = { @(Get-CimInstance Win32_PageFileUsage) }
  # graphics cards and accelerators keep their own memory (HBM on data-centre cards); Windows gives the size in the
  # driver key as a 64-bit figure - the older Win32_VideoController.AdapterRAM stops at 4 GB, so it is only the fallback
  Graphics      = { $k = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}'
                    @(Get-CimInstance Win32_VideoController | ForEach-Object { $v = $_; $mem = $null
                      foreach ($sub in @(Get-ChildItem $k -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -match '^\d{4}$' })) {
                        $p = Get-ItemProperty $sub.PSPath -ErrorAction SilentlyContinue
                        if ($p -and $p.DriverDesc -eq $v.Name -and $p.'HardwareInformation.qwMemorySize') { $mem = [double]$p.'HardwareInformation.qwMemorySize' } }
                      [pscustomobject]@{ Name = $v.Name; Memory = $(if ($mem) { $mem } else { [double]$v.AdapterRAM }); Exact = [bool]$mem; Driver = $v.DriverVersion; Pnp = $v.PNPDeviceID } }) }
  # THE DEVICE SERIALS. Windows gives what each maker wrote into the firmware; an empty or placeholder entry is shown as
  # "not given". Fans and antennas have no number Windows can read; a mobile broadband unit gives its Device Id (IMEI).
  Cpu           = { @(Get-CimInstance Win32_Processor | Select-Object Name, ProcessorId, SerialNumber, SocketDesignation, PartNumber) }
  Board         = { Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer, Product, SerialNumber, Version }
  Fans          = { @(Get-CimInstance Win32_Fan | Select-Object Name, DeviceID, PNPDeviceID) }
  SerialBus     = { @(Get-CimInstance Win32_USBController | Select-Object Name, PNPDeviceID) }
  Battery       = { @(Get-CimInstance Win32_Battery | Select-Object Name, DeviceID) }
  BatterySerial = { @(Get-CimInstance -Namespace root/wmi -ClassName BatteryStaticData -ErrorAction Stop | Select-Object ManufactureName, DeviceName, SerialNumber) }
  Radio         = { @(Get-CimInstance Win32_NetworkAdapter -Filter 'PhysicalAdapter=True' | Select-Object Name, MACAddress, PNPDeviceID) }
  Mobile        = { @(& netsh mbn show interfaces 2>$null | Where-Object { $_ -match '^\s*(Manufacturer|Model|Device Id)\s*:' } | ForEach-Object { $_.Trim() -replace '\s*:\s*', ' ' }) }
  GpuSerial     = { $n = Get-Command nvidia-smi -ErrorAction SilentlyContinue; if ($n) { @(& $n.Source --query-gpu=name,serial,uuid --format=csv,noheader 2>$null) } else { @() } }
  Processes     = { @(Get-Process | Sort-Object WorkingSet64 -Descending | Select-Object -First 10 Name, Id, WorkingSet64, PrivateMemorySize64, @{ n = 'Threads'; e = { $_.Threads.Count } }) }
}
$P = Invoke-Parallel $Q $Threads
$R = $P.Results

# SMBIOS type 17 memory types (DMTF DSP0134): 32 HBM, 33 HBM2, 36 HBM3 - stacked memory on the processor package, as on Intel Xeon Max
$TYPES = @{ 18 = 'DDR'; 19 = 'DDR2'; 24 = 'DDR3'; 26 = 'DDR4'; 27 = 'LPDDR'; 28 = 'LPDDR2'; 29 = 'LPDDR3'; 30 = 'LPDDR4'; 31 = 'Logical non-volatile'; 32 = 'HBM'; 33 = 'HBM2'; 34 = 'DDR5'; 35 = 'LPDDR5'; 36 = 'HBM3'; 37 = 'MRDIMM' }
$L.Add("DDR / RAM / ROM REPORT 2.3, THREADED")
$L.Add("Computer " + $env:COMPUTERNAME + ", taken " + (Get-Date -Format 'dd.MM.yyyy HH:mm'))
$L.Add(""); Add-Timing $L $P
if ($PSVersionTable.PSEdition -eq 'Core' -and -not $IsWindows) { $L.Add(""); $L.Add("NOTE: the memory and ROM queries need Windows; on Linux use memory-report.sh") }
$mods = @($R['Modules'].Data | Where-Object { $_ })
$total = ($mods | Measure-Object -Property Capacity -Sum).Sum
$L.Add(""); if ($R['Modules'].Error) { $L.Add("RAM: not read - " + $R['Modules'].Error) } else { $L.Add("RAM: " + $mods.Count + " module(s), " + (GB $total) + " in all") }
$slots = @($R['Slots'].Data | Where-Object { $_ })
if ($slots.Count) { $L.Add("Slots: " + (($slots | Measure-Object -Property MemoryDevices -Sum).Sum) + " on the board, up to " + (GB ((($slots | Measure-Object -Property MaxCapacityEx -Sum).Sum) * 1KB)) + " supported") }
$n = 0
foreach ($m in $mods) {
  $n++; $t = $TYPES[[int]$m.SMBIOSMemoryType]; if (-not $t) { $t = 'Type ' + $m.SMBIOSMemoryType }
  $L.Add(""); $L.Add("Module " + $n + " - " + $m.DeviceLocator + " " + $m.BankLabel)
  $L.Add("  Kind              : " + $t + ", " + (GB $m.Capacity))
  $L.Add("  Speed             : " + $m.Speed + " MT/s rated, " + $m.ConfiguredClockSpeed + " MT/s running")
  $L.Add("  Maker             : " + "$($m.Manufacturer)".Trim())
  $L.Add("  Part number       : " + "$($m.PartNumber)".Trim())
  $L.Add("  Serial number     : " + "$($m.SerialNumber)".Trim())
  Write-Serial $L $m.SerialNumber
}
$os = $R['System'].Data; $pc = $R['MemoryCounters'].Data
if ($os) {
  $tv = [double]$os.TotalVisibleMemorySize * 1KB; $fp = [double]$os.FreePhysicalMemory * 1KB
  $L.Add(""); $L.Add("MEMORY IN USE NOW")
  $L.Add("  Seen by Windows   : " + (GB $tv) + ", in use " + (GB ($tv - $fp)) + " (" + (100 * ($tv - $fp) / [Math]::Max(1, $tv)).ToString('0', $INV) + " %), free " + (GB $fp))
  $L.Add("  Virtual           : " + (GB ([double]$os.TotalVirtualMemorySize * 1KB)) + ", free " + (GB ([double]$os.FreeVirtualMemory * 1KB)))
}
if ($pc) {
  $L.Add("  Available         : " + $pc.AvailableMBytes + " MB")
  $L.Add("  Committed         : " + (MB $pc.CommittedBytes) + " of " + (MB $pc.CommitLimit) + " limit")
  $L.Add("  Cache             : " + (MB $pc.CacheBytes) + ", non-paged pool " + (MB $pc.PoolNonpagedBytes))
  $L.Add("  Paging            : " + $pc.PagesPersec + " pages a second, " + $pc.PageFaultsPersec + " page faults a second")
}
foreach ($pf in @($R['PageFile'].Data | Where-Object { $_ })) { $L.Add("  Page file         : " + $pf.Name + ", " + $pf.AllocatedBaseSize + " MB, " + $pf.CurrentUsage + " MB in use, peak " + $pf.PeakUsage + " MB") }
$gfx = @($R['Graphics'].Data | Where-Object { $_ })
if ($gfx.Count) { $L.Add(""); $L.Add("GRAPHICS AND ACCELERATORS (their own memory - HBM on data-centre cards; Windows does not name the memory type here)")
  foreach ($gc in $gfx) { $L.Add("  " + "$($gc.Name)".PadRight(40) + (GB $gc.Memory) + $(if ($gc.Exact) { "" } else { " (or more - the old figure stops at 4 GB)" }) + ", driver " + $gc.Driver) } }
$procs = @($R['Processes'].Data | Where-Object { $_ })
if ($procs.Count) { $L.Add(""); $L.Add("BIGGEST PROCESSES (working set, private, threads)")
  foreach ($p in $procs) { $L.Add("  " + "$($p.Name)".PadRight(24) + (MB $p.WorkingSet64).PadLeft(9) + (MB $p.PrivateMemorySize64).PadLeft(10) + ("" + $p.Threads + " thr").PadLeft(9) + "  id " + $p.Id) } }
$b = $R['Bios'].Data
$L.Add(""); $L.Add("ROM (BIOS / UEFI)")
if ($b) {
  $L.Add("  Maker             : " + $b.Manufacturer)
  $L.Add("  Version           : " + $b.SMBIOSBIOSVersion)
  $L.Add("  Released          : " + $b.ReleaseDate)
  $L.Add("  Serial number     : " + "$($b.SerialNumber)".Trim())
  Write-Serial $L $b.SerialNumber
} else { $L.Add("  Not read: " + $R['Bios'].Error) }
# DEVICE SERIALS, one row each: the same rows go to the .json for the small print on every page
$SR = New-Object System.Collections.Generic.List[object]
function Add-Ser([string]$k, [string]$v) { $SR.Add([ordered]@{ k = $k; v = ($v -replace '\s+', ' ').Trim() }) }
function Val($x) { $t = "$x".Trim(); if ($t -eq '' -or $t -match '^(to be filled|default string|none|n/a|not specified|not applicable|system serial number|\[n/a\])' -or $t -match '^0+$') { 'not given' } else { $t } }
$n = 0; foreach ($m in $mods) { $n++; Add-Ser ("DDR " + $n + " " + "$($m.DeviceLocator)".Trim()) ("$($m.Manufacturer) $($m.PartNumber)".Trim() + ", serial " + (Val $m.SerialNumber)) }
if ($b) { Add-Ser "ROM" ("$($b.Manufacturer) $($b.SMBIOSBIOSVersion)".Trim() + ", serial " + (Val $b.SerialNumber)) }
$n = 0; foreach ($c in @($R['Cpu'].Data | Where-Object { $_ })) { $n++
  Add-Ser ("CPU " + $n) ("$($c.Name)".Trim() + ", id " + (Val $c.ProcessorId) + ", serial " + (Val $c.SerialNumber) + ", part " + (Val $c.PartNumber))
  Add-Ser ("Socket " + $n) (Val $c.SocketDesignation) }
if ($R['Cpu'].Error) { Add-Ser "CPU" ("not read - " + $R['Cpu'].Error) }
$gs = @($R['GpuSerial'].Data | Where-Object { $_ }); $n = 0
foreach ($gc in $gfx) { $n++; $ser = 'not given'
  foreach ($line in $gs) { $f = "$line" -split ',\s*'; if ($f.Count -ge 3 -and "$($gc.Name)" -like ('*' + $f[0].Replace('NVIDIA ', '') + '*')) { $ser = (Val $f[1]) + ", uuid " + $f[2] } }
  Add-Ser ("GPU " + $n) ("$($gc.Name)".Trim() + ", serial " + $ser + ", device " + (Val $gc.Pnp)) }
$bd = $R['Board'].Data
if ($bd) { Add-Ser "Motherboard" ("$($bd.Manufacturer) $($bd.Product)".Trim() + ", version " + (Val $bd.Version) + ", serial " + (Val $bd.SerialNumber)) } else { Add-Ser "Motherboard" "not read" }
$fans = @($R['Fans'].Data | Where-Object { $_ }); $n = 0
if ($fans.Count) { foreach ($f in $fans) { $n++; Add-Ser ("Fan " + $n) ("$($f.Name)".Trim() + ", id " + (Val $f.DeviceID) + ", device " + (Val $f.PNPDeviceID)) } } else { Add-Ser "Fan" "not reported by Windows" }
$n = 0; foreach ($u in @($R['SerialBus'].Data | Where-Object { $_ })) { $n++; Add-Ser ("Serial bus " + $n) ("$($u.Name)".Trim() + ", device " + (Val $u.PNPDeviceID)) }
$radios = @($R['Radio'].Data | Where-Object { $_ -and "$($_.Name)" -match 'Wi-?Fi|Wireless|WLAN|802\.11|Bluetooth|Mobile|Cellular|WWAN|LTE|5G|Broadband' }); $n = 0
if ($radios.Count) { foreach ($ra in $radios) { $n++; Add-Ser ("RF unit " + $n) ("$($ra.Name)".Trim() + ", MAC " + (Val $ra.MACAddress) + ", device " + (Val $ra.PNPDeviceID)) } } else { Add-Ser "RF unit" "no radio adapter found" }
$mob = @($R['Mobile'].Data | Where-Object { $_ }); if ($mob.Count) { Add-Ser "Mobile RF unit" ($mob -join ', ') }
Add-Ser "Antenna" "not reported by Windows (antennas carry no readable number)"
$bs = @($R['BatterySerial'].Data | Where-Object { $_ }); $bw = @($R['Battery'].Data | Where-Object { $_ }); $n = 0
if ($bs.Count) { foreach ($x in $bs) { $n++; Add-Ser ("Battery " + $n) ("$($x.ManufactureName) $($x.DeviceName)".Trim() + ", serial " + (Val $x.SerialNumber)) } }
elseif ($bw.Count) { foreach ($x in $bw) { $n++; Add-Ser ("Battery " + $n) ("$($x.Name)".Trim() + ", serial not given (run as administrator to read it)") } }
else { Add-Ser "Battery" "none found" }
$L.Add(""); $L.Add("DEVICE SERIALS")
foreach ($x in $SR) { $L.Add("  " + $x.k.PadRight(18) + ": " + $x.v) }
$L.Add(""); $L.Add("Browser side: the timed memory figures are on thermals.html, section Memory, DDR.")
$L.Add("AI2ORBIT CO. Network system Pat. pend. Founded in 2018. Copyright 2018-2027. All rights reserved.")
$L | Set-Content -Encoding ASCII $Out
# the same modules as JSON, for thermals.html (section Memory Modules, Load memory-report.json)
$J = [ordered]@{ computer = $env:COMPUTERNAME; taken = (Get-Date -Format 'dd.MM.yyyy HH:mm'); version = '2.3'
  modules = @($mods | ForEach-Object { $tt = $TYPES[[int]$_.SMBIOSMemoryType]; if (-not $tt) { $tt = 'Type ' + $_.SMBIOSMemoryType }
    [ordered]@{ slot = "$($_.DeviceLocator)".Trim(); bank = "$($_.BankLabel)".Trim(); kind = $tt; gb = [Math]::Round([double]$_.Capacity / 1GB, 2); speed = "$($_.Speed)"; running = "$($_.ConfiguredClockSpeed)"
      maker = "$($_.Manufacturer)".Trim(); part = "$($_.PartNumber)".Trim(); serial = "$($_.SerialNumber)".Trim() } })
  bios = $(if ($b) { [ordered]@{ maker = "$($b.Manufacturer)"; version = "$($b.SMBIOSBIOSVersion)"; date = "$($b.ReleaseDate)"; serial = "$($b.SerialNumber)".Trim() } } else { $null })
  serials = $SR.ToArray() }
$JOut = [IO.Path]::ChangeExtension($Out, '.json')
$J | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $JOut
$L | ForEach-Object { $_ }
"Written: " + (Resolve-Path $Out) + " and " + (Resolve-Path $JOut) + " - load the .json on thermals.html, Memory Modules"

# THE WATCH: a sampling thread reads the counters once a second while this one prints and saves them
if ($Watch -gt 0) {
  "Watching the memory for $Watch s, one line a second - Ctrl+C to stop early"
  "time,available_mb,committed_mb,pages_per_s,in_use_pct" | Set-Content -Encoding ASCII $WatchOut
  $q = [Collections.Concurrent.ConcurrentQueue[object]]::new()
  $ps = [PowerShell]::Create()
  [void]$ps.AddScript({ param($q, $n)
    for ($i = 0; $i -lt $n; $i++) { $t0 = [DateTime]::Now
      $c = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory; $o = Get-CimInstance Win32_OperatingSystem
      $use = 100 * ([double]$o.TotalVisibleMemorySize - [double]$o.FreePhysicalMemory) / [Math]::Max(1, [double]$o.TotalVisibleMemorySize)
      $q.Enqueue([pscustomobject]@{ T = $t0; Avail = $c.AvailableMBytes; Comm = [Math]::Round($c.CommittedBytes / 1MB); Pages = $c.PagesPersec; Use = $use })
      $rest = 1000 - ([DateTime]::Now - $t0).TotalMilliseconds; if ($rest -gt 0) { Start-Sleep -Milliseconds $rest } } }).AddArgument($q).AddArgument($Watch)
  $h = $ps.BeginInvoke(); $seen = 0
  while (-not $h.IsCompleted -or -not $q.IsEmpty) {
    $s = $null
    while ($q.TryDequeue([ref]$s)) { $seen++
      $line = $s.T.ToString('HH:mm:ss') + "  available " + ("" + $s.Avail).PadLeft(6) + " MB  committed " + ("" + $s.Comm).PadLeft(6) + " MB  paging " + ("" + $s.Pages).PadLeft(5) + "/s  in use " + $s.Use.ToString('0', $INV) + " %"
      $line
      ($s.T.ToString('HH:mm:ss') + "," + $s.Avail + "," + $s.Comm + "," + $s.Pages + "," + $s.Use.ToString('0.0', $INV)) | Add-Content -Encoding ASCII $WatchOut }
    Start-Sleep -Milliseconds 100
  }
  $ps.EndInvoke($h) | Out-Null; $ps.Dispose()
  "Watched $seen seconds, saved: " + (Resolve-Path $WatchOut)
}
