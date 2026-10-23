// Edited by: OpenAI-Codex_Responsible: Website
import test from 'node:test';
import assert from 'node:assert/strict';
import { checkForbiddenPaths, checkChangelogProtection, checkPinned, evaluateGuard, formatGuardSuccess } from './guard.mjs';

test('forbidden paths include generated, secret, and resource files but not similar names', () => {
  assert.equal(checkForbiddenPaths(['dist/index.html', 'nested/.env.local', 'cert.PEM', 'Resource/a']).length, 4);
  assert.deepEqual(checkForbiddenPaths(['dist.txt', 'src/.env-example', 'src/key.txt']), []);
});
test('changelog allows bot changes and unrelated changes', () => {
  assert.deepEqual(checkChangelogProtection(['src/content/changelog.md'], 'github-actions[bot]'), []);
  assert.deepEqual(checkChangelogProtection(['README.md'], 'someone'), []);
  assert.equal(checkChangelogProtection(['src/content/changelog.md'], 'someone').length, 1);
});
test('pinned actions accept SHA, local, and docker references and report file and line', () => {
  assert.deepEqual(checkPinned([{ path: 'a.yml', content: 'uses: ./local\nuses: docker://alpine\nuses: actions/checkout@' + 'a'.repeat(40) }]), []);
  assert.deepEqual(checkPinned([{ path: 'a.yml', content: 'name: x\n  - uses: actions/checkout@v4' }]),
    ['a.yml:2: action must use a 40-character commit SHA: actions/checkout@v4']);
});

const input = (changes = {}) => ({
  files: ['README.md', '.github/workflows/guard.yml'],
  workflowFiles: [{ path: '.github/workflows/guard.yml', content: `uses: actions/checkout@${'a'.repeat(40)}` }],
  before: 'a'.repeat(40), after: 'HEAD', actor: 'local',
  resolveBefore: () => true, getChangedFiles: () => [], ...changes,
});

test('zero workflows fails even when tracked files exist', () => {
  assert.match(evaluateGuard(input({ workflowFiles: [] })).problems.join('\n'), /No workflow files/);
});
test('new branch and unresolved before have distinct skip reasons', () => {
  assert.equal(evaluateGuard(input({ before: '0'.repeat(40) })).changelogCheck, 'skipped: new branch');
  assert.equal(evaluateGuard(input({ resolveBefore: () => false })).changelogCheck, 'skipped: before cannot be resolved');
});
test('missing revisions have distinct skip reasons', () => {
  assert.equal(evaluateGuard(input({ before: '' })).changelogCheck, 'skipped: missing GUARD_BEFORE');
  assert.equal(evaluateGuard(input({ after: '' })).changelogCheck, 'skipped: missing GUARD_AFTER');
});
test('injected changed files protect changelog and unpinned uses fail', () => {
  assert.equal(evaluateGuard(input({ getChangedFiles: () => ['src/content/changelog.md'] })).problems.length, 1);
  assert.match(evaluateGuard(input({ workflowFiles: [{ path: 'bad.yml', content: 'uses: actions/checkout@v4' }] })).problems[0], /bad.yml:1/);
});
test('success output includes nonzero counts and changelog status', () => {
  assert.equal(formatGuardSuccess(evaluateGuard(input())),
    'Guard OK: 2 tracked files, 1 workflow files checked, changelog check: run');
});
