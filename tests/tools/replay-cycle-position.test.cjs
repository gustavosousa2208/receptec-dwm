'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { replay, parseRecord } = require('../../tools/replay-cycle-position.cjs');
function rows(round, time, status = 0) {
  return [5000,5000,6000].map((distance_mm,pair)=>JSON.stringify({round,pair,distance_mm,status,source:0,age_ms:0,elapsed_s:time}));
}
test('replay retains uint64 identities and excludes initial snapshots and duplicates',()=>{
  const source='{"round":18446744073709551615}';
  assert.equal(parseRecord(source).round,'18446744073709551615');
  const tuple=rows('9007199254740993',1);
  const output=replay(['{"initial":true}',...tuple,...tuple]);
  assert.equal(output.initialRecords,1);
  assert.equal(output.positions.length,1);
  assert.equal(output.positions[0].round,'9007199254740993');
  assert.equal(output.positions[0].xMm,3000);
  assert.equal(output.positions[0].yMm,4000);
});
test('failed tuples hold old position and cannot enter the mean',()=>{
  const output=replay([...rows('1',1),...rows('2',2,1)]);
  assert.equal(output.positions.length,1);
  assert.equal(output.finalState.mode,'held');
  assert.equal(output.finalState.diagnostic.reason,'failed-range');
});
test('out-of-order elapsed time is rejected rather than inventing motion',()=>{
  assert.throws(()=>replay([...rows('1',2),...rows('2',1)]),/moves backwards/);
});
