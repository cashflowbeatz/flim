const test = require('node:test');
const assert = require('node:assert/strict');
const {createRecoveryBudget} = require('../electron/renderer-recovery.cjs');
const {createSetupGate} = require('../electron/setup.cjs');

test('renderer recovery permits two automatic attempts per minute and then requires a deliberate reload', () => {
  let now = 100_000;
  const recover = createRecoveryBudget(() => now);
  assert.equal(recover(), true); assert.equal(recover(), true); assert.equal(recover(), false);
  now += 59_999; assert.equal(recover(), false);
  now++; assert.equal(recover(), true);
});

test('a backwards clock change cannot permanently lock the recovery screen', () => {
  let now = 100_000;
  const recover = createRecoveryBudget(() => now);
  recover(); recover(); assert.equal(recover(), false);
  now = 1; assert.equal(recover(), true);
});

test('setup can retry after failure and concurrent requests share each attempt', async () => {
  let attempts = 0, release;
  const ready = createSetupGate(async () => {
    attempts++;
    await new Promise(resolve => {release = resolve;});
    if (attempts === 1) throw new Error('temporary setup failure');
  });
  const first = ready(), duplicate = ready();
  assert.equal(first, duplicate);
  await Promise.resolve(); release();
  await assert.rejects(first, /temporary setup/);
  const retry = ready(); assert.equal(retry, ready());
  await Promise.resolve(); release(); await retry;
  await ready(); assert.equal(attempts, 2);
});

test('synchronous setup errors are caught and remain retryable', async () => {
  let fail = true;
  const ready = createSetupGate(() => {if (fail) throw new Error('sync failure');});
  await assert.rejects(ready(), /sync failure/);
  fail = false; await ready();
});
