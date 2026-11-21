// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Renders an SVG to square PNGs with Chromium, so filters and gradients come out as designed.
// Usage: node render_svg.cjs <input.svg> <output-prefix> <size> [<size> ...]
const { chromium } = require('playwright');
const { readFileSync } = require('node:fs');

const [input, prefix, ...sizes] = process.argv.slice(2);
if (!input || !prefix || sizes.length === 0) {
  console.error('usage: node render_svg.cjs <input.svg> <output-prefix> <size> [<size> ...]');
  process.exit(2);
}
(async () => {
const svg = readFileSync(input, 'utf8');
const browser = await chromium.launch();
try {
  for (const size of sizes.map(Number)) {
    const page = await browser.newPage({ viewport: { width: size, height: size } });
    // Inline, scaled to the viewport by CSS, so the SVG's own width and height do not matter.
    await page.setContent(
      `<html><head><style>html,body{margin:0;background:transparent}` +
      `svg{display:block;width:${size}px;height:${size}px}</style></head><body>${svg}</body></html>`);
    await page.screenshot({ path: `${prefix}-${size}.png`, omitBackground: true });
    await page.close();
  }
} finally {
  await browser.close();
}
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
