// Edited by: OpenAI-Codex_Responsible: Website
import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { scanI18n, formatI18nSuccess } from './check-i18n.mjs';

const createdDirectories = new Set();
const script = fileURLToPath(new URL('./check-i18n.mjs', import.meta.url));
const english = {
  greeting: 'Hello {name}',
  label: 'Planned',
  'architecture.node.ui': 'Desktop interface (C#, WPF)',
  'architecture.edge.ui': 'Named pipe: status and requests',
};
const chinese = {
  greeting: '你好 {name}',
  label: '計劃中',
  'architecture.node.ui': '桌面介面 (C#, WPF)',
  'architecture.edge.ui': '具名管道：狀態與請求',
};

function ownedDirectory(t) {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'ets2-i18n-check-'));
  createdDirectories.add(dir);
  t.after(() => {
    const resolved = path.resolve(dir);
    assert.ok(createdDirectories.has(dir), 'Temporary directory is not owned by this test run');
    assert.equal(path.dirname(resolved), path.resolve(os.tmpdir()), 'Temporary directory is outside os.tmpdir()');
    assert.ok(path.basename(resolved).startsWith('ets2-i18n-check-'), 'Temporary directory has the wrong prefix');
    rmSync(dir, { recursive: true, force: true });
  });
  return dir;
}

after(() => {
  assert.ok(createdDirectories.size > 0, 'No temporary directories were checked');
  assert.deepEqual([...createdDirectories].filter((dir) => existsSync(dir)), [], 'Temporary directories remain on disk');
});

function write(root, relative, value) {
  const full = path.join(root, relative);
  mkdirSync(path.dirname(full), { recursive: true });
  writeFileSync(full, typeof value === 'string' ? value : JSON.stringify(value));
}

function fixture(t, options = {}) {
  const root = ownedDirectory(t);
  const registry = options.registry ?? {
    default: 'en',
    locales: [
      { code: 'en', htmlLang: 'en', autonym: 'English', script: 'Latn', enabled: true },
      { code: 'zh-hant', htmlLang: 'zh-Hant', autonym: '繁體中文', script: 'Han', enabled: true },
    ],
  };
  write(root, 'src/i18n/locales.json', registry);
  write(root, 'scripts/i18n-policy.json', options.policy ?? {
    identicalAllowed: [],
    layoutBudgets: [
      { keyPattern: '^architecture\\.node\\.', splitAt: ' (', maxUnitsPerLine: 22 },
      { keyPattern: '^architecture\\.edge\\.', maxUnitsPerLine: 31 },
    ],
  });
  if (options.en !== null) write(root, 'src/i18n/en.json', options.en ?? english);
  if (options.zh !== null) write(root, 'src/i18n/zh-hant.json', options.zh ?? chinese);
  for (const page of options.enPages ?? ['index.astro', 'docs/index.astro']) {
    write(root, path.join('src/pages/en', page), '<p>English</p>');
  }
  for (const page of options.zhPages ?? ['index.astro', 'docs/index.astro']) {
    write(root, path.join('src/pages/zh-hant', page), '<p>中文</p>');
  }
  return root;
}

test('complete English and Han fixture reports real nonzero counts', (t) => {
  const result = scanI18n(fixture(t));
  assert.deepEqual(result.problems, []);
  assert.equal(formatI18nSuccess(result), 'i18n OK: 2 locales (en, zh-hant), 4 keys each, 2 pages each, 0 problems');
});

test('missing and extra keys are both reported', (t) => {
  const root = fixture(t, { zh: { ...chinese, extra: '額外欄位' } });
  const result = scanI18n(root);
  assert.match(result.problems.join('\n'), /zh-hant: extra key extra/);
  const withoutGreeting = { ...chinese };
  delete withoutGreeting.greeting;
  const second = scanI18n(fixture(t, { zh: withoutGreeting }));
  assert.match(second.problems.join('\n'), /zh-hant: missing key greeting/);
});

test('empty and whitespace-only values fail', (t) => {
  const result = scanI18n(fixture(t, { en: { ...english, greeting: '' }, zh: { ...chinese, label: '   ' } }));
  assert.match(result.problems.join('\n'), /en: greeting: empty value/);
  assert.match(result.problems.join('\n'), /zh-hant: label: empty value/);
});

test('placeholder mismatch is reported', (t) => {
  const result = scanI18n(fixture(t, { zh: { ...chinese, greeting: '你好 {person}' } }));
  assert.match(result.problems.join('\n'), /zh-hant: greeting: placeholder mismatch/);
});

test('untranslated and non-Han values fail unless exact keys are allowed', (t) => {
  const zh = { ...chinese, greeting: english.greeting, label: 'Translated' };
  const failed = scanI18n(fixture(t, { zh }));
  assert.match(failed.problems.join('\n'), /zh-hant: greeting: untranslated value/);
  assert.match(failed.problems.join('\n'), /zh-hant: label: no Han character/);
  const allowed = scanI18n(fixture(t, {
    zh,
    policy: { identicalAllowed: ['greeting', 'label'], layoutBudgets: [] },
  }));
  assert.deepEqual(allowed.problems, []);
});

