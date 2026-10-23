// Edited by: OpenAI-Codex_Responsible: Website
import assert from "node:assert/strict";
import test from "node:test";
import { checkLockfile } from "./check-lockfile.mjs";

const goodPackage = {
  resolved: "https://registry.npmjs.org/example/-/example-1.0.0.tgz",
  integrity: "sha512-example",
};

test("accepts a complete npm v3 lockfile", () => {
  const result = checkLockfile({
    lockfileVersion: 3,
    packages: { "": {}, "node_modules/example": goodPackage },
  });
  assert.equal(result.checked, 1);
  assert.deepEqual(result.problems, []);
});

test("reports missing resolved and integrity fields", () => {
  const result = checkLockfile({
    lockfileVersion: 3,
    packages: { "node_modules/example": {} },
  });
  assert.equal(result.checked, 1);
  assert.deepEqual(result.problems, [
    "node_modules/example: missing resolved URL",
    "node_modules/example: missing integrity",
  ]);
});

test("rejects non-npm, non-HTTPS, and malformed sources", () => {
  const result = checkLockfile({
    lockfileVersion: 3,
    packages: {
      "node_modules/git-source": {
        resolved: "git+https://example.com/repo.git",
        integrity: "sha512-example",
      },
      "node_modules/http-source": {
        resolved: "http://registry.npmjs.org/example.tgz",
        integrity: "sha512-example",
      },
      "node_modules/other-host": {
        resolved: "https://example.com/example.tgz",
        integrity: "sha512-example",
      },
      "node_modules/bad-url": { resolved: "not a URL", integrity: "sha512-example" },
    },
  });
  assert.equal(result.checked, 4);
  assert.equal(result.problems.length, 4);
});

test("does not require registry fields on links and bundled entries", () => {
  const result = checkLockfile({
    lockfileVersion: 3,
    packages: {
      "node_modules/workspace-link": { link: true },
      "node_modules/bundled": { inBundle: true },
    },
  });
  assert.equal(result.checked, 2);
  assert.deepEqual(result.problems, []);
});

test("rejects lockfile versions other than 3", () => {
  const result = checkLockfile({ lockfileVersion: 2, packages: {} });
  assert.deepEqual(result.problems, ["package-lock.json must use lockfileVersion 3"]);
});
