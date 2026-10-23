// Edited by: OpenAI-Codex_Responsible: Website
import assert from "node:assert/strict";
import test from "node:test";
import { checkLicenses, parseSpdx } from "./check-licenses.mjs";

const policy = {
  allowed: [
    "MIT",
    "ISC",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "Apache-2.0",
    "0BSD",
    "BlueOak-1.0.0",
    "CC0-1.0",
    "Unlicense",
    "Python-2.0",
  ],
  buildTimeExceptions: [
    {
      namePattern: "@img/sharp-*",
      licenses: [
        "LGPL-3.0-or-later",
        "Apache-2.0 AND LGPL-3.0-or-later",
        "Apache-2.0 AND LGPL-3.0-or-later AND MIT",
      ],
      reason: "test",
    },
    {
      namePattern: "lightningcss*",
      licenses: ["MPL-2.0"],
      reason: "test",
    },
  ],
  noLicenseExceptions: [],
};

function check(name, license, dev = true) {
  return checkLicenses(
    {
      lockfileVersion: 3,
      packages: {
        "": {},
        ["node_modules/" + name]: {
          name,
          ...(license === undefined ? {} : { license }),
          dev,
        },
      },
    },
    policy,
  );
}

test("allows an allowlisted license", () => {
  const result = check("example", "MIT", false);
  assert.equal(result.checked, 1);
  assert.equal(result.production, 1);
  assert.equal(result.dev, 0);
  assert.deepEqual(result.problems, []);
});

test("rejects a production dependency with LGPL even when its name has a dev exception", () => {
  const result = check("@img/sharp-linux-x64", "LGPL-3.0-or-later", false);
  assert.match(result.problems[0], /production package cannot use a build-time license exception/);
});

test("rejects a changed license outside the exact exception list", () => {
  const result = check("@img/sharp-linux-x64", "LGPL-2.1-only");
  assert.match(result.problems[0], /is not listed for matching build-time exception/);
});

test("rejects a new non-allowlisted license without an exception", () => {
  const result = check("new-package", "BUSL-1.1");
  assert.match(result.problems[0], /has no build-time exception/);
});

test("rejects a missing license field", () => {
  const result = check("unlicensed-package", undefined);
  assert.deepEqual(result.problems, ["unlicensed-package: missing license field"]);
});

test("supports SPDX AND and OR with AND precedence", () => {
  const bothAllowed = check("both", "MIT AND BSD-3-Clause");
  const eitherAllowed = check("either", "MIT OR BUSL-1.1");
  const grouped = check("grouped", "(MIT OR BUSL-1.1) AND ISC");
  const conjunctionRejected = check("strict", "MIT AND BUSL-1.1");
  assert.deepEqual(bothAllowed.problems, []);
  assert.deepEqual(eitherAllowed.problems, []);
  assert.deepEqual(grouped.problems, []);
  assert.match(conjunctionRejected.problems[0], /has no build-time exception/);
});

test("rejects SPDX WITH expressions", () => {
  assert.throws(() => parseSpdx("MIT WITH Classpath-exception-2.0"), /WITH expressions are not supported/);
  const result = check("with-license", "MIT WITH Classpath-exception-2.0");
  assert.match(result.problems[0], /unsupported license expression/);
});

test("applies only exact dev exceptions and reports their counts", () => {
  const result = checkLicenses(
    {
      lockfileVersion: 3,
      packages: {
        "node_modules/@img/sharp-linux-x64": {
          name: "@img/sharp-linux-x64",
          license: "Apache-2.0 AND LGPL-3.0-or-later",
          dev: true,
        },
        "node_modules/lightningcss-win32-x64-msvc": {
          name: "lightningcss-win32-x64-msvc",
          license: "MPL-2.0",
          dev: true,
        },
      },
    },
    policy,
  );
  assert.equal(result.exceptionsApplied, 2);
  assert.deepEqual(result.exceptionCounts.map((item) => item.count), [1, 1]);
  assert.deepEqual(result.problems, []);
});

test("warns when exceptions are unused without failing", () => {
  const result = check("ordinary-package", "MIT");
  assert.deepEqual(result.problems, []);
  assert.equal(result.warnings.length, 2);
  assert.match(result.warnings[0], /matched no non-allowlisted packages/);
});
