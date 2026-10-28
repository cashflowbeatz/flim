const {_electron} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs/promises');
const {createHash} = require('node:crypto');
const {execFileSync} = require('node:child_process');
const verifyExecutableIcons = require('./icons.cjs');
const installs = require('./install-picker.cjs');
const root = path.resolve(__dirname,'..');
const fl24 = path.resolve(process.env.FLIM_TEST_FL24_DIR || path.join(process.env.ProgramW6432 || process.env.ProgramFiles || 'C:\\Program Files', 'Image-Line', 'FL Studio 2024'));
const reviewedVersions = ['25.2.5.5319', '26.1.7.5654'];

async function localDiagnostics(userData, version, hashes) {
  const directory = path.join(userData, 'logs');
  let metadata, events, session;
  for (let attempt = 0; attempt < 50; attempt++) {
    try {
      const names = await fs.readdir(directory);
      assert.equal(names.length, 1, 'An isolated app start should create one diagnostics session');
      session = names[0];
      metadata = JSON.parse(await fs.readFile(path.join(directory, session, 'session.json'), 'utf8'));
      events = (await fs.readFile(path.join(directory, session, 'events.jsonl'), 'utf8')).trim().split('\n').filter(Boolean).map(line=>JSON.parse(line));
      if (events.some(event=>event.event==='native_files') && [...reviewedVersions, '24.2.2.4597'].every(version=>events.some(event=>event.event==='install_inspected' && event.flVersion===version))) break;
    } catch (error) { if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error; }
    await new Promise(resolve=>setTimeout(resolve, 100));
  }
  assert.equal(metadata?.format, 'flim-diagnostics-v1');
  assert.equal(metadata.appVersion, version); assert.equal(metadata.directory, session);
  assert.ok(Number.isSafeInteger(metadata.appPid) && metadata.appPid > 0);
  assert.deepEqual(metadata.nativePids, []);
  const native = events.find(event=>event.event==='native_files');
  assert.equal(native?.launcherHash, hashes['flim.exe']); assert.equal(native?.engineHash, hashes['flim_engine.dll']);
  assert.ok(events.some(event=>event.event==='app_start' && event.appVersion===version && event.pid===metadata.appPid));
  for (const version of reviewedVersions) assert.ok(events.some(event=>event.event==='install_inspected' && event.flVersion===version && event.supported && !event.experimental));
  assert.ok(events.some(event=>event.event==='install_inspected' && event.flVersion==='24.2.2.4597' && event.supported && !event.experimental));
  assert.ok(!events.some(event=>/^launch_/.test(event.event) || ['startup_failed','renderer_error','native_files_missing','request_failed'].includes(event.event)));
  assert.ok(!events.some(event=>Object.values(event).some(value=>typeof value==='string' && value.includes(fl24))));
  return {format: metadata.format, appVersion: metadata.appVersion, nativePids: metadata.nativePids, eventCount: events.length, nativeHashes: {launcher: native.launcherHash, engine: native.engineHash}};
}

