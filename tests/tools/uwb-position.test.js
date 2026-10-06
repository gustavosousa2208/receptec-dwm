'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { createProcessor } = require('../../tools/uwb-position.js');
function tuple(p, round, t, x = 300, y = 400, extra = {}) {
  const distances = [Math.hypot(x, y), Math.hypot(x - 1000, y), 1000];
  let result;
  distances.forEach((mm, pair) => { result = p.ingest({ round: String(round), pair, mm, source: 0, status: 0, age: 0, ...extra }, t); });
  return result;
}
test('coherent tuple, fixed mirror and 64-bit rounds', () => {
  const p = createProcessor({ halfPlane: -1, windowSize: 1, motionTauMs: 0 });
  const a = tuple(p, '9007199254740993', 1000);
  assert.equal(a.valid, true); assert.equal(a.round, '9007199254740993');
  assert.ok(Math.abs(a.position.xMm - 300) < 1e-9);
  assert.ok(Math.abs(a.position.yMm + 400) < 1e-9);
  assert.equal(p.ingest({ pair: 0, round: '9007199254740992' }, 1001).counters.accepted, 1);
});
test('never mixes rounds and duplicate records do not add samples', () => {
  const p = createProcessor();
  p.ingest({ pair: 0, round: '1', mm: 500, source: 0, status: 0, age: 0 }, 1000);
  p.ingest({ pair: 1, round: '2', mm: 500, source: 0, status: 0, age: 0 }, 1000);
  assert.equal(p.state(1000).position, null);
  tuple(p, 3, 1000); tuple(p, 3, 1000);
  assert.equal(p.state(1000).counters.accepted, 1);
});
test('calibration defaults to zero and is additive per pair', () => {
  const p = createProcessor();
  assert.deepEqual(p.configuration().offsetsMm, [0, 0, 0]);
  p.configure({ offsetsMm: [-50, -50, -50], windowSize: 1, motionTauMs: 0 });
  [500, Math.hypot(700, 400), 1000].forEach((mm, pair) => p.ingest({ round: '1', pair, mm: mm + 50, source: 0, status: 0, age: 0 }, 1000));
  assert.ok(Math.abs(p.state(1000).position.xMm - 300) < 1e-9);
});
test('impossible geometry is rejected before averaging and retains the point', () => {
  const p = createProcessor({ motionTauMs: 0 }); const first = tuple(p, 1, 1000);
  [211, 305, 704].forEach((mm, pair) => p.ingest({ round: '2', pair, mm, source: 0, status: 0, age: 0 }, 2000));
  const bad = p.state(2000);
  assert.equal(bad.diagnostic.reason, 'triangle'); assert.deepEqual(bad.position, first.position);
  const next = tuple(p, 3, 3000);
  assert.deepEqual(next.position, first.position); assert.equal(next.counters.accepted, 2);
});
test('failed, missing and old samples hold visibly without manufacturing a measurement', () => {
  const p = createProcessor({ maxAgeMs: 1000, holdMs: 2000 }); const first = tuple(p, 1, 1000);
  const failed = tuple(p, 2, 1500, 300, 400, { status: 1 });
  assert.equal(failed.valid, false); assert.equal(failed.mode, 'held');
  assert.deepEqual(failed.position, first.position);
  assert.equal(p.state(2500).stale, true); assert.equal(p.state(3500).mode, 'stale');
  assert.deepEqual(p.state(3500).position, first.position);
  assert.equal(p.state(3500).timestampMs, 1000);
});
test('rejects out-of-range, unknown age, stale and skewed samples', () => {
  for (const [extra, reason] of [[{ mm: 0 }, 'range-bounds'], [{ age: null }, 'unknown-age'], [{ age: 4000 }, 'stale-range']]) {
    const p = createProcessor(); assert.equal(tuple(p, 1, 5000, 300, 400, extra).diagnostic.reason, reason);
  }
  const p = createProcessor({ maxSkewMs: 10 });
  [500, Math.hypot(700, 400), 1000].forEach((mm, pair) => p.ingest({ round: '1', pair, mm, status: 0, source: 0, age: pair * 20 }, 1000));
  assert.equal(p.state(1000).diagnostic.reason, 'sample-skew');
});
test('velocity gate acts on raw position before smoothing', () => {
  const p = createProcessor({ maxSpeedMmS: 100, jumpAllowanceMm: 0 }); tuple(p, 1, 1000);
  const bad = tuple(p, 2, 2000, 600, 400);
  assert.equal(bad.diagnostic.reason, 'velocity');
  const recovered = tuple(p, 3, 3000, 350, 400);
  assert.equal(recovered.valid, true); assert.equal(recovered.counters.accepted, 2);
});
test('short averaging and exponential motion filtering use elapsed time', () => {
  const avg = createProcessor({ windowSize: 2, motionTauMs: 0 });
  tuple(avg, 1, 1000); assert.ok(Math.abs(tuple(avg, 2, 2000, 500).position.xMm - 400) < 1e-9);
  const p = createProcessor({ windowSize: 1, motionTauMs: 1000 }); tuple(p, 1, 1000);
  const a = tuple(p, 2, 2000, 500);
  assert.ok(Math.abs(a.position.xMm - (300 + 200 * (1 - Math.exp(-1)))) < 1e-9);
  const b = tuple(p, 3, 4000, 500);
  assert.ok(Math.abs(b.position.xMm - (a.position.xMm + (500 - a.position.xMm) * (1 - Math.exp(-2)))) < 1e-9);
});
test('configuration validation and copy isolation', () => {
  assert.throws(() => createProcessor({ windowSize: 0 }));
  assert.throws(() => createProcessor({ offsetsMm: [0, NaN, 0] }));
  assert.throws(() => createProcessor({ halfPlane: 0 }));
  const p = createProcessor(); const c = p.configuration(); c.offsetsMm[0] = 99;
  assert.equal(p.configuration().offsetsMm[0], 0);
});

