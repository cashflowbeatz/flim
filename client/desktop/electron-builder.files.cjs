'use strict';

module.exports = {
  ...require('./package.json').build,
  appId: 'com.cashflowbeatz.flim.files-preview',
  productName: 'flim Files Preview',
  extraMetadata: {name: 'flim-files-preview', productName: 'flim Files Preview'},
  directories: {output: 'release-files'},
  files: [
    {from: 'dist-files', to: 'dist', filter: ['**/*']},
    'electron/*.cjs', 'electron/recovery.html', 'electron/recovery.js', 'package.json',
  ],
  extraResources: [{from: '../out-file-sync', to: 'native', filter: ['flim.exe', 'flim_engine.dll', 'profiles/*.profile']}],
  artifactName: 'flim-rooms-${version}-files-preview-win64.${ext}',
};
