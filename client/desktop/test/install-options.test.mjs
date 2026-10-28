import test from 'node:test';
import assert from 'node:assert/strict';
import {installOptions} from '../src/utils/installOptions.js';

const install = (id, version, extra = {}) => ({id, version, path: `D:\\Music apps\\${id}`, supported: true, reason: '', ...extra});
const installs = [install('26-new', '26.2.0.1', {supported: false}), install('25', '25.2.5.5319'),
  install('26', '26.1.7.5654'), install('26-old', '26.1.6.5000'), install('24', '24.2.2.4597', {experimental: false})];

test('one major-only label per family, with reviewed builds preferred over unknown updates', () => {
  const options = installOptions(installs, '25');
  assert.deepEqual(options.map(item => item.label), ['FL Studio 26', 'FL Studio 25', 'FL Studio 24']);
  assert.deepEqual(options.map(item => item.id), ['26', '25', '24']);
  assert.equal(options[2].experimental, false);
  assert.equal(installs.length, 5);
});

test('explicit folder choice stays selected even if unsupported or older', () => {
  for (const id of ['26-new', '26-old']) {
    const option = installOptions(installs, id)[0];
    assert.equal(option.id, id); assert.equal(option.path, installs.find(item => item.id === id).path);
    assert.equal(option.supported, id !== '26-new');
    assert.equal(option.label, 'FL Studio 26');
  }
});

test('numeric version order and path tie-break remain stable across discovery order', () => {
  const copies = [install('b', '26.1.9.100'), install('c', '26.1.10.2'), install('a', '26.1.10.2'),
    install('experimental', '26.1.11.3', {experimental: true})];
  assert.equal(installOptions(copies, '')[0].id, 'a');
  assert.equal(installOptions([...copies].reverse(), '')[0].id, 'a');
  assert.deepEqual(installOptions([], ''), []);
});