test('outage recovery does not average old coordinates into the new point', () => {
  const p = createProcessor({ windowSize: 3, motionTauMs: 0, maxAgeMs: 1000 });
  tuple(p, 1, 1000);
  assert.ok(Math.abs(tuple(p, 2, 4000, 600).position.xMm - 600) < 1e-9);
});
test('calibration cannot convert a raw zero failure into a valid distance', () => {
  const p = createProcessor({ offsetsMm: [1000, 1000, 1000] });
  assert.equal(tuple(p, 1, 1000, 300, 400, { mm: 0 }).diagnostic.reason, 'range-bounds');
});

test('malformed records and lossy numeric round IDs are rejected', () => {
  const p = createProcessor();
  assert.equal(p.ingest(null, 1000).diagnostic.reason, 'invalid-record');
  assert.equal(p.ingest({ round: 9007199254740992 }, 1000).diagnostic.reason, 'invalid-round');
});

test('constant provenance is reserved for M-A and rejects before smoothing', () => {
  const p = createProcessor({ windowSize: 1, motionTauMs: 0 });
  const first = tuple(p, 1, 1000);
  const distances = [500, Math.hypot(700, 400), 1000];
  for (const badPair of [0, 1]) {
    const round = badPair + 2;
    distances.forEach((mm, pair) => p.ingest({ round: String(round), pair, mm,
      source: pair === badPair ? 1 : 0, status: 0, age: 0 }, 1000 + round * 500));
    const state = p.state(1000 + round * 500);
    assert.equal(state.diagnostic.reason, 'invalid-source');
    assert.deepEqual(state.position, first.position);
    assert.equal(state.counters.accepted, 1);
  }
  distances.forEach((mm, pair) => p.ingest({ round: '4', pair, mm,
    source: pair === 2 ? 1 : 0, status: 0, age: 0 }, 3000));
  assert.equal(p.state(3000).valid, true);
});

test('calibration cannot turn UINT32_MAX invalid distance into a position', () => {
  const p = createProcessor({ offsetsMm: [500 - 0xFFFFFFFF, 0, 0] });
  [0xFFFFFFFF, Math.hypot(700, 400), 1000].forEach((mm, pair) =>
    p.ingest({ round: '1', pair, mm, source: 0, status: 0, age: 0 }, 1000));
  assert.equal(p.state(1000).diagnostic.reason, 'range-bounds');
  assert.equal(p.state(1000).position, null);
});

test('moving tag remains stable across supported rates, an outlier and a lost round', () => {
  for (const period of [250, 300, 500, 1000]) {
    const p = createProcessor({ maxSpeedMmS: 200, jumpAllowanceMm: 0 });
    let last = null;
    for (let n = 0; n <= Math.ceil(10000 / period); n++) {
      const t = 1000 + n * period, x = 300 + n * period * 0.1;
      const s = n === 5 ? tuple(p, n + 1, t, x + 5000, 400)
        : tuple(p, n + 1, t, x, 400, n === 8 ? { status: 1 } : {});
      if (n === 5 || n === 8) {
        assert.equal(s.valid, false);
        assert.deepEqual(s.position, last.position);
        assert.equal(s.diagnostic.reason, n === 5 ? 'velocity' : 'failed-range');
      } else {
        assert.equal(s.valid, true);
        assert.ok(s.position.yMm > 0);
        if (last) assert.ok(s.position.xMm >= last.position.xMm);
        const lag = x - s.position.xMm;
        assert.ok(lag >= -1e-6 && lag < 300, `period=${period}, lag=${lag}`);
      }
      last = s;
    }
    assert.equal(last.counters.rejected, 2);
  }
});
