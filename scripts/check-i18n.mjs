// Edited by: OpenAI-Codex_Responsible: Website
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const codePattern = /^[a-z]{2,3}(-[a-z0-9]+)*$/;
const hanPattern = /\p{Script=Han}/u;

function readJson(file, problems) {
  try {
    return JSON.parse(readFileSync(file, 'utf8'));
  } catch (error) {
    problems.push(`${file}: invalid or missing JSON (${error.message})`);
    return null;
  }
}

function sourceFiles(dir) {
  if (!existsSync(dir)) return [];
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? sourceFiles(full)
      : entry.isFile() && /\.(?:astro|ts)$/.test(entry.name) ? [full] : [];
  });
}

function pagesFor(root, code) {
  const dir = path.join(root, 'src', 'pages', code);
  if (!existsSync(dir)) return [];
  return sourceFiles(dir).filter((file) => file.endsWith('.astro'))
    .map((file) => path.relative(dir, file).replaceAll(path.sep, '/')).sort();
}

function placeholders(value) {
  return [...new Set([...value.matchAll(/\{([^{}]+)\}/g)].map((match) => match[1]))].sort();
}

function displayWidth(value) {
  return [...value].reduce((total, character) => {
    const point = character.codePointAt(0);
    const wide = (point >= 0x3000 && point <= 0x303f)
      || (point >= 0x3400 && point <= 0x4dbf)
      || (point >= 0x4e00 && point <= 0x9fff)
      || (point >= 0xff00 && point <= 0xffef);
    return total + (wide ? 2 : 1);
  }, 0);
}

function budgetLines(value, splitAt) {
  if (!splitAt || !value.includes(splitAt)) return [value];
  const index = value.indexOf(splitAt);
  return [value.slice(0, index).trimEnd(), value.slice(index).trimStart()];
}

