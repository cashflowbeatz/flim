const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  for (const width of [280, 400, 810]) {
    const page = await browser.newPage({viewport: {width, height: 900}});
    try {
      await fixture(page); await open(page, url);
      await page.getByLabel('Your name').fill('Layout fixture');
      await check(`Create/Join switch preserves form geometry, input and scroll at ${width}px`, async () => {
        const measure = () => page.evaluate(() => {
          const rect = selector => { const r = document.querySelector(selector).getBoundingClientRect(); return {x: r.x, y: r.y, width: r.width, height: r.height}; };
          return {form: rect('.room-form'), name: rect('#room-name'), install: rect('#fl-install'), submit: rect('.submit'), scroll: scrollY};
        });
        const before = await measure();
        assert.equal(await page.getByRole('textbox', {name: 'Room number', exact: true}).count(), 0);
        for (let i = 0; i < 4; i++) {
          await page.getByRole('tab', {name: 'Join a room', exact: true}).click();
          await page.keyboard.type(i === 0 ? '12345678' : '');
          assert.deepEqual(await measure(), before);
          assert.equal(await page.getByLabel('Room number').inputValue(), i === 0 ? '12345678' : '1234 5678');
          await page.getByRole('tab', {name: 'Create a room', exact: true}).click();
          assert.deepEqual(await measure(), before);
          assert.equal(await page.getByLabel('Your name').inputValue(), 'Layout fixture');
        }
        await page.getByRole('tab', {name: 'Join a room', exact: true}).focus();
        await page.keyboard.press('Enter');
        await page.getByLabel('Room number').focus(); await page.keyboard.press('Shift+Tab');
        assert.equal(await page.getByRole('button', {name: 'Paste room number', exact: true}).evaluate(el => el === document.activeElement), true);
        await page.getByRole('tab', {name: 'Create a room', exact: true}).click();
        assert.equal(await page.locator('#room-code').isDisabled(), true);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        await shot(page, `stable-menu-${width}`);
      });
    } finally { await page.close(); }
  }
};
