// Edited by: OpenAI-Codex_Responsible: Website
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

export function parseSpdx(expression) {
  if (typeof expression !== "string" || expression.trim().length === 0) {
    throw new Error("SPDX expression must be a non-empty string");
  }
  if (/\bWITH\b/.test(expression)) {
    throw new Error("SPDX WITH expressions are not supported");
  }

  const tokens = [];
  const tokenPattern = /\s*(\(|\)|AND\b|OR\b|[A-Za-z0-9][A-Za-z0-9.+-]*)/y;
  let offset = 0;
  while (offset < expression.length) {
    if (expression.slice(offset).trim().length === 0) break;
    tokenPattern.lastIndex = offset;
    const match = tokenPattern.exec(expression);
    if (!match) {
      throw new Error("unsupported token in SPDX expression");
    }
    tokens.push(match[1]);
    offset = tokenPattern.lastIndex;
  }
  if (tokens.length === 0) {
    throw new Error("SPDX expression must contain a license identifier");
  }

  let cursor = 0;
  function parsePrimary() {
    const token = tokens[cursor];
    if (token === "(") {
      cursor += 1;
      const node = parseOr();
      if (tokens[cursor] !== ")") {
        throw new Error("missing closing parenthesis in SPDX expression");
      }
      cursor += 1;
      return node;
    }
    if (!token || token === ")" || token === "AND" || token === "OR") {
      throw new Error("expected a license identifier in SPDX expression");
    }
    cursor += 1;
    return { type: "license", value: token };
  }

  function parseAnd() {
    let node = parsePrimary();
    while (tokens[cursor] === "AND") {
      cursor += 1;
      node = { type: "and", left: node, right: parsePrimary() };
    }
    return node;
  }

  function parseOr() {
    let node = parseAnd();
    while (tokens[cursor] === "OR") {
      cursor += 1;
      node = { type: "or", left: node, right: parseAnd() };
    }
    return node;
  }

  const ast = parseOr();
  if (cursor !== tokens.length) {
    throw new Error("unexpected token in SPDX expression: " + tokens[cursor]);
  }
  return ast;
}

function expressionIsAllowed(node, allowed) {
  if (node.type === "license") return allowed.has(node.value);
  if (node.type === "and") {
    return expressionIsAllowed(node.left, allowed) && expressionIsAllowed(node.right, allowed);
  }
  return expressionIsAllowed(node.left, allowed) || expressionIsAllowed(node.right, allowed);
}

function packageName(key, entry) {
  if (entry && typeof entry.name === "string" && entry.name.length > 0) {
    return entry.name;
  }
  const marker = "node_modules/";
  const markerIndex = key.lastIndexOf(marker);
  return markerIndex >= 0 ? key.slice(markerIndex + marker.length) : key;
}

function globMatches(pattern, name) {
  if (pattern.endsWith("*") && pattern.indexOf("*") === pattern.length - 1) {
    return name.startsWith(pattern.slice(0, -1));
  }
  return pattern === name;
}

