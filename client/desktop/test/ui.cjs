const {chromium, _electron} = require('playwright');
const assert = require('node:assert/strict');
const http = require('node:http');
const path = require('node:path');
const fs = require('node:fs/promises');
const fitted = require('./window-geometry.cjs');
const installs = require('./install-picker.cjs');
const root = path.resolve(__dirname, '..');
const evidence = path.join(__dirname, 'evidence', require('../package.json').version);
const results = [];
const errors = [];

async function fixture(page, options = {}) {
  await page.addInitScript(options => {
    const state = {platform:'win32',name:'',installs: options.noInstall ? [] : [
      {id:'fl25',path:'test-only',version:'25.2.5.5319',supported:true,reason:''},
      {id:'fl26',path:'test-only',version:'26.1.7.5654',supported:true,reason:''},
      {id:'fl24',path:'test-only',version:'24.2.2.4597',supported:true,experimental:false,reason:''},
      {id:'unknown',path:'D:\\Music projects and applications\\'+ 'A long installation folder name '.repeat(8),version:'26.9.0.9999',supported:false,reason:'This build has not been verified yet.'}],
      selectedInstallId:options.noInstall ? '' : 'fl25',room:null,busy:Boolean(options.restoreBusy),nativeReady:!options.noNative,diagnosticsAvailable: !options.noLogs,
      notice:options.settingsNotice ? 'Your settings could not be saved. You can still use this session, but you may need to choose your name and FL Studio folder next time.' : ''};
    if(options.restoreRoom) {state.name='Returning producer';state.busy=Boolean(options.restoreBusy);state.room={code:'12345678',role:'host',maxParticipants:8,expiresAt:null,state:options.restoreBusy?'ready':'launched',status:options.restoreBusy?'waiting':'active',peers:[]};}
    window.testCalls = [];
    window.testState = state;
    window.testStatusFailures = options.statusFailures || 0;
    window.testSetupFailures = options.setupFailures || 0;
    const copy = () => JSON.parse(JSON.stringify(state));
    const create = async (input, role) => {
      window.testCalls.push(role);
      if (options.networkError) throw new Error('The relay could not be reached. Please try again.');
      if (role === 'member' && input.code.replace(/[ -]/g, '').length !== 8) throw new Error('Enter the eight-digit room number.');
      state.name = input.name; state.selectedInstallId = input.installId;
      state.room = {code:'12345678',role,maxParticipants:8,expiresAt:new Date(Date.now()+600000).toISOString(),state:'ready',status:'waiting',peers:[]};
      return copy();
    };
    window.flimDesktop = {
      getState:async()=>{
        if(window.testSetupFailures>0) {window.testSetupFailures--;throw new Error('The local setup could not be read.');}
        if(options.deferSetup) await new Promise(resolve=>{window.testReleaseSetup=resolve;});
        if(options.badState && !sessionStorage.getItem('test-bad-state-used')) {
          sessionStorage.setItem('test-bad-state-used','1');return {...copy(),installs:null};
        }
        return copy();
      }, savePreferences:async()=>copy(),
      chooseInstall:async currentInstallId=>{window.testCalls.push('choose');if(window.testChooseId || currentInstallId) state.selectedInstallId=window.testChooseId || currentInstallId;window.testChooseId='';return copy();},
      rescanInstalls:async currentInstallId=>{
        window.testCalls.push('rescan');
        if(options.rescanError)throw new Error('The installations could not be checked.');
        if(window.testDeferRescan)await new Promise(resolve=>{window.testReleaseRescan=resolve;});
        if(currentInstallId)state.selectedInstallId=currentInstallId;
        return copy();
      },
      createRoom:input=>create(input,'host'), joinRoom:input=>create(input,'member'),
      launchFL:async()=>{window.testCalls.push('launch');if(options.launchError) throw new Error(typeof options.launchError==='string' ? options.launchError : 'FL Studio could not start. Please try again.'); if(options.slow) await new Promise(r=>setTimeout(r,300)); state.room.state='launched'; return copy();},
      refreshRoom:async()=>{
        window.testCalls.push('refresh');
        if(options.statusError || window.testFailStatus || window.testStatusFailures>0) {window.testStatusFailures--;throw new Error('Status could not be loaded.');}
        if(options.ended){state.room.status='ended';state.room.peers=[];}
        else if(!options.waiting){state.room.status='active';state.room.peers=[{id:'a',name:state.name,role:state.room.role,fl:'25.2.5.5319'},{id:'b',name:'Studio friend',role:'member',fl:'26.1.7.5654'}];}
        if(window.testPeers)state.room.peers=window.testPeers;
        if(window.testRoomState)state.room.state=window.testRoomState;
        state.room.statusUpdatedAt=Date.now();
        const snapshot = copy();
        if(window.testDeferNextStatus){window.testDeferNextStatus=false;await new Promise(resolve=>{window.testReleaseStatus=resolve;});}
        return snapshot;
      },
      leaveRoom:async()=>{window.testCalls.push('leave');state.room=null;return copy();},
      copyInvite:async()=>{
        window.testCalls.push('copy');
        if(options.copyError)throw new Error('The clipboard could not be updated.');
        if(window.testDeferCopy)await new Promise(resolve=>{window.testReleaseCopy=resolve;});
        return {copied:true};
      },
      pasteRoomCode:async()=>{
        window.testCalls.push('paste');
        if(window.testDeferPaste)await new Promise(resolve=>{window.testReleasePaste=resolve;});
        if(options.pasteError)throw new Error('Copy an eight-digit room number, then paste again.');
        return {code:'87654321'};
      },
      openLogs:async()=>{
        window.testCalls.push('logs');
        if(window.testReenterLogs) {window.testReenterLogs=false;[...document.querySelectorAll('button')].find(button=>button.textContent==='Open test logs')?.click();}
        if(window.testDeferLogs)await new Promise(resolve=>{window.testReleaseLogs=resolve;});
        if(options.logsError)throw new Error('The test logs folder could not be opened.');return {opened:true};
      },
      reportError:input=>{window.testErrors=(window.testErrors||[]).concat(input);},
      fitWindow:height=>{window.testFits=(window.testFits||[]).concat(height);if(options.fitError && document.querySelector('.recovery'))throw new Error('Window fitting unavailable');},
    };
  }, options);
}

