# 6GGW Switch Control File — format & MIME registration

6GGW is the **controller / console** for a network switch. The Switch tab lets you set the
switch's intended state with simple On/Off functions, then export a **control file** that a
real switch or router firmware reads and executes — the same idea as a BIOS config.

> An app cannot flip the phone's own Wi-Fi / 5G radio (the operating system forbids that).
> 6GGW produces the *control file* that drives the switch; it does not physically toggle a radio.

## Two equivalent formats

### `.switchc` — C-style control (human-readable, BIOS-like)

```c
/* 6GGW switch control file — BIOS-style config for the network switch */
switch "6GGW" {
    wifi              = on;
    wifi_support      = "6,8";      /* Wi-Fi 6 and 8 */
    cellular_5g       = on;
    performance       = on;
    sync              = on;         /* share Wi-Fi + sync data */
    throughput_target = 50%;        /* requested headroom, +15%..+100% */
    routing_mode      = "FAST-PIPE";
}
```

### `.xml` — same values, XML control

```xml
<?xml version="1.0" encoding="UTF-8"?>
<switch name="6GGW">
  <wifi enabled="true" support="6,8"/>
  <cellular5g enabled="true"/>
  <performance enabled="true"/>
  <sync enabled="true"/>
  <throughput target="50"/>
  <routing mode="FAST-PIPE"/>
</switch>
```

The app **exports** both and can **load** either back in (Switch tab → Download / Load control file).

## Fields

| field | values | meaning |
|-------|--------|---------|
| `wifi` | on / off | Wi-Fi radio path enabled |
| `wifi_support` | `"6,8"` | Wi-Fi generations supported (Wi-Fi 6, Wi-Fi 8) |
| `cellular_5g` | on / off | 5G cellular path enabled |
| `performance` | on / off | live performance monitor enabled |
| `sync` | on / off | share Wi-Fi + sync data |
| `throughput_target` | 15–100 % | requested throughput headroom |
| `routing_mode` | FAST-PIPE / STORE-FWD / CUT-THRU / ADAPTIVE | switch routing strategy |

## MIME type registration (do this when you deploy)

Primary type:

    application/x-6ggw-switch    →  .switchc

The app also accepts the generic binary type you proposed:

    application/octet-stream     →  .fgpb

### Apache (`.htaccess` or vhost)
```apache
AddType application/x-6ggw-switch .switchc
AddType application/octet-stream  .fgpb
```

### nginx (`mime.types`)
```nginx
types {
    application/x-6ggw-switch  switchc;
    application/octet-stream   fgpb;
}
```

### Windows registry (per-machine, when you need OS-level association)
```
HKEY_CLASSES_ROOT\.switchc
  (Default) = "6GGW.SwitchControl"
  "Content Type" = "application/x-6ggw-switch"
```

Official/IANA registration is only needed if you want the type recognised publicly — you can
register it later when required; nothing in 6GGW depends on it. Locally, the AddType lines above
are enough for a server to serve `.switchc` correctly.

---
*6GGW — AI2ORBIT Co. Built on the NetSwitch engine.*
