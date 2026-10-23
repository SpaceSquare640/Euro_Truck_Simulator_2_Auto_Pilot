// Edited by: OpenAI-Codex_Responsible: Website
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

function htmlFiles(dir, root = dir) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? htmlFiles(full, root) : entry.isFile() && entry.name.endsWith('.html') ? [path.relative(root, full)] : [];
  });
}

export function scanLinks(dist, base) {
  const root = path.resolve(dist);
  const normalizedBase = `/${base.split('/').filter(Boolean).join('/')}/`;
  const problems = [];
  const pages = htmlFiles(root);
  let internalLinks = 0;
  let anchors = 0;
  if (pages.length === 0) problems.push('No HTML pages found');
  for (const page of pages) {
    const html = readFileSync(path.join(root, page), 'utf8');
    const links = [...html.matchAll(/\b(?:href|src)\s*=\s*(["'])(.*?)\1/gi)].map((match) => match[2]);
    for (const tag of html.matchAll(/<meta\b[^>]*>/gi)) {
      if (!/http-equiv\s*=\s*(["'])?refresh\1?/i.test(tag[0])) continue;
      const content = tag[0].match(/\bcontent\s*=\s*(["'])(.*?)\1/i)?.[2];
      const target = content?.match(/\burl\s*=\s*(.+)$/i)?.[1]?.trim().replace(/^['"]|['"]$/g, '');
      if (target) links.push(target);
    }
    for (const link of links) {
      if (link === '#' || /^(?:mailto:|tel:|data:|javascript:|\/\/)/i.test(link) || /https?:\/\//i.test(link)) continue;
      internalLinks++;
      const hashAt = link.indexOf('#');
      const fragment = hashAt >= 0 ? link.slice(hashAt + 1) : '';
      if (fragment) anchors++;
      if (link.startsWith('/') && !link.startsWith(normalizedBase)) {
        problems.push(`${page}: ${link}: missing base`);
        continue;
      }
      const clean = link.split(/[?#]/, 1)[0];
      const relative = !clean ? page : link.startsWith('/') ? clean.slice(normalizedBase.length)
        : path.posix.normalize(path.posix.join(path.posix.dirname(page), clean));
      const safe = relative.replace(/^\/+/, '');
      if (safe === '..' || safe.startsWith('../')) {
        problems.push(`${page}: ${link}: outside dist`);
        continue;
      }
      const candidates = clean.endsWith('/') ? [path.posix.join(safe, 'index.html')]
        : [safe, path.posix.join(safe, 'index.html'), `${safe}.html`];
      const target = candidates.find((candidate) => {
        const full = path.join(root, candidate);
        return existsSync(full) && statSync(full).isFile();
      });
      if (!target) {
        problems.push(`${page}: ${link}: target not found`);
      } else if (fragment) {
        let decoded;
        try { decoded = decodeURIComponent(fragment); } catch { decoded = fragment; }
        const targetHtml = target.endsWith('.html') ? readFileSync(path.join(root, target), 'utf8') : '';
        const ids = [...targetHtml.matchAll(/\b(?:id|name)\s*=\s*(["'])(.*?)\1/gi)].map((match) => match[2]);
        if (!ids.includes(decoded)) problems.push(`${page}: ${link}: missing anchor`);
      }
    }
  }
  if (internalLinks === 0) problems.push('No internal links found');
  return { problems, pages: pages.length, internalLinks, anchors };
}

export function checkLinks(dist, base) { return scanLinks(dist, base).problems; }

export function formatLinkSuccess(result) {
  return `Links OK: ${result.pages} pages scanned, ${result.internalLinks} internal links checked (${result.anchors} anchors), 0 problems`;
}

export function main(args = process.argv.slice(2)) {
  const dist = args[args.indexOf('--dist') + 1];
  const base = args[args.indexOf('--base') + 1];
  if (!dist || !base || !args.includes('--dist') || !args.includes('--base')) {
    console.error('Usage: node scripts/check-links.mjs --dist dist --base /path/');
    process.exitCode = 1;
    return;
  }
  const result = scanLinks(dist, base);
  for (const problem of result.problems) console.error(problem);
  if (result.problems.length) process.exitCode = 1;
  else console.log(formatLinkSuccess(result));
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main();
