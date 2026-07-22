#!/usr/bin/env node
/*
 * 6GGW SERVER COMPONENT  —  the "2. server" half of the client/server product.
 *
 * Zero dependencies. Runs on any Node 16+ box (HCL / Fujitsu / a laptop):
 *     node server.js            # listens on :8090
 *     PORT=80 node server.js
 *
 * What it REALLY does (no modelled numbers on this side):
 *   /api/health            — this server's own live health (cpu load, memory, uptime, cores)
 *   /api/ping              — tiny echo; the CLIENT times it to get the real server<->phone line length
 *   /api/distance?host=..  — resolves DNS + does a real TCP-handshake RTT to the host (3 samples,
 *                            takes the closest), then the signal-path cable length from that latency
 *                            using the client's constant d = 0.0001607 m per picosecond
 *   /api/estimate?users&bitrate&server — MPEG-4 / broadband delivery capacity for N post addresses
 *   POST /api/device/health            — store a device's health snapshot in its OWN encrypted DB
 *   GET  /api/device/history?id=..     — read that device's stored history (decrypted)
 *   GET  /api/devices                  — list known device IDs
 *   /                      — serves the bundled 6GGW client (the PWA) so one box ships both halves
 *
 * The TCP-handshake timing is a genuine network measurement: it opens a real socket to the
 * target and times the SYN/SYN-ACK round trip. It is the honest, portable stand-in for a raw
 * ICMP traceroute (which needs root and is blocked in most hosting), and it is really measured.
 *
 * Release channels: CHANNEL=dev|rc|prod  (default dev). dev/rc/prod is the same code with
 * different config — prod is the production build, rc the release-candidate you test before
 * promoting, dev your working build. Reported in /api/health.
 *
 * Per-device DB: each hardware device gets its OWN database file under data/devices/<id>.db,
 * encrypted at rest with AES-256-GCM (Node's built-in crypto). Transport security is TLS
 * (put this behind nginx/Caddy, or run node's https with a real cert) — see README on key sizes.
 */
'use strict';
const http = require('http');
const net = require('net');
const dns = require('dns');
const os = require('os');
const fs = require('fs');
const path = require('path');
const url = require('url');
const crypto = require('crypto');
const zlib = require('zlib');

const PORT = process.env.PORT || 8090;
const CHANNEL = (process.env.CHANNEL || 'dev').toLowerCase();   // dev | rc | prod
const VERSION = '0.4.0';
const SIGNAL_CONST = 0.0001607;            // metres per picosecond (client's constant; ~160,700 km/s cable)
const CLIENT_DIR = path.join(__dirname, '..');   // the PWA lives one level up (index.html, sw.js, ...)
const DB_DIR = path.join(__dirname, 'data', 'devices');
/* AES-256 key from env DB_KEY (any string); dev default is clearly marked. Set a real one in prod. */
const DB_KEY = crypto.createHash('sha256').update(process.env.DB_KEY || '6ggw-dev-key-change-me').digest();

/* ---- per-device encrypted DB (AES-256-GCM at rest, one file per device) ---- */
function safeId(id) { return String(id || '').replace(/[^a-zA-Z0-9._-]/g, '').slice(0, 64) || 'unknown'; }
function deviceFile(id) { return path.join(DB_DIR, safeId(id) + '.db'); }
function encrypt(text) {
  const iv = crypto.randomBytes(12);
  const c = crypto.createCipheriv('aes-256-gcm', DB_KEY, iv);
  const enc = Buffer.concat([c.update(text, 'utf8'), c.final()]);
  return Buffer.concat([iv, c.getAuthTag(), enc]);              // iv(12) | tag(16) | ciphertext
}
function decrypt(buf) {
  const iv = buf.subarray(0, 12), tag = buf.subarray(12, 28), data = buf.subarray(28);
  const d = crypto.createDecipheriv('aes-256-gcm', DB_KEY, iv);
  d.setAuthTag(tag);
  return Buffer.concat([d.update(data), d.final()]).toString('utf8');
}
function readHistory(id) {
  const f = deviceFile(id);
  if (!fs.existsSync(f)) return [];
  try { return JSON.parse(decrypt(fs.readFileSync(f))); } catch (e) { return []; }
}
function saveHealth(id, record) {
  fs.mkdirSync(DB_DIR, { recursive: true });
  const hist = readHistory(id);
  const row = Object.assign({ received_at: new Date().toISOString(), channel: CHANNEL }, record);
  hist.push(row);
  while (hist.length > 500) hist.shift();                       // keep last 500 per device
  fs.writeFileSync(deviceFile(id), encrypt(JSON.stringify(hist)));
  return { stored: true, id: safeId(id), count: hist.length, record: row };
}
function listDevices() {
  if (!fs.existsSync(DB_DIR)) return [];
  return fs.readdirSync(DB_DIR).filter(f => f.endsWith('.db')).map(f => f.slice(0, -3));
}

