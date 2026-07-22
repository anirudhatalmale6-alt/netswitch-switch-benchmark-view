#!/usr/bin/env node
/*
 * 6GGW network-data restore (standalone, zero-dependency).
 *
 *   node restore.js data/backups/backup-....6ggwbak.gz            # verify + restore
 *   node restore.js data/backups/backup-....6ggwbak.gz --verify   # verify only, do not write
 *
 * Verifies the archive checksum and every per-device sha256 before writing anything back into
 * data/devices/. Blobs are restored byte-for-byte (still AES-256-GCM encrypted); the running
 * server decrypts them with its DB_KEY as usual. Existing device files are backed up to *.db.bak
 * before being overwritten.
 */
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const arg = process.argv[2];
const verifyOnly = process.argv.includes('--verify');
if (!arg) { console.error('usage: node restore.js <backup.6ggwbak.gz> [--verify]'); process.exit(1); }

const gz = fs.readFileSync(arg);
const sidecar = arg + '.sha256';
if (fs.existsSync(sidecar)) {
  const want = fs.readFileSync(sidecar, 'utf8').trim().split(/\s+/)[0];
  const got = sha256(gz);
  if (want !== got) { console.error('ARCHIVE CHECKSUM MISMATCH\n  want ' + want + '\n  got  ' + got); process.exit(2); }
  console.log('archive sha256 OK (' + got.slice(0, 16) + '...)');
}

const bundle = JSON.parse(zlib.gunzipSync(gz).toString());
const { manifest, blobs } = bundle;
console.log('archive: ' + manifest.created_at + '  channel=' + manifest.channel + '  devices=' + manifest.device_count);

let ok = 0, bad = 0;
for (const f of manifest.files) {
  const raw = Buffer.from(blobs[f.id] || '', 'base64');
  if (sha256(raw) === f.sha256 && raw.length === f.bytes) ok++;
  else { bad++; console.error('  BAD blob: ' + f.id); }
}
console.log('verified ' + ok + '/' + manifest.files.length + ' device blobs' + (bad ? ' (' + bad + ' bad)' : ''));
if (bad) process.exit(3);
if (verifyOnly) { console.log('--verify: not writing.'); process.exit(0); }

const DB_DIR = path.join(__dirname, 'data', 'devices');
fs.mkdirSync(DB_DIR, { recursive: true });
for (const f of manifest.files) {
  const dest = path.join(DB_DIR, f.id + '.db');
  if (fs.existsSync(dest)) fs.copyFileSync(dest, dest + '.bak');
  fs.writeFileSync(dest, Buffer.from(blobs[f.id], 'base64'));
}
console.log('restored ' + manifest.files.length + ' device DB(s) into ' + DB_DIR + ' (prior files kept as *.db.bak)');