test('missing route in another locale is reported', (t) => {
  const result = scanI18n(fixture(t, { zhPages: ['index.astro'] }));
  assert.match(result.problems.join('\n'), /zh-hant: missing page docs\/index\.astro/);
});

test('node and edge budgets reject long lines after splitting labels', (t) => {
  const label = english['architecture.node.ui'];
  const lines = [label.slice(0, label.indexOf(' (')), label.slice(label.indexOf(' (') + 1)];
  assert.deepEqual(lines.map((line) => [...line].length), [17, 9]);
  const en = {
    ...english,
    'architecture.node.ui': 'Desktop interface extended (C#, WPF)',
    'architecture.edge.ui': 'Named pipe: status and requests extended',
  };
  const result = scanI18n(fixture(t, { en }));
  assert.match(result.problems.join('\n'), /en: architecture\.node\.ui: layout budget/);
  assert.match(result.problems.join('\n'), /en: architecture\.edge\.ui: layout budget/);
});

test('registry and dictionary errors identify the affected locale', (t) => {
  const entries = [
    { code: 'en', htmlLang: 'en', autonym: 'English', script: 'Latn', enabled: true },
    { code: 'zh-hant', htmlLang: 'zh-Hant', autonym: '繁體中文', script: 'Han', enabled: true },
  ];
  const missingDefault = scanI18n(fixture(t, { registry: { default: 'fr', locales: entries } }));
  assert.match(missingDefault.problems.join('\n'), /default locale is missing or disabled/);
  const disabled = scanI18n(fixture(t, { registry: { default: 'en', locales: [{ ...entries[0], enabled: false }, entries[1]] } }));
  assert.match(disabled.problems.join('\n'), /default locale is missing or disabled/);
  const duplicate = scanI18n(fixture(t, { registry: { default: 'en', locales: [entries[0], entries[0], entries[1]] } }));
  assert.match(duplicate.problems.join('\n'), /duplicate code/);
  const uppercase = scanI18n(fixture(t, { registry: { default: 'en', locales: [entries[0], { ...entries[1], code: 'ZH-hant' }] } }));
  assert.match(uppercase.problems.join('\n'), /invalid code/);
  const missingDictionary = scanI18n(fixture(t, { zh: null }));
  assert.match(missingDictionary.problems.join('\n'), /zh-hant\.json: invalid or missing JSON/);
  const orphanRoot = fixture(t);
  write(orphanRoot, 'src/i18n/fr.json', { greeting: 'Bonjour' });
  assert.match(scanI18n(orphanRoot).problems.join('\n'), /fr\.json: orphan dictionary/);
});

test('hard-coded locale paths in components, layouts and views fail but page wrappers are exempt', (t) => {
  const root = fixture(t);
  const literal = 'href={' + '\x60' + '${base}en/docs/' + '\x60' + '}';
  write(root, 'src/components/Sample.astro', literal);
  write(root, 'src/layouts/Sample.astro', literal);
  write(root, 'src/views/SampleView.astro', literal);
  write(root, 'src/pages/en/legacy.astro', literal);
  write(root, 'src/pages/zh-hant/legacy.astro', literal);
  const result = scanI18n(root);
  assert.equal(result.problems.filter((problem) => problem.includes('hard-coded locale path')).length, 3);
  assert.ok(!result.problems.some((problem) => problem.includes('pages/en/legacy.astro')));
});

test('zero locales, keys and pages are all failures', (t) => {
  const emptyLocales = scanI18n(fixture(t, { registry: { default: 'en', locales: [] } }));
  assert.match(emptyLocales.problems.join('\n'), /No enabled locales checked/);
  const emptyKeys = scanI18n(fixture(t, { en: {} }));
  assert.match(emptyKeys.problems.join('\n'), /No dictionary keys checked/);
  const emptyPages = scanI18n(fixture(t, { enPages: [] }));
  assert.match(emptyPages.problems.join('\n'), /no pages checked/);
});

test('CLI succeeds, fails and prints usage with the matching exit codes', (t) => {
  const valid = fixture(t);
  const ok = spawnSync(process.execPath, [script, '--root', valid], { encoding: 'utf8' });
  assert.equal(ok.status, 0);
  assert.equal(ok.stdout.trim(), 'i18n OK: 2 locales (en, zh-hant), 4 keys each, 2 pages each, 0 problems');
  const invalid = fixture(t, { zhPages: ['index.astro'] });
  const failed = spawnSync(process.execPath, [script, '--root', invalid], { encoding: 'utf8' });
  assert.equal(failed.status, 1);
  assert.match(failed.stderr, /zh-hant: missing page/);
  const usage = spawnSync(process.execPath, [script], { encoding: 'utf8' });
  assert.equal(usage.status, 1);
  assert.match(usage.stderr, /Usage: node scripts\/check-i18n\.mjs --root/);
});
