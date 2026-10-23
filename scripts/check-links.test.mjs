// Edited by: OpenAI-Codex_Responsible: Website
import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { checkLinks, scanLinks, formatLinkSuccess } from './check-links.mjs';

const createdDirectories = new Set();

function ownedDirectory(t) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'ets2-link-check-'));
  createdDirectories.add(dir);
  t.after(() => {
    const resolved = path.resolve(dir);
    assert.ok(createdDirectories.has(dir), 'Temporary directory is not owned by this test run');
    assert.equal(path.dirname(resolved), path.resolve(os.tmpdir()), 'Temporary directory is outside os.tmpdir()');
    assert.ok(path.basename(resolved).startsWith('ets2-link-check-'), 'Temporary directory has the wrong prefix');
    rmSync(dir, { recursive: true, force: true });
  });
  return dir;
}

after(() => {
  assert.ok(createdDirectories.size > 0, 'No temporary directories were checked');
  assert.deepEqual([...createdDirectories].filter((dir) => existsSync(dir)), [], 'Temporary directories remain on disk');
});

function fixture(t, html, files = []) {
  const dir = ownedDirectory(t);
  writeFileSync(path.join(dir, 'index.html'), html);
  for (const file of files) {
    mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
    writeFileSync(path.join(dir, file), file.endsWith('.html') ? '<main>OK</main>' : 'asset');
  }
  return dir;
}

test('valid absolute and relative links, refresh, and external schemes pass', (t) => {
  const dir = fixture(t, '<a href="/base/docs/">Docs</a><img src="asset.svg"><meta http-equiv="refresh" content="0;url=/base/docs/"><a href="https://example.com">External</a><a href="mailto:a@example.com">Mail</a>', ['docs/index.html', 'asset.svg']);
  assert.deepEqual(checkLinks(dir, '/base/'), []);
});
test('missing base is reported', (t) => {
  const dir = fixture(t, '<a href="/docs/">Docs</a>');
  assert.match(checkLinks(dir, '/base/')[0], /missing base/);
});
test('missing file and refresh destination are reported', (t) => {
  const dir = fixture(t, '<a href="missing">Missing</a><meta http-equiv="refresh" content="0;url=/base/absent/">');
  assert.equal(checkLinks(dir, '/base/').filter((problem) => problem.includes('target not found')).length, 2);
});

test('same-page and cross-page anchors are counted and decoded', (t) => {
  const dir = fixture(t, '<h1 id="local">Local</h1><a href="#local">Local</a><a href="/base/docs/#two%20words">Docs</a>', ['docs/index.html']);
  writeFileSync(path.join(dir, 'docs/index.html'), '<h2 name="two words">Docs</h2>');
  const result = scanLinks(dir, '/base/');
  assert.deepEqual(result.problems, []);
  assert.equal(result.anchors, 2);
  assert.equal(formatLinkSuccess(result), 'Links OK: 2 pages scanned, 2 internal links checked (2 anchors), 0 problems');
});
test('missing same-page anchor fails', (t) => {
  const dir = fixture(t, '<a href="#absent">Missing</a>');
  assert.match(checkLinks(dir, '/base/')[0], /missing anchor/);
});
test('missing cross-page anchor fails', (t) => {
  const dir = fixture(t, '<a href="/base/docs/#absent">Missing</a>', ['docs/index.html']);
  assert.match(checkLinks(dir, '/base/')[0], /missing anchor/);
});
test('zero HTML pages fails', (t) => {
  const dir = ownedDirectory(t);
  assert.match(checkLinks(dir, '/base/').join('\n'), /No HTML pages found/);
});
test('zero internal links fails', (t) => {
  const dir = fixture(t, '<a href="https://example.com">External</a><a href="#">Top</a>');
  assert.match(checkLinks(dir, '/base/')[0], /No internal links found/);
});
