// Capture a page through Chrome DevTools Protocol with an exact viewport.
// Usage: node tools/cdp_capture.mjs <url> <out.png> [width] [height] [port]
import { writeFileSync } from 'node:fs';

const [url, out, wArg, hArg, portArg, framesArg] = process.argv.slice(2);
const width = Number(wArg || 960), height = Number(hArg || 540), port = Number(portArg || 9222);
const waitFrames = Number(framesArg || 10);
if (!url || !out) { console.error('usage: cdp_capture.mjs <url> <out.png> [w] [h] [port]'); process.exit(2); }

const sleep = (ms) => new Promise(r => setTimeout(r, ms));
async function json(path, method = 'GET') {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${port}${path}`, { method });
      if (r.ok) return await r.json();
    } catch {}
    await sleep(100);
  }
  throw new Error(`CDP endpoint ${path} not ready`);
}

const target = await json('/json/new?about:blank', 'PUT');
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });

let nextId = 1;
const pending = new Map();
const consoleLines = [];
ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) {
    const { res, rej } = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? rej(new Error(msg.error.message)) : res(msg.result);
  } else if (msg.method === 'Runtime.consoleAPICalled') {
    const line = msg.params.args.map(a => a.value ?? a.description ?? '').join(' ');
    consoleLines.push(line);
    if (line.includes('frames=')) console.log(`[cdp] ${line}`);
  }
};
function send(method, params = {}) {
  const id = nextId++;
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

await send('Page.enable');
await send('Runtime.enable');
await send('Emulation.setDeviceMetricsOverride', {
  width, height, deviceScaleFactor: 1, mobile: false,
  screenWidth: width, screenHeight: height,
  screenOrientation: { type: 'landscapePrimary', angle: 90 },
});
await send('Page.navigate', { url });

// The example reports at frame 10; wait for that console line or a timeout.
for (let i = 0; i < 300; i++) {
  if (consoleLines.some(l => l.includes(`frames=${waitFrames}`) && l.includes('errors='))) break;
  await sleep(100);
}
await sleep(300);

const { data } = await send('Page.captureScreenshot', {
  format: 'png', fromSurface: true, captureBeyondViewport: false,
});
writeFileSync(out, Buffer.from(data, 'base64'));
console.log(`[cdp] saved ${out} ${width}x${height}`);
const log = consoleLines.find(l => l.includes(`frames=${waitFrames}`) && l.includes('errors='));
if (log) console.log(log);
ws.close();
process.exit(0);
