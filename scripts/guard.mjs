// Edited by: OpenAI-Codex_Responsible: Website
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const forbiddenPrefixes = ['node_modules/', 'dist/', '.astro/', '.claude/', '.codex/', 'Resource/'];

export function checkForbiddenPaths(files) {
  return files.filter((file) => {
    const path = file.replaceAll('\\', '/');
    const name = path.split('/').at(-1);
    return forbiddenPrefixes.some((prefix) => path.startsWith(prefix)) ||
      name === '.env' || name.startsWith('.env.') || /\.(pem|key|pfx)$/i.test(name);
  }).map((file) => `Forbidden tracked path: ${file}`);
}

export function checkChangelogProtection(changedFiles, actor) {
  return changedFiles.includes('src/content/changelog.md') && actor !== 'github-actions[bot]'
    ? [`src/content/changelog.md may only be changed by github-actions[bot] (actor: ${actor || 'unknown'})`]
    : [];
}

export function checkPinned(workflowFiles) {
  const problems = [];
  for (const { path, content } of workflowFiles) {
    content.split(/\r?\n/).forEach((line, index) => {
      const match = line.match(/^\s*(?:-\s*)?uses:\s*([^\s#]+)/);
      if (match && !match[1].startsWith('./') && !match[1].startsWith('docker://') &&
          !/@[a-fA-F0-9]{40}$/.test(match[1])) {
        problems.push(`${path}:${index + 1}: action must use a 40-character commit SHA: ${match[1]}`);
      }
    });
  }
  return problems;
}

function git(...args) {
  return execFileSync('git', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
}

export function evaluateGuard({ files, workflowFiles, before, after, actor, resolveBefore, getChangedFiles }) {
  const problems = [...checkForbiddenPaths(files), ...checkPinned(workflowFiles)];
  if (files.length === 0) problems.push('No tracked files found');
  if (workflowFiles.length === 0) problems.push('No workflow files found');

  let changelogCheck = 'run';
  if (!before) changelogCheck = 'skipped: missing GUARD_BEFORE';
  else if (!after) changelogCheck = 'skipped: missing GUARD_AFTER';
  else if (/^0+$/.test(before)) changelogCheck = 'skipped: new branch';
  else {
    try {
      if (!resolveBefore(before)) changelogCheck = 'skipped: before cannot be resolved';
    } catch {
      changelogCheck = 'skipped: before cannot be resolved';
    }
    if (changelogCheck === 'run') {
      problems.push(...checkChangelogProtection(getChangedFiles(before, after), actor));
    }
  }
  return { problems, trackedFiles: files.length, workflowFiles: workflowFiles.length, changelogCheck };
}

export function formatGuardSuccess(result) {
  return `Guard OK: ${result.trackedFiles} tracked files, ${result.workflowFiles} workflow files checked, changelog check: ${result.changelogCheck}`;
}

export function main() {
  const files = git('ls-files', '-z').split('\0').filter(Boolean);
  const workflowFiles = files.filter((path) => /^\.github\/workflows\/[^/]+\.yml$/.test(path))
    .map((path) => ({ path, content: readFileSync(path, 'utf8') }));
  const result = evaluateGuard({
    files, workflowFiles,
    before: process.env.GUARD_BEFORE, after: process.env.GUARD_AFTER, actor: process.env.GUARD_ACTOR,
    resolveBefore: (before) => Boolean(git('rev-parse', '--verify', `${before}^{commit}`).trim()),
    getChangedFiles: (before, after) => git('diff', '--name-only', '-z', before, after).split('\0').filter(Boolean),
  });
  for (const problem of result.problems) console.error(problem);
  if (result.problems.length) process.exitCode = 1;
  else console.log(formatGuardSuccess(result));
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main();
