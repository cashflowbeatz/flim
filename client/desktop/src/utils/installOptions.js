/** @param {string} version */
const parts = version => version.split('.').map(Number);

/** @param {import('../types').Install} a @param {import('../types').Install} b */
function preferred(a, b) {
  const rank = item => item.supported ? item.experimental ? 1 : 2 : 0;
  const support = rank(b) - rank(a);
  if (support) return support;
  const av = parts(a.version), bv = parts(b.version);
  for (let i = 0; i < 4; i++) if (av[i] !== bv[i]) return bv[i] - av[i];
  const ap = a.path.toLowerCase(), bp = b.path.toLowerCase();
  return ap < bp ? -1 : ap > bp ? 1 : a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

/** @param {import('../types').Install[]} installs @param {string} selectedId */
export function installOptions(installs, selectedId) {
  /** @type {Map<number, import('../types').Install>} */
  const families = new Map();
  for (const install of installs) {
    const major = parts(install.version)[0];
    const current = families.get(major);
    if (!current || install.id === selectedId || (current.id !== selectedId && preferred(install, current) < 0)) families.set(major, install);
  }
  return [...families].sort(([a], [b]) => b - a).map(([major, install]) => ({...install, label: `FL Studio ${major}`}));
}
