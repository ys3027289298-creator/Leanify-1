// Renders an SVG to a PNG using resvg (a single, fixed renderer version for the
// whole comparison so before/after differences are not renderer noise).
// usage: node render.js input.svg output.png
const fs = require('fs');
const { Resvg } = require('@resvg/resvg-js');

const [, , input, output] = process.argv;
if (!input || !output) {
  console.error('usage: node render.js input.svg output.png');
  process.exit(2);
}

const svg = fs.readFileSync(input);
const resvg = new Resvg(svg, {
  fitTo: { mode: 'width', value: 480 },
  background: '#ffffff',
  font: { loadSystemFonts: true },
});
const png = resvg.render().asPng();
fs.writeFileSync(output, png);