async function check(label, fn) { await fn(); results.push(label); console.log('PASS '+label); }
async function open(page, url) {
  page.on('pageerror', error=>errors.push(String(error)));
  page.on('console', msg=>{if(msg.type()==='error') errors.push(msg.text());});
  await page.goto(url);
  await page.getByRole('button',{name:'Get started',exact:true}).click();
  await page.getByLabel('Your name').waitFor();
}
async function shot(page, name) {await page.evaluate(()=>document.fonts.ready); await page.screenshot({path:path.join(evidence,name+'.png'),fullPage:true,animations:'disabled'});}
async function nativeShot(app, name) {
  const page=await app.firstWindow();
  await page.evaluate(async()=>{await document.fonts.ready;await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));});
  const png=await app.evaluate(async({BrowserWindow})=>(await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
  await fs.writeFile(path.join(evidence,name+'.png'),Buffer.from(png,'base64'));
}

(async()=>{
  await fs.mkdir(evidence,{recursive:true});
  const server = http.createServer(async(req,res)=>{
    try {
      const requested = new URL(req.url,'http://127.0.0.1').pathname;
      const file = path.resolve(root,'dist','.'+(requested==='/'?'/index.html':requested));
      if(!file.startsWith(path.join(root,'dist')+path.sep)) {res.writeHead(403);res.end();return;}
      const type={'.html':'text/html','.js':'text/javascript','.css':'text/css','.jpg':'image/jpeg','.png':'image/png','.ico':'image/x-icon','.woff2':'font/woff2'}[path.extname(file)];
      const data = await fs.readFile(file);
      res.writeHead(200,{'content-type':type||'application/octet-stream'});res.end(data);
    } catch {res.writeHead(404);res.end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const url = `http://127.0.0.1:${server.address().port}/`;
  let browser, electron;
  try {
    browser = await chromium.launch({executablePath:process.env.FLIM_TEST_CHROMIUM || 'C:/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe',headless:true});
    for (const width of [375,810,1200]) {
      const page = await browser.newPage({viewport:{width,height:900}}); await fixture(page);
      await page.goto(url); await shot(page,`closed-${width}`);
      await check(`Component fits its compact white shell ${width}`,async()=>{
        const layout=await page.evaluate(()=>{
          const shell=document.querySelector('main').getBoundingClientRect(),card=document.querySelector('.card-wrap').getBoundingClientRect();
          return {width:shell.width,height:shell.height,gutter:shell.width-card.width,background:getComputedStyle(document.documentElement).backgroundColor};
        });
        assert.ok(layout.width<=456 && layout.height<400 && layout.gutter<=16,JSON.stringify(layout));
        assert.equal(layout.background,'rgb(255, 255, 255)');
      });
      await page.getByRole('button',{name:'Get started',exact:true}).click();
      await page.getByLabel('Your name').waitFor();
      await check(`App expansion, font and width ${width}`,async()=>{
        await page.locator('.module-body').waitFor();
        assert.equal(await page.evaluate(()=>document.fonts.check('14px "General Sans"')),true);
        assert.match(await page.locator('h2').evaluate(el=>getComputedStyle(el).fontFamily),/^"General Sans"/);
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
        assert.equal(await page.locator('.app-logo').evaluate(image=>image.complete && image.naturalWidth===256),true);
        assert.equal(await page.getByLabel('Your name').evaluate(el=>el===document.activeElement),true);
      });
      await shot(page,`create-${width}`);
      await page.getByRole('tab',{name:'Join a room',exact:true}).click(); await shot(page,`join-${width}`);
      await check(`Escape and focus return ${width}`,async()=>{await page.getByLabel('Room number').focus();await page.keyboard.press('Escape');await page.locator('.module-artwork').waitFor();assert.equal(await page.getByRole('button',{name:'Get started',exact:true}).evaluate(el=>el===document.activeElement),true);});
      await page.close();
    }
    const page = await browser.newPage(); await fixture(page,{slow:true}); await open(page,url);
    await check('Empty required name prevents room allocation',async()=>{await page.getByRole('button',{name:'Create room & open FL Studio'}).click();assert.deepEqual(await page.evaluate(()=>window.testCalls),[]);});
    await check('One dropdown entry per FL family keeps build numbers in details',async()=>{
      assert.deepEqual(await installs.labels(page), ['FL Studio 26','FL Studio 25','FL Studio 24']);
      const details=page.locator('.install-details'); await details.locator('summary').focus();await page.keyboard.press('Enter');
      assert.deepEqual(await details.locator('summary').evaluate(el=>{const style=getComputedStyle(el);return [style.outlineStyle,style.outlineWidth,style.outlineColor];}),['solid','3px','rgb(54, 100, 156)']);
      assert.equal(await details.getByText('Build 25.2.5.5319',{exact:true}).isVisible(),true);
      await page.keyboard.press('Space');assert.equal(await details.getAttribute('open'),null);
    });
    await check('Folder picker route and unsupported-build gate',async()=>{await page.evaluate(()=>{window.testChooseId='unknown';});await page.getByRole('button',{name:'Choose folder'}).click();assert.equal(await installs.value(page),'unknown');assert.equal((await installs.labels(page)).length,3);assert.equal(await page.getByRole('button',{name:'Create room & open FL Studio'}).isDisabled(),true);assert.equal(await page.getByText('This build has not been verified yet.').isVisible(),true);
      await page.locator('.install-details summary').click();
      for(const width of [320,375,810,1200]){await page.setViewportSize({width,height:900});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await shot(page,`install-details-${width}`);}
      await page.locator('.install-details summary').click();await installs.select(page,'fl25');});
    await check('Canceled folder selection retains the selected FL version',async()=>{await installs.select(page,'fl26');await page.getByRole('button',{name:'Choose folder'}).click();assert.equal(await installs.value(page),'fl26');});
    await check('FL24 is selectable as supported without experimental wording',async()=>{
      await installs.select(page,'fl24');
      assert.match(await page.locator('#install-help').textContent(), /Any supported version can host/);
      assert.doesNotMatch(await page.locator('.room-form').textContent(), /experimental|test only|FL 24 testing/i);
      assert.equal(await page.getByRole('button',{name:'Create room & open FL Studio'}).isEnabled(), true);
      await shot(page,'fl24-supported');
      await installs.select(page,'fl26');
    });
    await check('Test logs open through the local desktop action',async()=>{
      await page.getByRole('button',{name:'Open test logs',exact:true}).click();
      assert.ok((await page.evaluate(()=>window.testCalls)).includes('logs'));
      assert.equal(await page.getByText('Logs stay on this computer.').isVisible(),true);
      assert.equal(await page.getByText('Menu',{exact:true}).count(),0);
      assert.equal(await page.getByLabel('Your name').isVisible(),true);
    });
    await page.getByLabel('Your name').fill('Room test');
    await check('Create automatically launches and shows real-state participant rows',async()=>{await page.getByRole('button',{name:'Create room & open FL Studio'}).click();await page.getByText('Studio friend',{exact:true}).waitFor();assert.equal(await page.getByText('Room is open',{exact:true}).isVisible(),true);assert.deepEqual((await page.evaluate(()=>window.testCalls)).filter(x=>x==='host'||x==='launch'),['host','launch']);});
    await shot(page,'room-active');
    await check('Copy, refresh and return to start',async()=>{await page.getByRole('button',{name:'Copy room number',exact:true}).click();await page.getByText('Room number copied',{exact:true}).waitFor();await page.getByRole('button',{name:'Refresh people'}).click();await page.getByRole('button',{name:'Return to start'}).click();await page.getByLabel('Your name').waitFor();assert.equal(await page.getByLabel('Your name').inputValue(),'Room test');});
    await check('Join accepts spaced room code and launches once',async()=>{await page.getByRole('tab',{name:'Join a room',exact:true}).click();await page.getByLabel('Room number').fill('1234 5678');await page.getByRole('button',{name:'Join room & open FL Studio'}).click();await page.getByText('You joined this room',{exact:true}).waitFor();await page.getByText('Studio friend',{exact:true}).waitFor();assert.equal((await page.evaluate(()=>window.testCalls)).filter(x=>x==='member').length,1);});
    await check('Delayed status cannot restore a room after returning to start',async()=>{
      await page.evaluate(()=>{window.testDeferNextStatus=true;});
      await page.getByRole('button',{name:'Refresh people'}).click();
      await page.waitForFunction(()=>Boolean(window.testReleaseStatus));
      await page.getByRole('button',{name:'Return to start'}).click();
      await page.getByLabel('Your name').waitFor();
      await page.evaluate(async()=>{window.testReleaseStatus();await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));});
      assert.equal(await page.getByLabel('Your name').isVisible(),true);
    });
    await page.close();
    {
      const page=await browser.newPage();
      await fixture(page,{launchError:'This room was created in FL Studio 26 using a newer build. Ask the person using FL Studio 24 to create the room, then join it.'});await open(page,url);
      await page.getByLabel('Your name').fill('Room test');await installs.select(page,'fl24');
      await page.getByRole('tab',{name:'Join a room',exact:true}).click();await page.getByLabel('Room number').fill('12345678');
      await check('Incompatible host guidance leaves a retryable room and a working return action',async()=>{
        await page.getByRole('button',{name:'Join room & open FL Studio'}).click();await page.getByRole('alert').waitFor();
        assert.match(await page.getByRole('alert').textContent(),/FL Studio 24 to create the room/);
        assert.equal(await page.getByRole('button',{name:'Open FL Studio',exact:true}).isEnabled(),true);
        assert.equal(await page.getByText('The launcher did not confirm startup.',{exact:false}).count(),0);
        await page.getByRole('button',{name:'Return to start'}).click();await page.getByLabel('Your name').waitFor();
      });await page.close();
    }
    {
      const page=await browser.newPage();await fixture(page);await open(page,url);
      await page.getByRole('tab',{name:'Join a room',exact:true}).click();
      await check('Room-number editing preserves the caret and accepts pasted hyphens',async()=>{
        const input=page.getByLabel('Room number');await input.fill('1234 5678');
        await input.press('Home');for(let i=0;i<5;i++)await input.press('ArrowRight');
        await input.press('Backspace');assert.equal(await input.inputValue(),'12345678');
        assert.equal(await input.evaluate(el=>el.selectionStart),4);
        await input.press('9');assert.equal(await input.inputValue(),'123495678');
        await input.fill('1234-5678');assert.equal(await input.evaluate(el=>el.checkValidity()),true);
        for(const valid of [' 1234 5678 ', '12-34 56-78', '12345678']) {await input.fill(valid);assert.equal(await input.evaluate(el=>el.checkValidity()),true);}
        for(const invalid of ['1234567', '123456789', '---']) {await input.fill(invalid);assert.equal(await input.evaluate(el=>el.checkValidity()),false);}
      });await page.close();
    }
    {
      const page=await browser.newPage();await page.clock.install();await fixture(page);await open(page,url);
      await page.getByLabel('Your name').fill('Room test');
      const create=async()=>{await page.getByRole('button',{name:'Create room & open FL Studio'}).click();await page.getByText('Studio friend',{exact:true}).waitFor();};
      await create();
      await check('Copy feedback and status requests belong to the current room visit',async()=>{
        await page.getByRole('button',{name:'Copy room number',exact:true}).click();
        await page.evaluate(()=>{window.testDeferNextStatus=true;});
        await page.getByRole('button',{name:'Refresh people'}).click();
        await page.waitForFunction(()=>Boolean(window.testReleaseStatus));
        await page.evaluate(()=>{window.testOldRelease=window.testReleaseStatus;window.testReleaseStatus=null;});
        await page.getByRole('button',{name:'Return to start'}).click();await create();
        assert.equal(await page.getByRole('button',{name:'Copy room number',exact:true}).isVisible(),true);
        await page.clock.fastForward(1000);
        await page.getByRole('button',{name:'Copy room number',exact:true}).click();
        await page.evaluate(()=>{window.testDeferNextStatus=true;});
        await page.getByRole('button',{name:'Refresh people'}).click();
        await page.waitForFunction(()=>Boolean(window.testReleaseStatus));
        await page.evaluate(()=>window.testOldRelease());
        await page.clock.fastForward(1501);
        assert.equal(await page.getByRole('button',{name:'Room number copied'}).isVisible(),true);
        assert.equal(await page.getByRole('button',{name:'Refreshing people…'}).isDisabled(),true);
        await page.evaluate(()=>window.testReleaseStatus());
        await page.getByRole('button',{name:'Refresh people'}).waitFor();
        await page.clock.fastForward(1000);
        assert.equal(await page.getByRole('button',{name:'Copy room number',exact:true}).isVisible(),true);
      });await page.close();
    }
    {
      const page=await browser.newPage({viewport:{width:320,height:450}});await fixture(page);await open(page,url);
      await check('Small windows keep every control scrollable with usable target sizes',async()=>{
        // Fractional translateY positions can round a 44px DOMRect down during entry.
        await page.locator('.menu-content').evaluate(async element=>{await Promise.all(element.getAnimations().map(animation=>animation.finished));});
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
        for(const target of ['Choose folder','Create a room','Join a room']) {
          const box=await page.getByRole(target === 'Choose folder' ? 'button' : 'tab',{name:target,exact:true}).boundingBox();
          assert.ok(box.height>=44,`${target} target height is ${box.height}px; expected at least 44px`);
        }
        const submit=page.getByRole('button',{name:'Create room & open FL Studio'});await submit.scrollIntoViewIfNeeded();
        const box=await submit.boundingBox();assert.ok(box.y>=0 && box.y+box.height<=450);
      });await shot(page,'short-window');await page.close();
    }
    for(const options of [{logsError:true},{noLogs:true}]) {
      const page=await browser.newPage({viewport:{width:375,height:900}});await fixture(page,options);await open(page,url);
      await check(options.logsError ? 'Failed log-folder opening leaves an actionable error' : 'Unavailable diagnostics are explained without blocking room creation',async()=>{
        const button=page.getByRole('button',{name:'Open test logs',exact:true});
        if(options.logsError) {
          await button.click(); await page.getByRole('alert').waitFor();
          assert.match(await page.getByRole('alert').textContent(), /could not be opened/);
          assert.equal(await button.isEnabled(),true);
        } else {
          assert.equal(await button.isDisabled(),true);
          assert.equal(await page.getByRole('button',{name:'Create room & open FL Studio'}).isEnabled(),true);
        }
      });await page.close();
    }
    for(const [scenario,options] of Object.entries({network:{networkError:true},launch:{launchError:true},status:{statusError:true},waiting:{waiting:true},ended:{ended:true},missing:{noInstall:true}})){
      const page=await browser.newPage({viewport:{width:810,height:900}});await fixture(page,options);await open(page,url);await page.getByLabel('Your name').fill('Room test');
      await check(`${scenario} state stays actionable`,async()=>{
        if(options.noInstall){assert.equal(await page.getByRole('button',{name:'Create room & open FL Studio'}).isDisabled(),true);assert.equal(await page.getByRole('button',{name:'Choose folder'}).isEnabled(),true);return;}
        await page.getByRole('button',{name:'Create room & open FL Studio'}).click();
        if(options.networkError) await page.getByRole('alert').waitFor();
        if(options.launchError) await page.getByRole('button',{name:'Open FL Studio',exact:true}).waitFor();
        if(options.statusError) await page.getByRole('button',{name:'Retry status'}).click();
        if(options.waiting) await page.getByText('Waiting for FL Studio to connect.',{exact:false}).waitFor();
        if(options.ended){await page.getByText('Room closed',{exact:true}).waitFor();assert.equal(await page.getByRole('button',{name:'Copy room number'}).count(),0);assert.equal(await page.getByRole('button',{name:'Refresh people'}).count(),0);}
      });await shot(page,`state-${scenario}`);await page.close();
    }
    {
      const page=await browser.newPage({viewport:{width:375,height:700}});await fixture(page,{settingsNotice:true});await open(page,url);
      await check('Settings-storage notice remains readable and does not block a session',async()=>{
        await page.getByLabel('Your name').fill('Room test');
        await page.getByRole('button',{name:'Create room & open FL Studio'}).click();
        await page.getByText('Studio friend',{exact:true}).waitFor();
        const notice=page.getByRole('status').filter({hasText:'Your settings could not be saved.'});
        await notice.scrollIntoViewIfNeeded();assert.equal(await notice.isVisible(),true);
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
        await shot(page,'settings-notice-375');
      });await page.close();
    }
    {
      const page=await browser.newPage();await page.clock.install();await fixture(page,{statusFailures:1});await open(page,url);
      await page.getByLabel('Your name').fill('Room test');await page.getByRole('button',{name:'Create room & open FL Studio'}).click();
      await check('Temporary status failures recover automatically without another click',async()=>{
        await page.getByRole('button',{name:'Retry status'}).waitFor();
        await page.clock.fastForward(5001);
        await page.getByText('Studio friend',{exact:true}).waitFor();
        assert.equal(await page.getByRole('button',{name:'Retry status'}).count(),0);
        assert.equal((await page.evaluate(()=>window.testCalls)).filter(x=>x==='refresh').length,2);
      });await page.close();
    }
    {
      const page=await browser.newPage();await page.clock.install();await fixture(page);await open(page,url);
      await page.getByLabel('Your name').fill('Room test');await page.getByRole('button',{name:'Create room & open FL Studio'}).click();await page.getByText('Studio friend',{exact:true}).waitFor();
      await check('Hidden and offline windows pause status traffic and refresh when available again',async()=>{
        await page.evaluate(()=>{window.testHidden=true;window.testOnline=true;Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.testHidden});Object.defineProperty(navigator,'onLine',{configurable:true,get:()=>window.testOnline});document.dispatchEvent(new Event('visibilitychange'));});
        await page.clock.fastForward(60001);
        assert.equal((await page.evaluate(()=>window.testCalls)).filter(x=>x==='refresh').length,1);
        await page.evaluate(()=>{window.testHidden=false;document.dispatchEvent(new Event('visibilitychange'));});
        await page.waitForFunction(()=>window.testCalls.filter(x=>x==='refresh').length===2);
        await page.evaluate(()=>{window.testOnline=false;});await page.clock.fastForward(60001);
        assert.equal((await page.evaluate(()=>window.testCalls)).filter(x=>x==='refresh').length,2);
        await page.evaluate(()=>{window.testOnline=true;window.dispatchEvent(new Event('online'));});
        await page.waitForFunction(()=>window.testCalls.filter(x=>x==='refresh').length===3);
      });await page.close();
    }
    await require('./refinements.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0214.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0215.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0217.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0218.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0219.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0220.cjs')({browser, url, check, fixture, open, shot});
    await require('./refinements-0222.cjs')({browser, url, check, fixture, open, shot});
    const preview = await browser.newPage({reducedMotion:'reduce'});await open(preview,url);
    await check('Browser preview is explicit and cannot fake an FL connection',async()=>{assert.equal(await preview.getByText('This is the browser preview.',{exact:false}).isVisible(),true);assert.equal(await preview.getByRole('button',{name:'Create room & open FL Studio'}).isDisabled(),true);assert.equal(await preview.locator('.menu-content').evaluate(el=>getComputedStyle(el).animationName),'none');});
    await preview.close();
    if (!process.env.FLIM_UI_SKIP_NATIVE) {
    electron = await _electron.launch({executablePath:path.join(root,'node_modules/electron/dist/electron.exe'),args:[root,`--user-data-dir=${path.join(root,'test/.tmp/ui-user-data')}`],cwd:root});
    const window = await electron.firstWindow(); await window.getByRole('button',{name:'Get started',exact:true}).waitFor();
    await electron.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1));
    await fitted(electron, window);
    await nativeShot(electron,'native-closed');await window.getByRole('button',{name:'Get started',exact:true}).click();await window.waitForFunction(()=>!document.querySelector('#fl-install').disabled);await nativeShot(electron,'native-open');
    await check('Actual desktop discovers installed FL24/25/26 without launching FL',async()=>{const state=await window.evaluate(()=>window.flimDesktop.getState());for(const version of ['24.2.2.4597','25.2.5.5319','26.1.7.5654']){const item=state.installs.find(x=>x.version===version);assert.equal(item?.supported,true);assert.equal(item?.experimental,false);}assert.deepEqual(await installs.labels(window),['FL Studio 26','FL Studio 25','FL Studio 24']);assert.equal(state.room,null);assert.equal(await window.evaluate(()=>typeof window.require),'undefined');});
    await check('Actual desktop rescans FL installations through the isolated bridge without opening a room',async()=>{
      const before=await installs.value(window);
      await window.getByLabel('Your name').fill('Retain typed name');
      await window.getByRole('button',{name:'Check again',exact:true}).click();
      await window.getByText('Compatibility was checked again.',{exact:false}).waitFor();
      const state=await window.evaluate(()=>window.flimDesktop.getState());
      assert.equal(state.room,null);assert.equal(state.selectedInstallId,before);
      assert.equal(await window.getByLabel('Your name').inputValue(),'Retain typed name');
      for(const version of ['24.2.2.4597','25.2.5.5319','26.1.7.5654'])assert.equal(state.installs.find(x=>x.version===version)?.supported,true);
    });
    await check('Actual desktop at 200 percent zoom keeps installation details and submit reachable',async()=>{
      await electron.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(2));
      await window.locator('.install-details summary').click();
      await fitted(electron, window);
      const submit=window.getByRole('button',{name:'Create room & open FL Studio',exact:true});await submit.scrollIntoViewIfNeeded();
      assert.equal(await window.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      const box=await submit.boundingBox();const height=await window.evaluate(()=>innerHeight);assert.ok(box.y>=0 && box.y+box.height<=height+1);
      const screenshot=await electron.evaluate(async({BrowserWindow})=>(await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
      await fs.writeFile(path.join(evidence,'native-zoom-details.png'),Buffer.from(screenshot,'base64'));
      await electron.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].webContents.setZoomFactor(1));
    });
    await check('Native window grows with the menu, shrinks on collapse and stays inside the work area',async()=>{
      await window.getByRole('button',{name:'Collapse room controls',exact:true}).click();
      await window.waitForFunction(()=>!document.querySelector('.card-wrap').matches('[data-open="true"]'));
      await fitted(electron, window);
      const closed=await electron.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].getBounds());
      assert.ok(closed.width<490 && closed.height<460,JSON.stringify(closed));
      await window.getByRole('button',{name:'Get started',exact:true}).click();
      await fitted(electron, window);
      const opened=await electron.evaluate(({BrowserWindow,screen})=>{const w=BrowserWindow.getAllWindows()[0],bounds=w.getBounds();return {bounds,area:screen.getDisplayMatching(bounds).workArea};});
      assert.ok(opened.bounds.height>closed.height+100);
      assert.ok(opened.bounds.y>=opened.area.y && opened.bounds.y+opened.bounds.height<=opened.area.y+opened.area.height);
      await nativeShot(electron,'native-fitted-open');
    });
    await check('Production CSP disables renderer networking',async()=>assert.match(await window.locator('meta[http-equiv="Content-Security-Policy"]').getAttribute('content'),/connect-src 'none'/));
    }
    assert.deepEqual(errors,[]);
    await fs.writeFile(path.join(evidence,'ui-results.json'),JSON.stringify({passed:results.length,results,errors},null,2));
  } finally {await electron?.close();await browser?.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
