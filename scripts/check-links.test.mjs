// Edited by: OpenAI-Codex_Responsible: Website
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { checkLinks, scanLinks, formatLinkSuccess } from './check-links.mjs';

function fixture(html, files = []) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'ets2-link-check-'));
  if (process.env.W2_SKIP_CLEANUP) console.log(`TEMP_FIXTURE=${dir}`);
  writeFileSync(path.join(dir, 'index.html'), html);
  for (const file of files) {
    mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
    writeFileSync(path.join(dir, file), file.endsWith('.html') ? '<main>OK</main>' : 'asset');
  }
  return dir;
}

test('valid absolute and relative links, refresh, and external schemes pass', (t) => {
  const dir = fixture('<a href="/base/docs/">Docs</a><img src="asset.svg"><meta http-equiv="refresh" content="0;url=/base/docs/"><a href="https://example.com">External</a><a href="mailto:a@example.com">Mail</a>', ['docs/index.html', 'asset.svg']);
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.deepEqual(checkLinks(dir, '/base/'), []);
});
test('missing base is reported', (t) => {
  const dir = fixture('<a href="/docs/">Docs</a>');
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.match(checkLinks(dir, '/base/')[0], /missing base/);
});
test('missing file and refresh destination are reported', (t) => {
  const dir = fixture('<a href="missing">Missing</a><meta http-equiv="refresh" content="0;url=/base/absent/">');
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.equal(checkLinks(dir, '/base/').filter((problem) => problem.includes('target not found')).length, 2);
});

test('same-page and cross-page anchors are counted and decoded', (t) => {
  const dir = fixture('<h1 id="local">Local</h1><a href="#local">Local</a><a href="/base/docs/#two%20words">Docs</a>', ['docs/index.html']);
  writeFileSync(path.join(dir, 'docs/index.html'), '<h2 name="two words">Docs</h2>');
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  const result = scanLinks(dir, '/base/');
  assert.deepEqual(result.problems, []);
  assert.equal(result.anchors, 2);
  assert.equal(formatLinkSuccess(result), 'Links OK: 2 pages scanned, 2 internal links checked (2 anchors), 0 problems');
});
test('missing same-page anchor fails', (t) => {
  const dir = fixture('<a href="#absent">Missing</a>');
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.match(checkLinks(dir, '/base/')[0], /missing anchor/);
});
test('missing cross-page anchor fails', (t) => {
  const dir = fixture('<a href="/base/docs/#absent">Missing</a>', ['docs/index.html']);
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.match(checkLinks(dir, '/base/')[0], /missing anchor/);
});
test('zero HTML pages fails', (t) => {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'ets2-link-check-'));
  if (process.env.W2_SKIP_CLEANUP) console.log(`TEMP_FIXTURE=${dir}`);
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.match(checkLinks(dir, '/base/').join('\n'), /No HTML pages found/);
});
test('zero internal links fails', (t) => {
  const dir = fixture('<a href="https://example.com">External</a><a href="#">Top</a>');
  t.after(() => { if (!process.env.W2_SKIP_CLEANUP) rmSync(dir, { recursive: true, force: true }); });
  assert.match(checkLinks(dir, '/base/')[0], /No internal links found/);
});