/* ---- backup: bundle the (already-encrypted) per-device DBs into one gzipped, checksummed
        archive under data/backups/. The blobs stay AES-256 encrypted inside the bundle, so it
        is safe to copy straight onto tape (IBM/LTO) or any offline store. No DB key needed here. ---- */
const BACKUP_DIR = path.join(__dirname, 'data', 'backups');
function sha256(buf) { return crypto.createHash('sha256').update(buf).digest('hex'); }
function makeBackup() {
  fs.mkdirSync(BACKUP_DIR, { recursive: true });
  const ids = listDevices();
  const files = [], blobs = {};
  for (const id of ids) {
    const raw = fs.readFileSync(deviceFile(id));            // encrypted bytes, as-is
    files.push({ id, bytes: raw.length, sha256: sha256(raw) });
    blobs[id] = raw.toString('base64');
  }
  const manifest = {
    created_at: new Date().toISOString(), channel: CHANNEL, version: VERSION,
    device_count: ids.length, files, encryption: 'AES-256-GCM at rest (blobs unchanged)'
  };
  const bundleJson = Buffer.from(JSON.stringify({ manifest, blobs }));
  const gz = zlib.gzipSync(bundleJson);
  const stamp = manifest.created_at.replace(/[:.]/g, '-');
  const name = 'backup-' + CHANNEL + '-' + stamp + '.6ggwbak.gz';
  const outPath = path.join(BACKUP_DIR, name);
  fs.writeFileSync(outPath, gz);
  fs.writeFileSync(outPath + '.sha256', sha256(gz) + '  ' + name + '\n');
  return { file: name, path: outPath, devices: ids.length, bytes: gz.length,
    bundle_sha256: sha256(gz), manifest };
}
function listBackups() {
  if (!fs.existsSync(BACKUP_DIR)) return [];
  return fs.readdirSync(BACKUP_DIR).filter(f => f.endsWith('.6ggwbak.gz'))
    .map(f => ({ file: f, bytes: fs.statSync(path.join(BACKUP_DIR, f)).size }));
}

/* ---- real TCP-handshake RTT to host:port (ms), rejects on failure ---- */
function tcpRtt(host, port, timeoutMs) {
  return new Promise((resolve, reject) => {
    const t0 = process.hrtime.bigint();
    const sock = net.connect({ host, port }, () => {
      const ms = Number(process.hrtime.bigint() - t0) / 1e6;
      sock.destroy();
      resolve(ms);
    });
    sock.setTimeout(timeoutMs || 4000);
    sock.on('timeout', () => { sock.destroy(); reject(new Error('timeout')); });
    sock.on('error', (e) => { sock.destroy(); reject(e); });
  });
}

/* ---- distance: DNS resolve -> 3 TCP RTT samples -> take-closest -> cable km ---- */
async function measureDistance(host, port) {
  port = port || 443;
  const addr = await new Promise((res, rej) =>
    dns.lookup(host, (e, a) => e ? rej(e) : res(a)));
  const samples = [];
  for (let i = 0; i < 3; i++) {
    try { samples.push(await tcpRtt(addr, port, 4000)); } catch (e) { /* skip a failed sample */ }
  }
  if (!samples.length) throw new Error('no TCP response from ' + host + ':' + port);
  const rttMs = Math.min.apply(null, samples);            // take-closest / best sample
  const oneWayKm = SIGNAL_CONST * (rttMs * 1e9) / 1000 / 2;
  return {
    host, ip: addr, port,
    rtt_ms: +rttMs.toFixed(2),
    samples: samples.map(s => +s.toFixed(2)),
    signal_path_km: +oneWayKm.toFixed(1),
    method: 'TCP-handshake RTT, best of 3, cable constant d=0.0001607 m/ps',
    measured_at: new Date().toISOString()
  };
}

