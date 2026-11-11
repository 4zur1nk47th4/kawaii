// Shared helpers for the API routes (files starting with "_" are not exposed as routes).
const { Redis } = require('@upstash/redis');

// Vercel's Upstash integration sets KV_REST_API_*; a manual Upstash setup uses UPSTASH_REDIS_REST_*.
const redis = new Redis({
  url: process.env.UPSTASH_REDIS_REST_URL || process.env.KV_REST_API_URL,
  token: process.env.UPSTASH_REDIS_REST_TOKEN || process.env.KV_REST_API_TOKEN,
});

// framesize values are esp32-camera enum numbers: 5 = QVGA, 8 = VGA, 9 = SVGA
const DEFAULT_CTRL = { light: false, framesize: 5, brightness: 0, contrast: 0 };

async function getCtrl() {
  const saved = await redis.get('ctrl');
  return { ...DEFAULT_CTRL, ...(saved && typeof saved === 'object' ? saved : {}) };
}

// Website visitors: optional ACCESS_KEY env var. If unset, the page is open to anyone.
function viewerOk(req) {
  const need = process.env.ACCESS_KEY;
  if (!need) return true;
  return (req.query && req.query.k) === need;
}

// The ESP32 must send header "x-device-key" equal to the DEVICE_KEY env var.
function deviceOk(req) {
  const need = process.env.DEVICE_KEY;
  return !!need && req.headers['x-device-key'] === need;
}

async function readRaw(req) {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  return Buffer.concat(chunks);
}

module.exports = { redis, getCtrl, viewerOk, deviceOk, readRaw, DEFAULT_CTRL };
