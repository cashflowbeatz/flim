const assert = require('node:assert/strict');
const installs = require('./install-picker.cjs');

module.exports = async ({browser, url, check, fixture, open, shot}) => {
  for (const width of [320, 375, 810, 1200]) {
    const page = await browser.newPage({viewport: {width, height: 740}, reducedMotion: 'reduce'});
    try {
      await fixture(page); await open(page, url);
      await check(`Styled FL dropdown remains readable and operable at ${width}px`, async () => {
        const trigger = installs.picker(page);
        const items = await installs.choices(page);
        assert.deepEqual(await items.allTextContents(), ['FL Studio 26', 'FL Studio 25', 'FL Studio 24']);
        assert.equal(await items.filter({hasText: 'FL Studio 25'}).getAttribute('aria-selected'), 'true');
        assert.equal(await trigger.getAttribute('aria-haspopup'), 'listbox');
        assert.equal(await trigger.evaluate(el => el === document.activeElement), true);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        for (const item of await items.all()) {
          const box = await item.boundingBox(); assert.ok(box.height >= 44);
        }
        await shot(page, `dropdown-${width}`);
        await page.keyboard.press('Escape');
        assert.equal(await trigger.getAttribute('aria-expanded'), 'false');
        assert.equal(await page.getByLabel('Your name').isVisible(), true);
      });
      if (width !== 375) continue;
      await check('Arrows preview the family; Enter selects it without submitting the room', async () => {
        const trigger = installs.picker(page); await trigger.focus();
        await trigger.press('ArrowDown'); await trigger.press('Home');
        assert.equal(await installs.value(page), 'fl25');
        const id = await trigger.getAttribute('aria-activedescendant');
        assert.equal(await page.locator(`#${id}`).textContent(), 'FL Studio 26');
        await trigger.press('Enter');
        assert.equal(await installs.value(page), 'fl26');
        assert.equal(await trigger.getAttribute('aria-expanded'), 'false');
        assert.deepEqual(await page.evaluate(() => window.testCalls), []);
      });
      await check('Escape cancels a preview without collapsing room setup or changing FL', async () => {
        const trigger = installs.picker(page); await trigger.press('End');
        await trigger.press('Escape');
        assert.equal(await installs.value(page), 'fl26');
        assert.equal(await page.getByLabel('Your name').isVisible(), true);
        assert.equal(await trigger.evaluate(el => el === document.activeElement), true);
      });
      await check('Type-ahead accepts a family number; Space selects FL24', async () => {
        const trigger = installs.picker(page); await trigger.press('2'); await trigger.press('4');
        const id = await trigger.getAttribute('aria-activedescendant');
        assert.equal(await page.locator(`#${id}`).textContent(), 'FL Studio 24');
        await trigger.press('Space');
        assert.equal(await installs.value(page), 'fl24');
        assert.doesNotMatch(await page.locator('.room-form').textContent(), /experimental|test only/i);
      });
      await check('Canceled type-ahead does not leak into the next dropdown visit', async () => {
        const trigger = installs.picker(page);
        await trigger.press('2'); await trigger.press('6'); await trigger.press('Escape');
        await trigger.press('2'); await trigger.press('5');
        const id = await trigger.getAttribute('aria-activedescendant');
        assert.equal(await page.locator(`#${id}`).textContent(), 'FL Studio 25');
        await trigger.press('Escape'); assert.equal(await installs.value(page), 'fl24');
      });
      await check('Tab and outside clicks dismiss uncommitted dropdown choices', async () => {
        const trigger = installs.picker(page); await trigger.press('Home'); await trigger.press('Tab');
        assert.equal(await installs.value(page), 'fl24');
        assert.equal(await page.getByRole('button', {name: 'Check again', exact: true}).evaluate(el => el === document.activeElement), true);
        await trigger.click(); await page.getByLabel('Your name').click();
        assert.equal(await trigger.getAttribute('aria-expanded'), 'false');
        assert.equal(await installs.value(page), 'fl24');
      });
      await check('Mouse selection returns focus and mode switching preserves the chosen family', async () => {
        await installs.select(page, 'fl25');
        assert.equal(await installs.picker(page).evaluate(el => el === document.activeElement), true);
        await page.getByRole('tab', {name: 'Join a room'}).click();
        assert.equal(await installs.value(page), 'fl25');
        await page.getByLabel('Your name').fill('Dropdown test');
        await page.getByLabel('Room number').fill('12345678');
        await page.getByRole('button', {name: 'Join room & open FL Studio'}).click();
        await page.getByText('Studio friend', {exact: true}).waitFor();
        assert.deepEqual((await page.evaluate(() => window.testCalls)).filter(call => ['host', 'member', 'launch'].includes(call)), ['member', 'launch']);
      });
    } finally {await page.close();}
  }
  const page = await browser.newPage({reducedMotion: 'reduce'});
  try {
    await fixture(page); await open(page, url);
    await check('Inputs and dropdown have no dark perimeter in idle, focused or invalid states', async () => {
      const noRing = async target => {
        const s = await target.evaluate(el => {const s = getComputedStyle(el); return {outline: s.outlineWidth, top: s.borderTopWidth, left: s.borderLeftWidth, right: s.borderRightWidth, bottom: s.borderBottomColor};});
        assert.equal(s.outline, '0px'); assert.equal(s.top, '0px'); assert.equal(s.left, '0px'); assert.equal(s.right, '0px');
        return s;
      };
      const name = page.getByLabel('Your name'); await name.focus();
      assert.equal((await noRing(name)).bottom, 'rgb(54, 100, 156)');
      await page.getByRole('button', {name: 'Create room & open FL Studio'}).click();
      assert.equal((await noRing(name)).bottom, 'rgb(54, 100, 156)');
      const picker = installs.picker(page); await picker.focus(); await picker.press('ArrowDown');
      assert.equal((await noRing(picker)).bottom, 'rgb(54, 100, 156)');
      await picker.press('Escape');
      await page.getByRole('tab', {name: 'Join a room'}).click();
      await page.getByLabel('Room number').focus(); await noRing(page.getByLabel('Room number'));
      await shot(page, 'soft-card-join');
    });
    await check('The empty dropdown stays disabled while the folder route remains usable', async () => {
      const empty = await browser.newPage();
      try {
        await fixture(empty, {noInstall: true}); await open(empty, url);
        assert.equal(await installs.picker(empty).isDisabled(), true);
        assert.equal(await installs.picker(empty).textContent(), 'No installation found');
        assert.equal(await empty.getByRole('button', {name: 'Choose folder'}).isEnabled(), true);
        assert.equal(await empty.getByRole('button', {name: 'Create room & open FL Studio'}).isDisabled(), true);
      } finally {await empty.close();}
    });
    await check('Forced colors keep keyboard focus and dropdown selection visible', async () => {
      await page.emulateMedia({forcedColors: 'active'});
      const trigger = installs.picker(page); await trigger.focus(); await trigger.press('End');
      const style = await trigger.evaluate(el => {const s = getComputedStyle(el);return {outline: s.outlineStyle, width: s.outlineWidth};});
      assert.equal(style.outline, 'solid'); assert.equal(style.width, '2px');
      assert.equal(await page.getByRole('option', {selected: true}).count(), 1);
      await shot(page, 'dropdown-forced-colors');
    });
  } finally {await page.close();}
};
