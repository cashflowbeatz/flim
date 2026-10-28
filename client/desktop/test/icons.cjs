const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { NtExecutable, NtExecutableResource, Data, Resource } = require('resedit');

module.exports = function verifyExecutableIcons(executable) {
  const expected = Data.IconFile.from(fs.readFileSync(path.join(__dirname, '../public/brand/flim.ico')));
  const pe = NtExecutable.from(fs.readFileSync(executable), { ignoreCert: true });
  const resources = NtExecutableResource.from(pe);
  const groups = Resource.IconGroupEntry.fromEntries(resources.entries);
  assert.ok(groups.length > 0, 'The executable must contain a Windows icon group.');
  const actual = groups[0].getIconItemsFromEntries(resources.entries);
  const sizes = [];
  for (const icon of expected.icons) {
    const width = icon.width || icon.data.width;
    const found = actual.find(item => (item.width || 256) === width && (item.height || 256) === width);
    assert.ok(found, `Missing ${width}px icon in ${path.basename(executable)}`);
    assert.deepEqual(Buffer.from(found.bin), Buffer.from(icon.data.bin), `${width}px embedded icon differs from the app logo.`);
    sizes.push(width);
  }
  return { executable: path.basename(executable), sizes };
};