/* ---- round-the-clock SECURITY WATCH: a two-hand "clock" watchdog + security event log.
        short arm = fast tick (samples request/error/listener state);
        long arm  = slow tick (rolls the window up + runs the firewall / egress reachability
        self-test and raises ALERTs). Everything is appended to data/security/security-<date>.log.
        Cadences and the egress target are env-tunable (SEC_SHORT_MS / SEC_LONG_MS / SEC_FW_HOST). ---- */
const SEC_DIR  = path.join(__dirname, 'data', 'security');
const SHORT_MS = parseInt(process.env.SEC_SHORT_MS, 10) || 15000;    // short arm (minute hand)
const LONG_MS  = parseInt(process.env.SEC_LONG_MS, 10)  || 300000;   // long arm  (hour hand)
const FW_HOST  = process.env.SEC_FW_HOST || 'one.one.one.one';       // egress reachability probe
const FW_PORT  = parseInt(process.env.SEC_FW_PORT, 10) || 443;
const SEC = { started: Date.now(), reqTotal: 0, reqWindow: 0, errTotal: 0, errWindow: 0,
  byIp: {}, lastShort: null, lastLong: null,
  egress_ok: null, egress_ms: null, egress_checked: null, alerts: [] };

function secLogFile() { return path.join(SEC_DIR, 'security-' + new Date().toISOString().slice(0, 10) + '.log'); }
function secLog(level, event, detail) {
  try {
    fs.mkdirSync(SEC_DIR, { recursive: true });
    fs.appendFileSync(secLogFile(), JSON.stringify(Object.assign(
      { ts: new Date().toISOString(), level, event, channel: CHANNEL }, detail || {})) + '\n');
    if (level === 'ALERT') { SEC.alerts.push({ ts: new Date().toISOString(), event, detail: detail || {} }); while (SEC.alerts.length > 50) SEC.alerts.shift(); }
  } catch (e) { /* logging must never crash the server */ }
}
function secObserve(req, code, ms, ip) {
  SEC.reqTotal++; SEC.reqWindow++;
  if (code >= 400) { SEC.errTotal++; SEC.errWindow++; }
  SEC.byIp[ip] = (SEC.byIp[ip] || 0) + 1;
  const p = String(req.url || '').split('?')[0];
  if (code >= 400) secLog('WARN', 'http_error', { ip, method: req.method, path: p, code, ms });
  else if (req.method !== 'GET') secLog('INFO', 'http_write', { ip, method: req.method, path: p, code, ms });
}
async function secEgressCheck() {           // the firewall self-test: can we still reach out?
  try {
    const ms = await tcpRtt(FW_HOST, FW_PORT, 4000);
    SEC.egress_ok = true; SEC.egress_ms = +ms.toFixed(1);
  } catch (e) {
    SEC.egress_ok = false; SEC.egress_ms = null;
    secLog('ALERT', 'egress_blocked', { target: FW_HOST + ':' + FW_PORT,
      error: String(e && e.message || e), hint: 'outbound firewall may be blocking this port' });
  }
  SEC.egress_checked = new Date().toISOString();
}
function secShortTick() {                    // minute hand
  SEC.lastShort = new Date().toISOString();
  if (!srv.listening) secLog('ALERT', 'listener_down', { port: PORT });
  if (SEC.reqWindow >= 20 && SEC.errWindow / SEC.reqWindow > 0.5)
    secLog('ALERT', 'error_rate_high', { window_requests: SEC.reqWindow, window_errors: SEC.errWindow });
}
async function secLongTick() {               // hour hand — roll up + firewall self-test
  SEC.lastLong = new Date().toISOString();
  await secEgressCheck();
  const top = Object.entries(SEC.byIp).sort((a, b) => b[1] - a[1])[0];
  if (top && top[1] > 1000) secLog('ALERT', 'ip_spike', { ip: top[0], requests: top[1] });
  secLog('INFO', 'window_summary', {
    uptime_s: Math.round((Date.now() - SEC.started) / 1000),
    requests: SEC.reqWindow, errors: SEC.errWindow, unique_ips: Object.keys(SEC.byIp).length,
    egress_ok: SEC.egress_ok, egress_ms: SEC.egress_ms,
    load_avg: os.loadavg().map(n => +n.toFixed(2)), mem_free_mb: +(os.freemem() / 1048576).toFixed(0) });
  SEC.reqWindow = 0; SEC.errWindow = 0; SEC.byIp = {};
}
function secStatus() {
  return { watch: 'running', channel: CHANNEL,
    started: new Date(SEC.started).toISOString(),
    uptime_s: Math.round((Date.now() - SEC.started) / 1000),
    short_arm_ms: SHORT_MS, long_arm_ms: LONG_MS,
    last_short_tick: SEC.lastShort, last_long_tick: SEC.lastLong,
    requests_total: SEC.reqTotal, errors_total: SEC.errTotal,
    firewall: { egress_target: FW_HOST + ':' + FW_PORT, egress_ok: SEC.egress_ok,
      egress_ms: SEC.egress_ms, checked: SEC.egress_checked },
    fw_ok: SEC.egress_ok !== false,
    recent_alerts: SEC.alerts.slice(-10), log_file: path.basename(secLogFile()) };
}
function secTail(n) {
  try {
    const lines = fs.readFileSync(secLogFile(), 'utf8').trim().split('\n');
    return lines.slice(-Math.max(1, Math.min(500, n || 50)))
      .map(l => { try { return JSON.parse(l); } catch (e) { return { raw: l }; } });
  } catch (e) { return []; }
}

