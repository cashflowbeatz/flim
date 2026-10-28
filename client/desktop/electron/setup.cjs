'use strict';

function createSetupGate(initialize) {
  let completed = false, flight = null;
  return function ready() {
    if (completed) return Promise.resolve();
    if (flight) return flight;
    flight = Promise.resolve().then(initialize).then(() => {completed = true;}).finally(() => {flight = null;});
    return flight;
  };
}

module.exports = {createSetupGate};
