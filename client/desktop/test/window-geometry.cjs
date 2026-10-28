const assert = require('node:assert/strict');
module.exports = async function fitted(app, page) {
  for(let i=0;i<30;i++) {
    const native=await app.evaluate(({BrowserWindow,screen})=>{const w=BrowserWindow.getAllWindows()[0];return {bounds:w.getBounds(),content:w.getContentSize(),zoom:w.webContents.getZoomFactor(),area:screen.getDisplayMatching(w.getBounds()).workArea};});
    const renderer=await page.evaluate(()=>({width:innerWidth,height:innerHeight,scroll:document.documentElement.scrollWidth,main:document.querySelector('main').getBoundingClientRect().height}));
    const expected=Math.min(native.area.height-24,Math.max(320,Math.ceil(renderer.main*native.zoom)+native.bounds.height-native.content[1]));
    if(Math.abs(native.bounds.height-expected)<3 && Math.abs(renderer.width*native.zoom-native.content[0])<3 && renderer.scroll<=renderer.width) return {native,renderer};
    if(i===29)assert.fail(JSON.stringify({native,renderer,expected}));
    await new Promise(resolve=>setTimeout(resolve,100));
  }
};