/* ---- delivery capacity estimate (bitrate x users vs server uplink) ---- */
function estimate(q) {
  const bitrate = Math.max(0.05, parseFloat(q.bitrate) || 1.5);   // Mbps per stream
  const users = Math.max(1, parseInt(q.users, 10) || 30000);
  const serverGbps = Math.max(0.1, parseFloat(q.server) || 10);
  const serverMbps = serverGbps * 1000;
  const demandMbps = bitrate * users;
  const cap = Math.floor(serverMbps / bitrate);
  const coverage = Math.min(100, cap / users * 100);
  const serversNeeded = Math.ceil(demandMbps / serverMbps);
  const access = [
    { name: 'Fixed line (fibre)', downlink_mbps: 500, note: 'typical' },
    { name: 'Mobile Wi-Fi (4G)', downlink_mbps: 40, note: 'typical' },
    { name: '5G', downlink_mbps: 300, note: 'typical' }
  ].map(a => ({ ...a, streams_sustained: Math.floor(a.downlink_mbps / bitrate), fits_stream: a.downlink_mbps >= bitrate }));
  return {
    inputs: { bitrate_mbps: bitrate, users, server_gbps: serverGbps },
    total_demand_mbps: +demandMbps.toFixed(1),
    total_demand_gbps: +(demandMbps / 1000).toFixed(2),
    streams_one_server_serves: cap,
    coverage_percent: +coverage.toFixed(1),
    servers_needed_for_all: serversNeeded,
    access_comparison: access
  };
}

/* ---- server's own health (all real, from the OS) ---- */
function health() {
  const mem = process.memoryUsage();
  return {
    ok: true,
    channel: CHANNEL,
    version: VERSION,
    host: os.hostname(),
    platform: os.platform() + ' ' + os.arch(),
    node: process.version,
    cpu_cores: os.cpus().length,
    load_avg: os.loadavg().map(n => +n.toFixed(2)),          // 1/5/15 min (0 on Windows)
    mem_total_mb: +(os.totalmem() / 1048576).toFixed(0),
    mem_free_mb: +(os.freemem() / 1048576).toFixed(0),
    rss_mb: +(mem.rss / 1048576).toFixed(1),
    heap_used_mb: +(mem.heapUsed / 1048576).toFixed(1),
    uptime_s: +process.uptime().toFixed(0),
    security: { watch: 'running', fw_ok: SEC.egress_ok !== false, alerts: SEC.alerts.length },
    time: new Date().toISOString()
  };
}

function json(res, code, obj) {
  const body = JSON.stringify(obj, null, 2);
  res.writeHead(code, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve) => {
    let b = '';
    req.on('data', c => { b += c; if (b.length > 1e6) req.destroy(); });   // 1 MB cap
    req.on('end', () => { try { resolve(b ? JSON.parse(b) : {}); } catch (e) { resolve({}); } });
    req.on('error', () => resolve({}));
  });
}

const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css',
  '.json': 'application/json', '.webmanifest': 'application/manifest+json', '.png': 'image/png',
  '.bin': 'application/octet-stream', '.switchc': 'application/x-6ggw-switch' };