export function checkLicenses(lockfile, policy) {
  const problems = [];
  const warnings = [];
  const packages =
    lockfile && typeof lockfile.packages === "object" && lockfile.packages !== null
      ? lockfile.packages
      : null;

  if (!packages) {
    return {
      checked: 0,
      production: 0,
      dev: 0,
      exceptionsApplied: 0,
      exceptionCounts: [],
      problems: ["package-lock.json must contain a packages object"],
      warnings: [],
    };
  }
  if (!policy || !Array.isArray(policy.allowed) || !Array.isArray(policy.buildTimeExceptions)) {
    return {
      checked: 0,
      production: 0,
      dev: 0,
      exceptionsApplied: 0,
      exceptionCounts: [],
      problems: ["license policy must define allowed and buildTimeExceptions arrays"],
      warnings: [],
    };
  }

  const entries = Object.entries(packages).filter(([key]) => key !== "");
  const allowed = new Set(policy.allowed);
  const noLicenseExceptions = new Set(policy.noLicenseExceptions || []);
  const exceptionCounts = policy.buildTimeExceptions.map(() => 0);
  let production = 0;
  let dev = 0;
  let exceptionsApplied = 0;

  for (const [key, entry] of entries) {
    const name = packageName(key, entry);
    const isDev = Boolean(entry && entry.dev === true);
    if (isDev) dev += 1;
    else production += 1;

    const license = entry && entry.license;
    if (typeof license !== "string" || license.trim().length === 0) {
      if (!noLicenseExceptions.has(name)) {
        problems.push(name + ": missing license field");
      }
      continue;
    }

    let ast;
    try {
      ast = parseSpdx(license);
    } catch (error) {
      problems.push(
        name +
          ": unsupported license expression " +
          JSON.stringify(license) +
          " (" +
          error.message +
          ")",
      );
      continue;
    }
    if (expressionIsAllowed(ast, allowed)) continue;

    const matchingExceptions = [];
    for (let index = 0; index < policy.buildTimeExceptions.length; index += 1) {
      const exception = policy.buildTimeExceptions[index];
      if (
        exception &&
        typeof exception.namePattern === "string" &&
        globMatches(exception.namePattern, name)
      ) {
        matchingExceptions.push({ exception, index });
      }
    }

    if (!isDev) {
      if (matchingExceptions.length > 0) {
        problems.push(
          name + ": production package cannot use a build-time license exception for " + license,
        );
      } else {
        problems.push(name + ": production license is not allowed: " + license);
      }
      continue;
    }

    const approvedException = matchingExceptions.find(
      ({ exception }) => Array.isArray(exception.licenses) && exception.licenses.includes(license),
    );
    if (!approvedException) {
      if (matchingExceptions.length > 0) {
        const patterns = matchingExceptions.map(({ exception }) => exception.namePattern).join(", ");
        problems.push(
          name +
            ": license " +
            JSON.stringify(license) +
            " is not listed for matching build-time exception " +
            patterns,
        );
      } else {
        problems.push(name + ": dev license is not allowed and has no build-time exception: " + license);
      }
      continue;
    }

    exceptionCounts[approvedException.index] += 1;
    exceptionsApplied += 1;
  }

  const exceptionResults = policy.buildTimeExceptions.map((exception, index) => ({
    namePattern: exception.namePattern,
    reason: exception.reason,
    count: exceptionCounts[index],
  }));
  for (const exception of exceptionResults) {
    if (exception.count === 0) {
      warnings.push('build-time exception "' + exception.namePattern + '" matched no non-allowlisted packages');
    }
  }

  return {
    checked: entries.length,
    production,
    dev,
    exceptionsApplied,
    exceptionCounts: exceptionResults,
    problems,
    warnings,
  };
}

function run() {
  try {
    const lockfile = JSON.parse(readFileSync(resolve(process.cwd(), "package-lock.json"), "utf8"));
    const policy = JSON.parse(
      readFileSync(resolve(process.cwd(), "scripts/license-policy.json"), "utf8"),
    );
    const result = checkLicenses(lockfile, policy);

    process.stdout.write(
      "Checked " +
        result.checked +
        " packages (" +
        result.production +
        " production, " +
        result.dev +
        " dev); exceptions applied: " +
        result.exceptionsApplied +
        "\n",
    );
    for (const exception of result.exceptionCounts) {
      process.stdout.write(
        'Exception "' + exception.namePattern + '": ' + exception.count + " packages\n",
      );
    }
    for (const warning of result.warnings) {
      process.stdout.write("Warning: " + warning + "\n");
    }
    if (result.problems.length > 0) {
      for (const problem of result.problems) {
        process.stderr.write("License policy failure: " + problem + "\n");
      }
      process.exitCode = 1;
    }
  } catch (error) {
    process.stderr.write("License check failed: " + error.message + "\n");
    process.exitCode = 1;
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  run();
}
