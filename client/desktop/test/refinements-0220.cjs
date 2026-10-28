const assert = require('node:assert/strict');

module.exports = async ({browser, url, check, fixture, shot}) => {
  const page = await browser.newPage({viewport: {width: 400, height: 900}});
  try {
    await fixture(page, {badState: true, fitError: true}); await page.goto(url);
    await check('Recovery survives a failing window-fit bridge and keeps reload and logs reachable', async () => {
      await page.getByRole('button', {name: 'Reload interface', exact: true}).waitFor();
      assert.equal(await page.getByRole('button', {name: 'Open test logs', exact: true}).isVisible(), true);
      assert.ok(await page.evaluate(() => window.testFits.length > 0));
      assert.equal(await page.evaluate(() => document.activeElement.textContent), 'The interface needs to reload.');
    });
    await check('Recovery coalesces synchronous duplicate log clicks while its first request is pending', async () => {
      await page.evaluate(() => {
        window.testDeferLogs = true;
        window.testReenterLogs = true;
        const button = [...document.querySelectorAll('button')].find(button => button.textContent === 'Open test logs');
        button.click(); button.click();
      });
      assert.equal(await page.evaluate(() => window.testCalls.filter(call => call === 'logs').length), 1);
      assert.equal(await page.getByRole('button', {name: 'Opening test logs…', exact: true}).isDisabled(), true);
      await page.evaluate(() => window.testReleaseLogs());
      await page.getByRole('button', {name: 'Open test logs', exact: true}).waitFor();
    });
    await check('Recovery refits changing content at a narrow viewport without horizontal overflow', async () => {
      const before = await page.evaluate(() => window.testFits.length);
      await page.setViewportSize({width: 280, height: 640});
      await page.waitForFunction(count => window.testFits.length > count, before);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      const fit = await page.evaluate(() => ({reported: window.testFits.at(-1), height: Math.ceil(document.querySelector('.recovery').getBoundingClientRect().height)}));
      assert.equal(fit.reported, fit.height);
      await shot(page, 'recovery-narrow-0220');
      await page.getByRole('button', {name: 'Reload interface', exact: true}).click();
      await page.getByRole('button', {name: 'Get started', exact: true}).waitFor();
      assert.equal(await page.evaluate(() => window.testCalls.includes('launch')), false);
    });
  } finally {await page.close();}
};
