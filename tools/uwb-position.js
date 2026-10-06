/* Browser/CommonJS UWB position processing. Distances and positions use mm,
 * timestamps use one monotonic millisecond clock supplied by the caller.
 * BLE age-derived timestamps are estimates, not exact RF acquisition times. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.UwbPosition = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const defaults = {
    offsetsMm: [0, 0, 0], minRangeMm: 1, maxRangeMm: 100000,
    maxAgeMs: 3000, maxSkewMs: 1000, holdMs: 3000, windowSize: 3,
    motionTauMs: 1000, maxSpeedMmS: 3000, jumpAllowanceMm: 100,
    halfPlane: 1
  };
  function options(input) {
    const c = { ...defaults, ...input, offsetsMm: [...(input.offsetsMm || defaults.offsetsMm)] };
    const nonnegative = ['maxAgeMs', 'maxSkewMs', 'holdMs', 'motionTauMs', 'maxSpeedMmS', 'jumpAllowanceMm'];
    if (c.offsetsMm.length !== 3 || !c.offsetsMm.every(Number.isFinite) ||
        !nonnegative.every(k => Number.isFinite(c[k]) && c[k] >= 0) ||
        !Number.isFinite(c.minRangeMm) || c.minRangeMm <= 0 ||
        !Number.isFinite(c.maxRangeMm) || c.maxRangeMm < c.minRangeMm ||
        !Number.isInteger(c.windowSize) || c.windowSize < 1 || c.windowSize > 100 ||
        ![1, -1].includes(c.halfPlane)) throw new Error('Invalid position configuration');
    return c;
  }
  function createProcessor(input = {}) {
    let config = options(input), pending = new Map(), history = [], last = null;
    let previousRaw = null, latestRound = null, diagnostic = null;
    const counters = { accepted: 0, rejected: 0, reasons: {} };
    function reject(reason, raw, now) {
      counters.rejected++; counters.reasons[reason] = (counters.reasons[reason] || 0) + 1;
      diagnostic = { reason, raw, receivedMs: now };
      return state(now);
    }
    function state(now) {
      if (!Number.isFinite(now)) throw new Error('Monotonic time required');
      const age = last ? Math.max(0, now - last.timestampMs) : null;
      const fresh = last !== null && age <= config.maxAgeMs;
      const held = last !== null && age <= config.holdMs;
      return {
        position: last ? { xMm: last.xMm, yMm: last.yMm } : null,
        round: last ? last.round : null, timestampMs: last ? last.timestampMs : null,
        ageMs: age, valid: fresh && diagnostic === null,
        stale: !fresh, held: held && (!fresh || diagnostic !== null),
        mode: !last ? 'unavailable' : (!fresh || diagnostic !== null ? (held ? 'held' : 'stale') : 'measured'),
        diagnostic, distances: last ? { ...last.distances } : null,
        sampleTimestampsMs: last ? [...last.sampleTimestampsMs] : null,
        counters: { ...counters, reasons: { ...counters.reasons } }
      };
    }
    function ingest(record, now) {
      if (!Number.isFinite(now)) throw new Error('Monotonic time required');
      if (!record || typeof record !== 'object') return reject('invalid-record', record, now);
      if (typeof record.round === 'number' && !Number.isSafeInteger(record.round))
        return reject('invalid-round', record, now);
      let round;
      try { round = BigInt(record.round); } catch (_) { return reject('invalid-round', record, now); }
      if (round <= 0n || !Number.isInteger(record.pair) || record.pair < 0 || record.pair > 2)
        return reject('invalid-record', record, now);
      if (latestRound !== null && round <= latestRound) return state(now);
      const key = round.toString();
      if (!pending.has(key)) pending.set(key, new Map());
      const tuple = pending.get(key);
      if (tuple.has(record.pair)) return state(now); // Identical notifications cannot grow the averaging window.
      tuple.set(record.pair, { ...record, received: now });
      while (pending.size > 16) pending.delete(pending.keys().next().value);
      if (tuple.size !== 3) return state(now);
      latestRound = round;
      for (const id of pending.keys()) if (BigInt(id) <= round) pending.delete(id);
      const raw = [0, 1, 2].map(p => tuple.get(p));
      if (raw.some(r => r.status !== 0 || ![0, 1].includes(r.source))) return reject('failed-range', raw, now);
      if (raw.some(r => r.source === 1 && r.pair !== 2)) return reject('invalid-source', raw, now);
      if (raw.some(r => !Number.isFinite(r.age) || r.age < 0)) return reject('unknown-age', raw, now);
      const timestamps = raw.map(r => r.received - r.age);
      if (timestamps.some(t => now - t > config.maxAgeMs)) return reject('stale-range', raw, now);
      if (Math.max(...timestamps) - Math.min(...timestamps) > config.maxSkewMs) return reject('sample-skew', raw, now);
      const d = raw.map((r, i) => r.mm + config.offsetsMm[i]);
      if (raw.some(r => !Number.isFinite(r.mm) || r.mm <= 0 || r.mm === 0xFFFFFFFF) || d.some(v => !Number.isFinite(v) || v < config.minRangeMm || v > config.maxRangeMm))
        return reject('range-bounds', raw, now);
      const [mt, at, ma] = d;
      if (mt + at < ma || mt + ma < at || at + ma < mt) return reject('triangle', raw, now);
      const x = (mt * mt - at * at + ma * ma) / (2 * ma);
      const y2 = mt * mt - x * x;
      // Only arithmetic roundoff may be clamped; impossible triangles were rejected above.
      if (y2 < -1e-6) return reject('triangle', raw, now);
      const candidate = { xMm: x, yMm: config.halfPlane * Math.sqrt(Math.max(0, y2)) };
      const timestamp = Math.min(...timestamps); // Conservative freshness of the complete tuple.
      if (previousRaw) {
        const dt = timestamp - previousRaw.timestampMs;
        if (dt <= 0) return reject('nonmonotonic-time', raw, now);
        if (Math.hypot(x - previousRaw.xMm, candidate.yMm - previousRaw.yMm) > config.jumpAllowanceMm + config.maxSpeedMmS * dt / 1000)
          return reject('velocity', raw, now);
      }
      previousRaw = { ...candidate, timestampMs: timestamp };
      // A long outage must not mix old averaging samples into recovered data.
      if (last && timestamp - last.timestampMs > config.maxAgeMs) history = [];
      history.push(candidate);
      if (history.length > config.windowSize) history.shift();
      const mean = { xMm: history.reduce((s, p) => s + p.xMm, 0) / history.length,
        yMm: history.reduce((s, p) => s + p.yMm, 0) / history.length };
      const alpha = !last || config.motionTauMs === 0 ? 1 : -Math.expm1(-(timestamp - last.timestampMs) / config.motionTauMs);
      last = { xMm: last ? last.xMm + alpha * (mean.xMm - last.xMm) : mean.xMm,
        yMm: last ? last.yMm + alpha * (mean.yMm - last.yMm) : mean.yMm,
        round: key, timestampMs: timestamp, sampleTimestampsMs: timestamps,
        distances: { mtMm: mt, atMm: at, maMm: ma } };
      diagnostic = null; counters.accepted++;
      return state(now);
    }
    return {
      ingest, state,
      configuration: () => ({ ...config, offsetsMm: [...config.offsetsMm] }),
      configure: (patch) => {
        config = options({ ...config, ...patch });
        // Calibration/half-plane changes invalidate previous coordinates immediately.
        pending.clear(); history = []; last = null; previousRaw = null; diagnostic = null;
      }
    };
  }
  return { createProcessor };
});
