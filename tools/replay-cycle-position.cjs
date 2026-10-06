#!/usr/bin/env node
'use strict';
// Replay saved BLE publication ages; these are not RF acquisition timestamps.
const fs = require('node:fs');
const { createProcessor } = require('./uwb-position.js');
function parseRecord(line) {
  return JSON.parse(line, (key, value, context) => {
    if (key !== 'round' || typeof value !== 'number') return value;
    if (context && context.source) return context.source;
    if (!Number.isSafeInteger(value)) throw new Error('Unsafe round ID: use JSON strings or a Node version with JSON parse source context');
    return String(value);
  });
}
function replay(lines, settings = {}) {
  const processor = createProcessor(settings);
  let now = 0, notifications = 0, initialRecords = 0, accepted = 0;
  const positions = [];
  for (const line of lines) {
    if (!line.trim()) continue;
    const row = parseRecord(line);
    if (row.initial) { initialRecords++; continue; }
    if (row.error) throw new Error(`Invalid captured payload: ${row.error}`);
    const time = row.elapsed_s * 1000;
    if (!Number.isFinite(time) || time < now) throw new Error('Capture time is missing or moves backwards');
    now = time; notifications++;
    const state = processor.ingest({ round: row.round, pair: row.pair, mm: row.distance_mm,
      source: row.source, status: row.status, age: row.age_ms }, now);
    if (state.counters.accepted > accepted) {
      positions.push({ round: state.round, receivedMs: now, timestampMs: state.timestampMs,
        sampleTimestampsMs: state.sampleTimestampsMs, ...state.position });
      accepted = state.counters.accepted;
    }
  }
  return { notifications, initialRecords, settings: processor.configuration(),
    finalState: processor.state(now), positions,
    limitations: 'Replay uses BLE publication age, not RF acquisition time. Position acceptance does not prove physical accuracy. Initial reads are excluded; duplicate tuples cannot grow the averaging window.' };
}
module.exports = { parseRecord, replay };
if (require.main === module) {
  try {
    const [input, settingsJson] = process.argv.slice(2);
    if (!input) throw new Error('Usage: node tools/replay-cycle-position.cjs <records.jsonl> [settings JSON]');
    const result = replay(fs.readFileSync(input, 'utf8').split('\n'), settingsJson ? JSON.parse(settingsJson) : {});
    process.stdout.write(JSON.stringify(result, null, 2) + '\n');
  } catch (error) { console.error(error.message); process.exitCode = 2; }
}
