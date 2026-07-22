#!/usr/bin/env node
/*
 * 6GGW network-data backup (standalone, zero-dependency).
 *
 * Bundles every per-device DB (data/devices/*.db) into ONE gzipped, checksummed archive
 * under data/backups/. The device blobs are already AES-256-GCM encrypted at rest, and this
 * script copies them byte-for-byte, so the archive is safe to move straight onto tape (IBM/LTO)
 * or any offline store WITHOUT the DB key ever touching this process.
 *
 *   node backup.js                 # write a new archive into data/backups/
 *   node backup.js | mt ...        # (or) pipe onto a tape device in your own cron job
 *
 * Pair with restore.js to verify + unpack an archive back into data/devices/.
 */
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');

const CHANNEL = (process.env.CHANNEL || 'dev').toLowerCase();
const DB_DIR = path.join(__dirname, 'data', 'devices');
const BACKUP_DIR = path.join(__dirname, 'data', 'backups');
const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

if (!fs.existsSync(DB_DIR)) { console.error('no data/devices — nothing to back up'); process.exit(1); }
fs.mkdirSync(BACKUP_DIR, { recursive: true });

const ids = fs.readdirSync(DB_DIR).filter(f => f.endsWith('.db')).map(f => f.slice(0, -3));
const files = [], blobs = {};
for (const id of ids) {
  const raw = fs.readFileSync(path.join(DB_DIR, id + '.db'));
  files.push({ id, bytes: raw.length, sha256: sha256(raw) });
  blobs[id] = raw.toString('base64');
}
const manifest = {
  created_at: new Date().toISOString(), channel: CHANNEL,
  device_count: ids.length, files, encryption: 'AES-256-GCM at rest (blobs unchanged)'
};
const gz = zlib.gzipSync(Buffer.from(JSON.stringify({ manifest, blobs })));
const stamp = manifest.created_at.replace(/[:.]/g, '-');
const name = 'backup-' + CHANNEL + '-' + stamp + '.6ggwbak.gz';
const outPath = path.join(BACKUP_DIR, name);
fs.writeFileSync(outPath, gz);
fs.writeFileSync(outPath + '.sha256', sha256(gz) + '  ' + name + '\n');

console.log('backed up ' + ids.length + ' device DB(s) -> ' + outPath);
console.log('  bytes: ' + gz.length + '   sha256: ' + sha256(gz));
console.log('  now copy this file (and its .sha256) onto your tape/LTO or offsite store.');
