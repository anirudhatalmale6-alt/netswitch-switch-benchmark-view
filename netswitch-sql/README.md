# NetSwitch — Microsoft SQL Server client (Windows installer)

A small Windows console client that connects the NetSwitch (Microsoft SQL Server) variant to a
Microsoft SQL Server over ODBC. It does exactly what you asked for:

- **forces a password change on first use** — the temporary/shipped password must be changed
  before the software will connect. The change is made *on the server* with `ALTER LOGIN`, so the
  old password genuinely stops working afterwards;
- **never stores the password** — only the server + username (a profile) is saved, in
  `%APPDATA%\NetSwitch\sql.cfg`. The password is typed at the prompt each time, hidden (no echo);
- after connecting it runs a **timed health query** and reports the **SQL round-trip in ms** — the
  same "measure the real line, then rank/route it" idea the rest of NetSwitch uses, applied to the
  database link.

## Install

Run `NetSwitch-SQL-Setup.exe`. It installs to `C:\Program Files\NetSwitch`, adds a Start-Menu and
Desktop shortcut (a console that opens in the install folder), and an entry in Apps & features.

## First use (the forced password change)

Open the "NetSwitch SQL (console)" shortcut and run:

```
netswitch_sql.exe --server tcp:YOURHOST,1433 --user YOURLOGIN
```

It will ask for the **current (temporary) password**, then a **new password** twice, change it on
the server, reconnect with the new one, and print the health line. From then on it just asks for the
password. Use `--reset` to force another change later.

Options:

```
--server HOST[,PORT]   e.g. tcp:127.0.0.1,1433     (required first run)
--user   NAME          SQL login                   (required first run)
--db     NAME          default database            (optional)
--driver NAME          ODBC driver (default: ODBC Driver 17 for SQL Server)
--reset                force the first-use password change again
--help
```

## What it needs on the machine

- The **Microsoft ODBC Driver for SQL Server** (17 or 18) installed on the Windows box. If you have
  18, pass `--driver "ODBC Driver 18 for SQL Server"`. It is a free Microsoft download.
- The SQL login must have permission to change its own password (a normal SQL login does).
- The link is opened with `Encrypt=yes;TrustServerCertificate=yes` so it's encrypted even against a
  dev server's self-signed certificate. For production, install a proper server certificate and drop
  `TrustServerCertificate`.

## Honest note on testing

I built and static-linked the `.exe` here and verified the program itself runs (argument parsing,
prompts, help). The **live SQL connection and the `ALTER LOGIN` password change can only be verified
against a real SQL Server** — I don't run one, and shouldn't guess at your instance. Point it at your
server (or a free SQL Server Express / a Docker `mssql` container) and it will connect; if anything
about the driver name or server address needs adjusting, the exact ODBC error is printed so it's a
one-line fix.

## Build from source

```
x86_64-w64-mingw32-g++ -std=c++17 -O2 netswitch_sql.cpp -o netswitch_sql.exe -static -lodbc32
makensis installer.nsi        # -> NetSwitch-SQL-Setup.exe
```
