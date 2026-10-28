document.getElementById('reload').addEventListener('click', async event => {
  event.currentTarget.disabled = true;
  try { await window.flimDesktop.reloadControls(); }
  catch { document.getElementById('error').textContent = 'The controls could not be reloaded. Try again.'; event.target.disabled = false; }
});
