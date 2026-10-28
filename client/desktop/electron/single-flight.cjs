'use strict';

function singleFlight(task) {
  let current;
  return (...args) => {
    if (current) return current;
    const flight = Promise.resolve().then(() => task(...args));
    current = flight;
    void flight.finally(() => {if (current === flight) current = null;}).catch(() => {});
    return flight;
  };
}

module.exports = {singleFlight};
