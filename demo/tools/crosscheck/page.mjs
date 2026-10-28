// The page's half of the crosscheck: loads the demo in headless Chrome and
// renders each case through window.isohypseDemo.renderCase, writing RGBA8
// (top row first) to <out>/<name>.rgba. Run by crosscheck.py; by hand:
//
//     node page.mjs <url> <cases.json> <out-dir>
//
// Chrome is driven over --remote-debugging-pipe, so it opens no port of its
// own. CHROME names the binary; ISOHYPSE_GL=swiftshader renders on the CPU
// instead of this machine's GPU (through ANGLE on Metal).
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [url, casesPath, outDir] = process.argv.slice(2);
const cases = JSON.parse(readFileSync(casesPath, 'utf8'));
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const profile = mkdtempSync(join(process.env.TMPDIR ?? tmpdir(), 'isohypse-chrome-'));
const gl = process.env.ISOHYPSE_GL === 'swiftshader'
  ? ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
  : ['--use-angle=metal', '--enable-gpu', '--ignore-gpu-blocklist'];
const chrome = spawn(CHROME, ['--headless=new', '--no-first-run', '--no-default-browser-check', `--user-data-dir=${profile}`,
  '--remote-debugging-pipe', '--window-size=1600,1200', ...gl, 'about:blank'], { stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe'] });

let nextId = 1;
const waiting = new Map();
const problems = [];
let buffer = Buffer.alloc(0);
chrome.stdio[4].on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  for (let at = buffer.indexOf(0); at >= 0; at = buffer.indexOf(0)) {
    const msg = JSON.parse(buffer.subarray(0, at).toString('utf8'));
    buffer = buffer.subarray(at + 1);
    if (msg.id && waiting.has(msg.id)) {
      const { resolve, reject } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message)); else resolve(msg.result);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      problems.push(msg.params.exceptionDetails.exception?.description ?? msg.params.exceptionDetails.text);
    } else if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
      problems.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
    }
  }
});
const send = (method, params = {}, sessionId = undefined) => {
  const id = nextId++;
  chrome.stdio[3].write(`${JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) })}\0`);
  return new Promise((resolve, reject) => waiting.set(id, { resolve, reject }));
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let code = 0;
try {
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  const S = (m, p) => send(m, p, sessionId);
  const evaluate = async (expression) => {
    const r = await S('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
    return r.result.value;
  };
  await S('Runtime.enable');
  await S('Page.navigate', { url });
  for (let i = 0; i < 200 && !(await evaluate('!!window.isohypseDemo?.renderer').catch(() => false)); i += 1) await sleep(100);
  for (const c of cases) {
    const clip = c.clip ? JSON.stringify(readFileSync(c.clip).toString('base64')) : 'null';
    const result = await evaluate(`window.isohypseDemo.renderCase({ variant: ${JSON.stringify(c.variant)}, settings: ${JSON.stringify(c.settings)}, frames: ${c.frames}, width: ${c.width}, height: ${c.height}, clip: ${clip} })`);
    writeFileSync(join(outDir, `${c.name}.rgba`), Buffer.from(result.rgba, 'base64'));
    writeFileSync(join(outDir, `${c.name}.json`), JSON.stringify({ sweep: result.sweep, lines: result.lines, datum: result.datum, format: result.format }));
  }
} catch (error) {
  problems.push(error.message);
  code = 1;
}
for (const p of problems) console.error(`page: ${p}`);
try { await send('Browser.close'); } catch { /* gone */ }
await Promise.race([new Promise((r) => chrome.once('exit', r)), sleep(3000)]);
chrome.kill('SIGKILL');
await sleep(200);
try { rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }); } catch { /* Chrome's leftovers */ }
process.exit(problems.length ? 1 : code);
