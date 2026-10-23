// Edited by: OpenAI-Codex_Responsible: Website
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const registryHost = "registry.npmjs.org";

export function checkLockfile(lockfile) {
  const problems = [];
  const packages =
    lockfile && typeof lockfile.packages === "object" && lockfile.packages !== null
      ? lockfile.packages
      : null;

  if (!lockfile || lockfile.lockfileVersion !== 3) {
    problems.push("package-lock.json must use lockfileVersion 3");
  }

  if (!packages) {
    problems.push("package-lock.json must contain a packages object");
    return { checked: 0, problems };
  }

  const entries = Object.entries(packages).filter(([key]) => key !== "");

  for (const [key, entry] of entries) {
    const name = key || "(root)";
    if (!entry || typeof entry !== "object") {
      problems.push(name + ": package entry must be an object");
      continue;
    }

    const specialEntry = entry.link === true || entry.inBundle === true;
    if (!specialEntry) {
      if (typeof entry.resolved !== "string" || entry.resolved.length === 0) {
        problems.push(name + ": missing resolved URL");
      }
      if (typeof entry.integrity !== "string" || entry.integrity.length === 0) {
        problems.push(name + ": missing integrity");
      }
    }

    if (Object.hasOwn(entry, "resolved")) {
      if (typeof entry.resolved !== "string" || entry.resolved.length === 0) {
        problems.push(name + ": resolved URL must be a non-empty HTTPS URL on " + registryHost);
        continue;
      }
      try {
        const url = new URL(entry.resolved);
        if (url.protocol !== "https:" || url.hostname.toLowerCase() !== registryHost) {
          problems.push(name + ": disallowed resolved source " + entry.resolved);
        }
      } catch {
        problems.push(name + ": invalid resolved URL " + entry.resolved);
      }
    }
  }

  return { checked: entries.length, problems };
}

function run() {
  try {
    const lockfilePath = resolve(process.cwd(), "package-lock.json");
    const lockfile = JSON.parse(readFileSync(lockfilePath, "utf8"));
    const result = checkLockfile(lockfile);

    if (result.problems.length > 0) {
      process.stderr.write(
        "Lockfile problems: " + result.checked + " packages checked, " + result.problems.length + " problems\n",
      );
      for (const problem of result.problems) {
        process.stderr.write("- " + problem + "\n");
      }
      process.exitCode = 1;
      return;
    }

    process.stdout.write(
      "Lockfile OK: " + result.checked + " packages checked, 0 problems\n",
    );
  } catch (error) {
    process.stderr.write("Lockfile check failed: " + error.message + "\n");
    process.exitCode = 1;
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  run();
}