export function scanI18n(root) {
  const project = path.resolve(root);
  const problems = [];
  const registry = readJson(path.join(project, 'src/i18n/locales.json'), problems);
  const policy = readJson(path.join(project, 'scripts/i18n-policy.json'), problems);
  const entries = Array.isArray(registry?.locales) ? registry.locales : [];
  if (!Array.isArray(registry?.locales)) problems.push('src/i18n/locales.json: locales must be an array');
  const seen = new Set();
  for (const [index, entry] of entries.entries()) {
    const label = `src/i18n/locales.json: locale ${entry?.code ?? index}`;
    if (!codePattern.test(entry?.code ?? '')) problems.push(`${label}: invalid code`);
    if (seen.has(entry?.code)) problems.push(`${label}: duplicate code`);
    seen.add(entry?.code);
    if (typeof entry?.htmlLang !== 'string' || !entry.htmlLang.trim()) problems.push(`${label}: missing htmlLang`);
    if (typeof entry?.autonym !== 'string' || !entry.autonym.trim()) problems.push(`${label}: missing autonym`);
    if (entry?.script !== 'Latn' && entry?.script !== 'Han') problems.push(`${label}: invalid script`);
    if (typeof entry?.enabled !== 'boolean') problems.push(`${label}: enabled must be boolean`);
  }
  if (typeof registry?.default !== 'string' || !entries.some((entry) => entry.code === registry.default && entry.enabled === true)) {
    problems.push('src/i18n/locales.json: default locale is missing or disabled');
  }
  const enabled = entries.filter((entry) => entry?.enabled === true && codePattern.test(entry.code ?? ''));
  const dictionaries = new Map();
  const i18nDir = path.join(project, 'src/i18n');
  for (const entry of enabled) {
    const file = path.join(i18nDir, `${entry.code}.json`);
    const dictionary = readJson(file, problems);
    if (!dictionary || typeof dictionary !== 'object' || Array.isArray(dictionary)) {
      problems.push(`${entry.code}: dictionary must be a JSON object at ${file}`);
      continue;
    }
    dictionaries.set(entry.code, dictionary);
    for (const [key, value] of Object.entries(dictionary)) {
      if (typeof value !== 'string') problems.push(`${entry.code}: ${key}: value must be a string`);
      else if (!value.trim()) problems.push(`${entry.code}: ${key}: empty value`);
    }
  }
  if (existsSync(i18nDir)) {
    for (const filename of readdirSync(i18nDir).filter((name) => name.endsWith('.json') && name !== 'locales.json')) {
      const code = filename.slice(0, -5);
      if (!seen.has(code)) problems.push(`${filename}: orphan dictionary for ${code}`);
    }
  }
  const source = dictionaries.get(registry?.default);
  const sourceKeys = source ? Object.keys(source).sort() : [];
  const allowed = Array.isArray(policy?.identicalAllowed) ? new Set(policy.identicalAllowed) : new Set();
  const budgets = Array.isArray(policy?.layoutBudgets) ? policy.layoutBudgets : [];
  if (!policy || !Array.isArray(policy.identicalAllowed) || !Array.isArray(policy.layoutBudgets)) {
    problems.push('scripts/i18n-policy.json: invalid policy');
  }
  for (const entry of enabled) {
    const dictionary = dictionaries.get(entry.code);
    if (!dictionary) continue;
    if (entry.code !== registry?.default) {
      for (const key of sourceKeys) {
        if (!Object.hasOwn(dictionary, key)) problems.push(`${entry.code}: missing key ${key}`);
      }
      for (const key of Object.keys(dictionary)) {
        if (!Object.hasOwn(source ?? {}, key)) problems.push(`${entry.code}: extra key ${key}`);
      }
    }
    for (const [key, value] of Object.entries(dictionary)) {
      if (typeof value !== 'string') continue;
      const original = source?.[key];
      if (entry.code !== registry?.default && typeof original === 'string') {
        if (JSON.stringify(placeholders(value)) !== JSON.stringify(placeholders(original))) {
          problems.push(`${entry.code}: ${key}: placeholder mismatch`);
        }
        if (!allowed.has(key) && value === original) problems.push(`${entry.code}: ${key}: untranslated value`);
        if (!allowed.has(key) && entry.script === 'Han' && !hanPattern.test(value)) {
          problems.push(`${entry.code}: ${key}: no Han character`);
        }
      }
      for (const budget of budgets) {
        let pattern;
        try { pattern = new RegExp(budget.keyPattern); }
        catch { problems.push(`scripts/i18n-policy.json: invalid keyPattern ${budget.keyPattern}`); continue; }
        if (!pattern.test(key)) continue;
        for (const line of budgetLines(value, budget.splitAt)) {
          const width = displayWidth(line);
          if (width > budget.maxUnitsPerLine) {
            problems.push(`${entry.code}: ${key}: layout budget ${width} > ${budget.maxUnitsPerLine} for ${JSON.stringify(line)}`);
          }
        }
      }
    }
  }
  const defaultPages = pagesFor(project, registry?.default ?? '');
  for (const entry of enabled) {
    if (entry.code === registry?.default) continue;
    const actual = pagesFor(project, entry.code);
    for (const page of defaultPages) if (!actual.includes(page)) problems.push(`${entry.code}: missing page ${page}`);
    for (const page of actual) if (!defaultPages.includes(page)) problems.push(`${entry.code}: extra page ${page}`);
  }
  const scan = [
    ...sourceFiles(path.join(project, 'src/components')),
    ...sourceFiles(path.join(project, 'src/layouts')),
    ...sourceFiles(path.join(project, 'src/views')),
    path.join(project, 'src/pages/index.astro'),
  ].filter(existsSync);
  const codes = entries.filter((entry) => typeof entry?.code === 'string').map((entry) => entry.code);
  for (const file of scan) {
    const content = readFileSync(file, 'utf8');
    for (const code of codes) {
      const literal = `}${code}/`;
      if (content.includes(literal)) problems.push(`${path.relative(project, file)}: hard-coded locale path ${literal}`);
    }
  }
  if (enabled.length === 0) problems.push('No enabled locales checked');
  if (sourceKeys.length === 0) problems.push('No dictionary keys checked');
  if (defaultPages.length === 0) problems.push(`${registry?.default ?? 'default'}: no pages checked`);
  return { problems, locales: enabled.map((entry) => entry.code), keys: sourceKeys.length, pages: defaultPages.length };
}

export function formatI18nSuccess(result) {
  return `i18n OK: ${result.locales.length} locales (${result.locales.join(', ')}), ${result.keys} keys each, ${result.pages} pages each, 0 problems`;
}

export function main(args = process.argv.slice(2)) {
  const index = args.indexOf('--root');
  const root = index >= 0 ? args[index + 1] : undefined;
  if (!root) {
    console.error('Usage: node scripts/check-i18n.mjs --root <directory>');
    process.exitCode = 1;
    return;
  }
  const result = scanI18n(root);
  for (const problem of result.problems) console.error(problem);
  if (result.problems.length) process.exitCode = 1;
  else console.log(formatI18nSuccess(result));
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main();
