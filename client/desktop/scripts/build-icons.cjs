const { app, nativeImage } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

app.whenReady().then(() => {
  const root = path.resolve(__dirname, '..');
  const master = nativeImage.createFromPath(path.join(root, 'artwork/flim-logo-master.png'));
  if (master.isEmpty()) throw new Error('The logo master could not be read.');
  const size = master.getSize();
  if (size.width !== size.height || size.width < 512) throw new Error('The logo master must be square and at least 512px.');
  const output = path.join(root, 'public/brand');
  fs.mkdirSync(output, { recursive: true });
  const resize = width => master.resize({ width, height: width, quality: 'best' }).toPNG();
  fs.writeFileSync(path.join(output, 'flim-logo.png'), resize(256));

  const sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256];
  const frames = sizes.map(resize);
  const directory = Buffer.alloc(6 + 16 * sizes.length);
  directory.writeUInt16LE(1, 2);
  directory.writeUInt16LE(sizes.length, 4);
  let offset = directory.length;
  sizes.forEach((width, index) => {
    const entry = 6 + 16 * index;
    directory[entry] = width === 256 ? 0 : width;
    directory[entry + 1] = width === 256 ? 0 : width;
    directory.writeUInt16LE(1, entry + 4);
    directory.writeUInt16LE(32, entry + 6);
    directory.writeUInt32LE(frames[index].length, entry + 8);
    directory.writeUInt32LE(offset, entry + 12);
    offset += frames[index].length;
  });
  fs.writeFileSync(path.join(output, 'flim.ico'), Buffer.concat([directory, ...frames]));
  console.log(`Saved transparent PNG and Windows ICO (${sizes.join(', ')}px).`);
  app.quit();
}).catch(error => { console.error(error); app.exit(1); });
