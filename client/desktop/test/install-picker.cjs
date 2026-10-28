const assert = require('node:assert/strict');
const picker = page => page.getByRole('combobox', {name: 'FL Studio', exact: true});

async function choices(page) {
  const trigger = picker(page);
  if (await trigger.getAttribute('aria-expanded') !== 'true') await trigger.click();
  return page.getByRole('option');
}

async function labels(page) {
  const items = await choices(page);
  const result = await items.allTextContents();
  await picker(page).press('Escape');
  return result;
}

async function select(page, id) {
  const items = await choices(page);
  const index = await items.evaluateAll((elements, id) => elements.findIndex(item => item.dataset.value === id), id);
  assert.ok(index >= 0, `Installation ${id} must be offered by the picker`);
  await items.nth(index).click();
}

module.exports = {picker, choices, labels, select, value: page => picker(page).evaluate(el => el.value)};
