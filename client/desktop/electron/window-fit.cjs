'use strict';

function fitBounds(height, bounds, content, workArea, zoom = 1, cssWidth = 456) {
  if (!Number.isFinite(height) || height < 1 || height > 8192 || !Number.isFinite(zoom) || zoom < .25 || zoom > 5) return null;
  const frame = Math.max(0, bounds.height - content[1]);
  const frameWidth = Math.max(0, bounds.width - content[0]);
  if (![400, 456, 680, 756].includes(cssWidth)) return null;
  const width = Math.min(Math.max(1, workArea.width - 24), Math.ceil(cssWidth * zoom) + frameWidth);
  const maxHeight = Math.max(1, workArea.height - 24);
  const target = Math.min(maxHeight, Math.max(Math.min(320, maxHeight), Math.ceil(height * zoom) + frame));
  return {x: Math.max(workArea.x, Math.min(bounds.x, workArea.x + Math.max(0, workArea.width - width))),
    y: Math.max(workArea.y, Math.min(bounds.y, workArea.y + workArea.height - target)), width, height: target};
}

module.exports = {fitBounds};
