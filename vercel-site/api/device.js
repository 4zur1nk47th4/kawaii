// ESP32 polls this while nobody is watching (cheap heartbeat + settings).
const { redis, getCtrl, deviceOk } = require('./_lib');

module.exports = async (req, res) => {
  try {
    res.setHeader('Cache-Control', 'no-store');
    if (!deviceOk(req)) return res.status(401).json({ error: 'bad device key' });
    await redis.set('lastseen', Date.now(), { ex: 15 });
    const [ctrl, viewer] = await Promise.all([getCtrl(), redis.get('viewer')]);
    return res.status(200).json({ ...ctrl, watch: !!viewer });
  } catch (e) {
    return res.status(500).json({ error: String(e && e.message ? e.message : e) });
  }
};
