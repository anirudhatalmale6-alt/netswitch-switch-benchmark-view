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
 *   /                      — serves the bundled 6GGW client (the PWA) so one box ships both halves
 *
 * The TCP-handshake timing is a genuine network measurement: it opens a real socket to the
 * target and times the SYN/SYN-ACK round trip. It is the honest, portable stand-in for a raw
 * ICMP traceroute (which needs root and is blocked in most hosting), and it is really measured.
 */
'use strict';
const http = require('http');
const net = require('net');
const dns = require('dns');
const os = require('os');
const fs = require('fs');
const path = require('path');
const url = require('url');

const PORT = process.env.PORT || 8090;
const SIGNAL_CONST = 0.0001607;            // metres per picosecond (client's constant; ~160,700 km/s cable)
const CLIENT_DIR = path.join(__dirname, '..');   // the PWA lives one level up (index.html, sw.js, ...)

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
    time: new Date().toISOString()
  };
}

function json(res, code, obj) {
  const body = JSON.stringify(obj, null, 2);
  res.writeHead(code, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
  res.end(body);
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
    return serveStatic(req, res, u.pathname);
  } catch (e) {
    return json(res, 502, { error: String(e && e.message || e) });
  }
});

srv.listen(PORT, () => {
  console.log('6GGW server component listening on :' + PORT);
  console.log('  GET /api/health');
  console.log('  GET /api/ping');
  console.log('  GET /api/distance?host=bbc.co.uk');
  console.log('  GET /api/estimate?users=30000&bitrate=1.5&server=10');
  console.log('  GET /            (serves the bundled 6GGW client)');
});
