// Browser <-> server: read camera state, change light / camera settings.
const { redis, getCtrl, viewerOk } = require('./_lib');

const clampInt = (v, lo, hi, fallback) => {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? Math.min(hi, Math.max(lo, n)) : fallback;
};

module.exports = async (req, res) => {
  try {
    res.setHeader('Cache-Control', 'no-store');
    if (!viewerOk(req)) return res.status(401).json({ error: 'locked' });

    if (req.method === 'GET') {
      const [ctrl, lastseen] = await Promise.all([getCtrl(), redis.get('lastseen')]);
      return res.status(200).json({ ctrl, online: !!lastseen });
    }

    if (req.method === 'POST') {
      const body = req.body && typeof req.body === 'object' ? req.body : {};
      const ctrl = await getCtrl();

      if (typeof body.light === 'boolean') ctrl.light = body.light;
      if (body.brightness !== undefined) ctrl.brightness = clampInt(body.brightness, -2, 2, ctrl.brightness);
      if (body.contrast !== undefined) ctrl.contrast = clampInt(body.contrast, -2, 2, ctrl.contrast);
      if (body.framesize !== undefined) {
        const fs = parseInt(body.framesize, 10);
        if ([5, 8, 9].includes(fs)) ctrl.framesize = fs;
      }

      await redis.set('ctrl', ctrl);
      return res.status(200).json({ ctrl });
    }

    res.setHeader('Allow', 'GET, POST');
    return res.status(405).end();
  } catch (e) {
    return res.status(500).json({ error: String(e && e.message ? e.message : e) });
  }
};
