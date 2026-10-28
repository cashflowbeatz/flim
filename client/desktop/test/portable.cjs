const {chromium} = require('playwright');
const {spawn} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs/promises');
const net = require('node:net');
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const installs = require('./install-picker.cjs');
const root=path.resolve(__dirname,'..');
const fl24=path.resolve(process.env.FLIM_TEST_FL24_DIR || path.join(process.env.ProgramW6432 || process.env.ProgramFiles || 'C:\\Program Files', 'Image-Line', 'FL Studio 2024'));
const reviewedVersions=['25.2.5.5319','26.1.7.5654'];

async function localDiagnostics(userData,version,hashes){
  const directory=path.join(userData,'logs');
  let metadata,events,session;
  for(let attempt=0;attempt<50;attempt++){
    try{
      const names=await fs.readdir(directory);
      assert.equal(names.length,1,'An isolated portable launch should create one diagnostics session');
      session=names[0];
      metadata=JSON.parse(await fs.readFile(path.join(directory,session,'session.json'),'utf8'));
      events=(await fs.readFile(path.join(directory,session,'events.jsonl'),'utf8')).trim().split('\n').filter(Boolean).map(line=>JSON.parse(line));
      if(events.some(event=>event.event==='native_files') && [...reviewedVersions,'24.2.2.4597'].every(version=>events.some(event=>event.event==='install_inspected' && event.flVersion===version)))break;
    }catch(error){if(error.code!=='ENOENT' && !(error instanceof SyntaxError))throw error;}
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  assert.equal(metadata?.format,'flim-diagnostics-v1');assert.equal(metadata.appVersion,version);assert.equal(metadata.directory,session);
  assert.ok(Number.isSafeInteger(metadata.appPid) && metadata.appPid>0);assert.deepEqual(metadata.nativePids,[]);
  const native=events.find(event=>event.event==='native_files');
  assert.equal(native?.launcherHash,hashes['flim.exe']);assert.equal(native?.engineHash,hashes['flim_engine.dll']);
  assert.ok(events.some(event=>event.event==='app_start' && event.appVersion===version && event.pid===metadata.appPid));
  for(const version of reviewedVersions)assert.ok(events.some(event=>event.event==='install_inspected' && event.flVersion===version && event.supported && !event.experimental));
  assert.ok(events.some(event=>event.event==='install_inspected' && event.flVersion==='24.2.2.4597' && event.supported && !event.experimental));
  assert.ok(!events.some(event=>/^launch_/.test(event.event) || ['startup_failed','renderer_error','native_files_missing','request_failed'].includes(event.event)));
  assert.ok(!events.some(event=>Object.values(event).some(value=>typeof value==='string' && value.includes(fl24))));
  return {format:metadata.format,appVersion:metadata.appVersion,nativePids:metadata.nativePids,eventCount:events.length,nativeHashes:{launcher:native.launcherHash,engine:native.engineHash}};
}

(async()=>{
  const portLease=net.createServer();
  await new Promise(resolve=>portLease.listen(0,'127.0.0.1',resolve));
  const port=portLease.address().port;
  await new Promise(resolve=>portLease.close(resolve));
  const version=require('../package.json').version;
  const hashes={};
  for(const file of ['flim.exe','flim_engine.dll'])hashes[file]=createHash('sha256').update(await fs.readFile(path.join(root,'../out-experimental',file))).digest('hex');
  const testRoot=path.join(root,'test/.tmp');await fs.mkdir(testRoot,{recursive:true});
  const userData=await fs.mkdtemp(path.join(testRoot,'portable-user-data-'));
  await fs.writeFile(path.join(userData,'preferences.json'),JSON.stringify({name:'',flPath:'C:\\Program Files\\Image-Line\\FL Studio 2025',flPaths:[fl24]})+'\n');
  const child=spawn(path.join(root,`release/flim-rooms-${version}-win64.exe`),[`--remote-debugging-port=${port}`,'--remote-debugging-address=127.0.0.1',`--user-data-dir=${userData}`],{cwd:root,stdio:'ignore'});
  let launchError, launchExit;child.on('error',error=>{launchError=error;});
  child.on('exit', (code, signal) => {launchExit = {code, signal};});
  let browser;
  try{
    const deadline=Date.now()+60000;
    while(Date.now()<deadline){
      if(launchError)throw launchError;
      if(launchExit)throw new Error(`Portable exited before startup: ${JSON.stringify(launchExit)}`);
      try{browser=await chromium.connectOverCDP(`http://127.0.0.1:${port}`,{timeout:1000});break;}catch{}
      await new Promise(resolve=>setTimeout(resolve,500));
    }
    assert.ok(browser,'Portable launcher did not expose its own debug endpoint within sixty seconds');
    const page=browser.contexts()[0].pages()[0];
    await page.getByRole('button',{name:'Get started',exact:true}).waitFor();
    assert.equal(await page.getByRole('button', {name: 'Files', exact: true}).getAttribute('title'), 'Show files sidebar');
    await page.getByRole('button', {name: 'Files', exact: true}).click();
    await page.getByText('Create or join a room to share samples.', {exact: true}).waitFor();
    await page.waitForFunction(() => innerWidth === 680);
    await page.getByRole('button', {name: 'Hide files', exact: true}).click();
    await page.waitForFunction(() => innerWidth === 400);
    assert.match(page.url(),/app\.asar\/dist\/index\.html$/);
    await page.getByRole('button',{name:'Get started',exact:true}).click();
    await page.waitForFunction(()=>!document.querySelector('#fl-install').disabled);
    const state=await page.evaluate(()=>window.flimDesktop.getState());
    assert.equal(state.nativeReady,true);assert.equal(state.room,null);
    for(const version of reviewedVersions)assert.ok(state.installs.some(x=>x.version===version && x.supported && !x.experimental));
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
    await fs.writeFile(path.join(__dirname,'evidence/portable-results.json'),JSON.stringify({passed:true,checks:['Portable executable self-extracts and opens the actual app','Installed FL24 is discovered and selectable alongside reviewed FL25/26 without changing the user profile','Real local diagnostics include app version, inspected builds and matching native file hashes','No real room allocated and no FL instance launched'],diagnostics},null,2));
    console.log('PASS portable executable startup, supported FL24/25/26 discovery, local diagnostics/hash metadata and clean initial room state');
  }finally{
    if(browser){const session=await browser.newBrowserCDPSession();await session.send('Browser.close').catch(()=>{});await browser.close().catch(()=>{});}
    else if(child.exitCode===null){child.kill();}
    assert.ok(path.resolve(userData).startsWith(path.resolve(testRoot)+path.sep));
    await fs.rm(userData,{recursive:true,force:true,maxRetries:8,retryDelay:250});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