function serveStatic(req, res, pathname) {
  let rel = pathname === '/' ? '/index.html' : pathname;
  const file = path.normalize(path.join(CLIENT_DIR, rel));
  if (!file.startsWith(CLIENT_DIR)) { res.writeHead(403); return res.end('forbidden'); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404, { 'Content-Type': 'text/plain' }); return res.end('not found'); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
}

const srv = http.createServer(async (req, res) => {
  const u = url.parse(req.url, true);
  const _t0 = Date.now();
  const _ip = (req.socket && req.socket.remoteAddress) || '?';
  res.on('finish', () => secObserve(req, res.statusCode, Date.now() - _t0, _ip));
  try {
    if (u.pathname === '/api/health') return json(res, 200, health());
    if (u.pathname === '/api/ping')   return json(res, 200, { pong: true, t: Date.now() });
    if (u.pathname === '/api/estimate') return json(res, 200, estimate(u.query));
    if (u.pathname === '/api/distance') {
      const host = (u.query.host || '').trim();
      if (!host) return json(res, 400, { error: 'pass ?host=example.com' });
      const port = parseInt(u.query.port, 10) || 443;
      const r = await measureDistance(host, port);
      return json(res, 200, r);
    }
    if (u.pathname === '/api/device/health' && req.method === 'POST') {
      const body = await readBody(req);
      const id = body.id || body.device_id;
      if (!id) return json(res, 400, { error: 'pass a device id in the JSON body: {"id":"...", ...stats}' });
      return json(res, 200, saveHealth(id, body));
    }
    if (u.pathname === '/api/device/history') {
      const id = (u.query.id || '').trim();
      if (!id) return json(res, 400, { error: 'pass ?id=deviceId' });
      return json(res, 200, { id: safeId(id), history: readHistory(id) });
    }
    if (u.pathname === '/api/devices') return json(res, 200, { channel: CHANNEL, devices: listDevices() });
    if (u.pathname === '/api/backup' && req.method === 'POST') return json(res, 200, makeBackup());
    if (u.pathname === '/api/backups') return json(res, 200, { channel: CHANNEL, dir: BACKUP_DIR, backups: listBackups() });
    if (u.pathname === '/api/security') return json(res, 200, secStatus());
    if (u.pathname === '/api/security/log') return json(res, 200, { log: path.basename(secLogFile()), lines: secTail(parseInt(u.query.n, 10) || 50) });
    return serveStatic(req, res, u.pathname);
  } catch (e) {
    return json(res, 502, { error: String(e && e.message || e) });
  }
});

srv.listen(PORT, () => {
  console.log('6GGW server component  v' + VERSION + '  [channel: ' + CHANNEL + ']  listening on :' + PORT);
  console.log('  GET  /api/health');
  console.log('  GET  /api/ping');
  console.log('  GET  /api/distance?host=bbc.co.uk');
  console.log('  GET  /api/estimate?users=30000&bitrate=1.5&server=10');
  console.log('  POST /api/device/health      {"id":"phone-01", ...stats}   (per-device encrypted DB)');
  console.log('  GET  /api/device/history?id=phone-01');
  console.log('  GET  /api/devices');
  console.log('  POST /api/backup             (bundle all device DBs -> data/backups/*.6ggwbak.gz, for tape/LTO)');
  console.log('  GET  /api/backups');
  console.log('  GET  /api/security           (round-the-clock watch status + firewall self-test)');
  console.log('  GET  /api/security/log?n=50');
  console.log('  GET  /                        (serves the bundled 6GGW client)');
  if (CHANNEL !== 'prod' && (process.env.DB_KEY || '') === '')
    console.log('  note: using the dev DB key. Set DB_KEY=... for rc/prod.');
  // start the security clock: immediate firewall/egress sweep, then the two hands tick round the clock
  secLog('INFO', 'watch_start', { port: PORT, short_arm_ms: SHORT_MS, long_arm_ms: LONG_MS, fw_target: FW_HOST + ':' + FW_PORT });
  secLongTick().catch(() => {});
  setInterval(secShortTick, SHORT_MS).unref();
  setInterval(function () { secLongTick().catch(() => {}); }, LONG_MS).unref();
  console.log('  security watch: ON  (short arm ' + (SHORT_MS / 1000) + 's, long arm ' + (LONG_MS / 1000) + 's, fw probe ' + FW_HOST + ':' + FW_PORT + ')');
});
