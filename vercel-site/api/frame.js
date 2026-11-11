// POST = ESP32 uploads a JPEG frame.  GET = browser fetches the latest frame.
const { redis, getCtrl, viewerOk, deviceOk, readRaw } = require('./_lib');

module.exports = async (req, res) => {
  try {
    res.setHeader('Cache-Control', 'no-store');

    if (req.method === 'POST') {
      if (!deviceOk(req)) return res.status(401).json({ error: 'bad device key' });
      const buf = await readRaw(req);
      if (!buf.length) return res.status(400).json({ error: 'empty body' });

      await redis.set('frame', buf.toString('base64'), { ex: 20 });
      await redis.set('lastseen', Date.now(), { ex: 15 });
      const [ctrl, viewer] = await Promise.all([getCtrl(), redis.get('viewer')]);
      return res.status(200).json({ ...ctrl, watch: !!viewer });
    }

    if (req.method === 'GET') {
      if (!viewerOk(req)) return res.status(401).json({ error: 'locked' });
      // Tell the camera someone is watching (flag expires after 10 s).
      await redis.set('viewer', 1, { ex: 10 });
      const b64 = await redis.get('frame');
      if (!b64 || typeof b64 !== 'string') return res.status(404).end();
      res.setHeader('Content-Type', 'image/jpeg');
      return res.status(200).send(Buffer.from(b64, 'base64'));
    }

    res.setHeader('Allow', 'GET, POST');
    return res.status(405).end();
  } catch (e) {
    return res.status(500).json({ error: String(e && e.message ? e.message : e) });
  }
};
