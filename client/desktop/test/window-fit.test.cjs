const {test} = require('node:test');
const assert = require('node:assert/strict');
const {fitBounds} = require('../electron/window-fit.cjs');
const bounds = {x: 100, y: 100, width: 472, height: 600};
const content = [456, 561];
const area = {x: 0, y: 0, width: 1920, height: 1040};

test('window follows the card when opened and collapsed, with stable geometry', () => {
  const closed = fitBounds(420.2, bounds, content, area);
  assert.deepEqual(closed, {...bounds, height: 460});
  const open = fitBounds(800, closed, [456, 421], area);
  assert.equal(open.height, 839);
  assert.deepEqual(fitBounds(800, open, [456, 800], area), open);
  assert.equal(fitBounds(420.2, open, [456, 800], area).height, 460);
});
test('oversized room and zoom fit the monitor while preserving vertical scrolling', () => {
  const fit = fitBounds(1700, bounds, content, area, 2);
  assert.equal(fit.width, 928); assert.equal(fit.height, 1016);
  assert.equal(fit.y, 24);
  const normal = fitBounds(420, fit, [912, 977], area, 1);
  assert.equal(normal.width, 472); assert.equal(normal.height, 459);
});
test('negative monitor coordinates and screen edges remain reachable', () => {
  const left = {x: -1280, y: -200, width: 1280, height: 720};
  const fit = fitBounds(900, {...bounds, x: -100, y: 400}, content, left);
  assert.equal(fit.x, -472); assert.equal(fit.y, -176);
  assert.equal(fit.height, 696);
  const tiny = fitBounds(50, {...bounds, x: -9000, y: -9000}, content, {x: 10,y: 10,width: 300,height: 250});
  assert.deepEqual(tiny, {x: 10,y: 10,width: 276,height: 226});
});
test('untrusted heights and zoom values cannot change native bounds', () => {
  for (const value of [undefined, null, NaN, Infinity, '600', {}, -1, 0, 8193]) assert.equal(fitBounds(value, bounds, content, area), null);
  for (const zoom of [0, NaN, Infinity, '2', 5.1]) assert.equal(fitBounds(600, bounds, content, area, zoom), null);
});
