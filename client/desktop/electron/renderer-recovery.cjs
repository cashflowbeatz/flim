'use strict';

function createRecoveryBudget(now = Date.now) {
  let attempts = [];
  return () => {
    const time = now();
    attempts = attempts.filter(at => time >= at && time - at < 60_000);
    if (attempts.length >= 2) return false;
    attempts.push(time);
    return true;
  };
}

module.exports = {createRecoveryBudget};