(async()=>{
  const packaged = path.join(root,'release/win-unpacked');
  const resources = path.join(packaged,'resources');
  const version = require('../package.json').version;
  const icons = [verifyExecutableIcons(path.join(packaged,'flim.exe')), verifyExecutableIcons(path.join(root,`release/flim-rooms-${version}-win64.exe`))];
  const files = (await fs.readdir(path.join(resources,'native'))).sort();
  assert.deepEqual(files,['flim.exe','flim_engine.dll','profiles']);
  const hashes = {};
  for(const file of ['flim.exe','flim_engine.dll','profiles/25.2.5.5319.profile','profiles/26.1.7.5654.profile']){
    const bytes = await fs.readFile(path.join(resources,'native',file));
    assert.deepEqual(bytes,await fs.readFile(path.join(root,'../out-experimental',file)));
    hashes[file]=createHash('sha256').update(bytes).digest('hex');
  }
  const {listPackage,extractFile} = require('@electron/asar');
  const listing = listPackage(path.join(resources,'app.asar'));
  assert.ok(listing.some(file=>/[\\/]dist[\\/]index\.html$/.test(file)));
  assert.ok(!listing.some(file=>/[\\/](?:test|\.wrangler|cloudflare-private|\.env|src)[\\/]/.test(file) && !file.includes('node_modules')));
  assert.ok(!listing.some(file=>/host-key|preferences\.json|\.flp$|\.log$/i.test(file)));
  for(const file of ['electron/main.cjs','electron/preload.cjs','electron/window-fit.cjs','electron/bridge-core.cjs','electron/atomic-settings.cjs','electron/single-flight.cjs','electron/sample-location.cjs','electron/file-transfers.cjs','electron/local-file.cjs','electron/setup.cjs','electron/diagnostics.cjs','electron/renderer-recovery.cjs','electron/recovery.html','electron/recovery.js','electron/peer-core.cjs','electron/peer-electron.cjs','electron/peer-preload.cjs','electron/peer-worker.html','electron/peer-worker.js','node_modules/ws/index.js',...(await fs.readdir(path.join(root,'node_modules/ws/lib'))).filter(name=>name.endsWith('.js')).map(name=>'node_modules/ws/lib/'+name),'dist/index.html',...listing.filter(file=>/[\\/]dist[\\/]assets[\\/].*\.(?:js|css)$/.test(file)).map(file=>file.replace(/^[\\/]/,''))]) {
    assert.deepEqual(extractFile(path.join(resources,'app.asar'),path.normalize(file)),await fs.readFile(path.join(root,file)),`Packaged source differs: ${file}`);
  }
  const inspected=JSON.parse(execFileSync(path.join(resources,'native/flim.exe'),['--inspect-json','C:\\Program Files\\Image-Line\\FL Studio 2025'],{encoding:'utf8',windowsHide:true}));
  assert.equal(inspected.supported,true);
  assert.equal(inspected.experimental,false);
  const inspected24=JSON.parse(execFileSync(path.join(resources,'native/flim.exe'),['--inspect-json',fl24],{encoding:'utf8',windowsHide:true}));
  assert.equal(inspected24.version,'24.2.2.4597'); assert.equal(inspected24.supported,true); assert.equal(inspected24.experimental,false);
  assert.equal(inspected24.reason,'');
  const testRoot=path.join(root,'test/.tmp'); await fs.mkdir(testRoot,{recursive:true});
  const userData=await fs.mkdtemp(path.join(testRoot,'packaged-user-data-'));
  await fs.writeFile(path.join(userData,'preferences.json'),JSON.stringify({name:'',flPath:'C:\\Program Files\\Image-Line\\FL Studio 2025',flPaths:[fl24]})+'\n');
  const app = await _electron.launch({executablePath:path.join(packaged,'flim.exe'),args:[`--user-data-dir=${userData}`],cwd:packaged,env:{...process.env,FLIM_DESKTOP_DEV:'1'}});
  try{
    const page=await app.firstWindow();const errors=[];
    page.on('pageerror',error=>errors.push(String(error)));
    page.on('console',msg=>{if(msg.type()==='error')errors.push(msg.text());});
    await page.getByRole('button',{name:'Get started',exact:true}).waitFor();
    assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).getAttribute('title'), 'Show files sidebar');
    await page.getByRole('button', {name: 'Files', exact: true}).click();
    await page.getByText('Create or join a room to share samples.', {exact: true}).waitFor();
    await page.waitForFunction(() => innerWidth === 680);
    await page.getByRole('button', {name: 'Hide files', exact: true}).focus(); await page.keyboard.press('Escape');
    await page.waitForFunction(() => innerWidth === 400);
    assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).evaluate(el => el === document.activeElement), true);
    assert.match(page.url(),/app\.asar\/dist\/index\.html$/);
    await page.evaluate(()=>document.fonts.ready);
    assert.equal(await page.evaluate(()=>document.fonts.check('14px "General Sans"')),true);
    assert.equal(await page.locator('.app-logo').evaluate(image=>image.complete && image.naturalWidth===256),true);
    assert.match(await page.locator('link[rel="icon"]').getAttribute('href'),/brand\/flim\.ico$/);
    const handle = await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].getNativeWindowHandle().readBigUInt64LE().toString());
    execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'test/window-icons.ps1'),'-WindowHandle',handle,'-OutputDirectory',path.join(root,'test/evidence')],{windowsHide:true,timeout:15000});
    const windowIcons = await app.evaluate(({nativeImage}, root) => {
      const path = process.getBuiltinModule('node:path');
      const fs = process.getBuiltinModule('node:fs');
      const output = path.join(root,'test/evidence');
      const ico = fs.readFileSync(path.join(root,'public/brand/flim.ico'));
      const results = [];
      for (const name of ['window-small','window-large']) {
        const actual = nativeImage.createFromPath(path.join(output,name+'.png'));
        const size = actual.getSize();
        let expected;
        for (let i=0;i<ico.readUInt16LE(4);i++) {
          const entry = 6+16*i;
          if ((ico[entry] || 256)===size.width) expected = nativeImage.createFromBuffer(ico.subarray(ico.readUInt32LE(entry+12),ico.readUInt32LE(entry+12)+ico.readUInt32LE(entry+8)));
        }
        if (!expected) throw new Error(`No reference frame for ${size.width}px window icon`);
        const a=actual.toBitmap(), b=expected.toBitmap();
        if (a.length!==b.length) throw new Error('Window icon dimensions differ from logo frame');
        let difference=0;
        for(let i=0;i<a.length;i++) difference+=Math.abs(a[i]-b[i]);
        const meanDifference=difference/a.length;
        if(meanDifference>2) throw new Error(`${name} differs from app logo by ${meanDifference}`);
        results.push({name,...size,meanDifference});
      }
      return results;
    }, root);
    await page.screenshot({path:path.join(__dirname,'evidence/packaged-closed.png'),animations:'disabled'});
    await page.getByRole('button',{name:'Get started',exact:true}).click();
    await page.waitForFunction(()=>!document.querySelector('#fl-install').disabled);
    const state=await page.evaluate(()=>window.flimDesktop.getState());
    assert.equal(state.nativeReady,true);
    for(const version of reviewedVersions) assert.ok(state.installs.some(x=>x.version===version && x.supported && !x.experimental));
    const installed24=state.installs.find(x=>x.version==='24.2.2.4597' && x.supported && !x.experimental && x.path.toLowerCase()===fl24.toLowerCase());
    assert.ok(installed24,'The selected installed FL24 must be discovered, even when another copy exists');
    assert.equal(state.installs.find(x=>x.id===state.selectedInstallId)?.version,'25.2.5.5319');
    assert.deepEqual(await installs.labels(page),['FL Studio 26','FL Studio 25','FL Studio 24']);
    if (!(await (await installs.choices(page)).evaluateAll((items,id)=>items.some(item=>item.dataset.value===id),installed24.id))) {
      await page.evaluate(installId=>window.flimDesktop.savePreferences({name:'Package test',installId}),installed24.id);
      await page.reload();await page.getByRole('button',{name:'Get started',exact:true}).click();
      await page.waitForFunction(()=>!document.querySelector('#fl-install').disabled);
    }
    await installs.select(page,installed24.id);
    assert.equal(await installs.value(page),installed24.id);
    assert.match(await page.locator('#install-help').textContent(),/Any supported version can host/);
    assert.doesNotMatch(await page.locator('.room-form').textContent(),/experimental|test only|FL 24 testing/i);
    await page.locator('.install-details summary').click();
    assert.ok((await page.locator('.install-details').textContent()).includes(fl24));
    assert.equal(await page.getByRole('button',{name:'Create room & open FL Studio',exact:true}).isEnabled(),true);
    assert.equal(state.diagnosticsAvailable,true);
    const diagnostics=await localDiagnostics(userData,version,hashes);
    assert.equal(state.room,null);
    assert.equal(await page.evaluate(()=>typeof window.require),'undefined');
    assert.match(await page.locator('meta[http-equiv="Content-Security-Policy"]').getAttribute('content'),/connect-src 'none'/);
    await page.screenshot({path:path.join(__dirname,'evidence/packaged-open.png'),animations:'disabled',fullPage:true});
    assert.deepEqual(errors,[]);
    const result={passed:true,checks:['Native payload matches tested binaries','No private keys, test fixtures, preferences or logs in ASAR','Bundled launcher verifies installed FL25 and supported FL24','Packaged app ignores inherited dev flag','Local renderer and General Sans load under production CSP','Installed FL24 is discovered and selectable alongside supported FL25/26 without changing the user profile','Real local diagnostics include app version, inspected builds and matching native file hashes','Renderer has no Node access','No room allocated or FL launched by packaging check','Header logo and favicon load locally','All nine icon sizes embedded in the app and portable executable','Running Windows small and large icons match the new logo'],hashes,icons,windowIcons,diagnostics,errors};
    result.checks.push('Visible Files edge arrow opens the pre-room sidebar, expands to 680px, then restores width and focus');
    await fs.writeFile(path.join(__dirname,'evidence/package-results.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify(result,null,2));
  }finally{
    await app.close();
    assert.ok(path.resolve(userData).startsWith(path.resolve(testRoot)+path.sep));
    await fs.rm(userData,{recursive:true,force:true,maxRetries:5,retryDelay:200});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
