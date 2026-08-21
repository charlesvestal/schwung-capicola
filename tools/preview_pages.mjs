#!/usr/bin/env node
// tools/preview_pages.mjs
//
// Proves capicola's declared UI contract (chain_params + ui_hierarchy) lays
// out into clean knob pages, with no device and no ARM build. Pulls both
// contracts straight out of the COMPILED plugin (via a tiny dumper built ad
// hoc into build/dump_contracts) so this can never drift from
// capicola_plugin.cpp, then runs them through Schwung's real page planner and
// contract validator.
//
// Usage: node tools/preview_pages.mjs
// Env:   SCHWUNG_ROOT — path to a schwung checkout (default: ../schwung)
//
// Exit 0 only if: exactly 4 knob pages AND 0 validator errors AND the
// hierarchy/chain_params key sets match in both directions. Exit 1 otherwise,
// having printed why.

import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const REPO_ROOT = path.dirname(__dirname);

const SCHWUNG_ROOT = path.resolve(
    process.env.SCHWUNG_ROOT || path.join(REPO_ROOT, "..", "schwung")
);

function fail(msg) {
    console.error(`FAIL: ${msg}`);
    process.exit(1);
}

if (!existsSync(SCHWUNG_ROOT)) {
    fail(`SCHWUNG_ROOT does not exist: ${SCHWUNG_ROOT} (set SCHWUNG_ROOT or check out ../schwung)`);
}

/* ---------------------------------------------------------- build the dumper */

const buildLib = path.join(REPO_ROOT, "build", "lib");
if (!existsSync(buildLib)) {
    fail(`${buildLib} is missing — run scripts/apply_patches.sh first`);
}

const dumperSrc = path.join(REPO_ROOT, "tools", "dump_contracts.cpp");
const dumperBin = path.join(REPO_ROOT, "build", "dump_contracts");
mkdirSync(path.join(REPO_ROOT, "build"), { recursive: true });

console.log("Building build/dump_contracts...");
try {
    execFileSync("c++", [
        "-std=c++17", "-O1",
        "-I", buildLib,
        "-I", path.join(REPO_ROOT, "src", "dsp"),
        dumperSrc,
        path.join(REPO_ROOT, "src", "dsp", "capicola_plugin.cpp"),
        path.join(REPO_ROOT, "src", "dsp", "capicola_engine.cpp"),
        path.join(REPO_ROOT, "src", "dsp", "capicola_params.cpp"),
        "-o", dumperBin,
    ], { stdio: "inherit" });
} catch (e) {
    fail(`dumper failed to build: ${e.message}`);
}

/* ------------------------------------------------------- run it, get JSON */

function dump(key) {
    let out;
    try {
        out = execFileSync(dumperBin, [key], { encoding: "utf8" });
    } catch (e) {
        fail(`dump_contracts ${key} failed: ${e.message}`);
    }
    let parsed;
    try {
        parsed = JSON.parse(out);
    } catch (e) {
        fail(`dump_contracts ${key} did not print valid JSON: ${e.message}\n---\n${out}\n---`);
    }
    return parsed;
}

const chainParams = dump("chain_params");
const hierarchy = dump("ui_hierarchy");

if (!Array.isArray(chainParams)) fail("chain_params did not parse to an array");
if (!hierarchy || typeof hierarchy !== "object") fail("ui_hierarchy did not parse to an object");

/* --------------------------------------------------------- run the planner */

const { planPages, PAGE_KNOBS } = await import(
    path.join(SCHWUNG_ROOT, "src", "shared", "param_pages", "page_plan.mjs")
);
const { validateContract } = await import(
    path.join(SCHWUNG_ROOT, "src", "shared", "param_pages", "validate_contract.mjs")
);

const plan = planPages({ hierarchy, chainParams, mode: null, visible: undefined });

console.log("\n--- Page plan ---");
for (const p of plan.pages) {
    console.log(`${p.kind.padEnd(6)} "${p.name}" ${p.keys ? p.keys.length : ""}`);
}
if (plan.warnings && plan.warnings.length) {
    console.log("\n--- Planner warnings ---");
    for (const w of plan.warnings) console.log(`  ${w}`);
}

const knobPages = plan.pages.filter((p) => p.kind === PAGE_KNOBS);

/* ------------------------------------------------------------- validation */

const result = validateContract({ id: "capicola", hierarchy, chainParams });
const findings = (result && result.findings) || [];
const errors = findings.filter((f) => f.rule !== "info" && f.level !== "info" &&
    !(f.rule && /^info/.test(f.rule)));

console.log("\n--- Validator findings ---");
if (findings.length === 0) {
    console.log("  none");
} else {
    for (const f of findings) {
        console.log(`  [${f.level || f.severity || "?"}] ${f.rule || ""} ${f.message || JSON.stringify(f)}`);
    }
}

/* Count real errors by severity field, whatever the validator calls it, since
 * different callers of validate_contract.mjs read either `level` or
 * `severity` — be defensive rather than silently treating errors as info. */
const errorFindings = findings.filter((f) => (f.level || f.severity) === "error");

/* -------------------------------------------------- key-set cross-check */

function collectHierarchyKeys(h) {
    const keys = new Set();
    const levels = (h && h.levels) || {};
    for (const lvl of Object.values(levels)) {
        if (!lvl || typeof lvl !== "object") continue;
        for (const k of (lvl.knobs || [])) {
            const kk = typeof k === "string" ? k : (k && k.key);
            if (kk) keys.add(kk);
        }
        for (const p of (lvl.params || [])) {
            if (typeof p === "string") { keys.add(p); continue; }
            if (p && typeof p === "object" && p.key && !p.level) keys.add(p.key);
        }
    }
    return keys;
}

const hierarchyKeys = collectHierarchyKeys(hierarchy);
const chainParamKeys = new Set(chainParams.map((p) => p && p.key).filter(Boolean));

const missingFromChainParams = [...hierarchyKeys].filter((k) => !chainParamKeys.has(k));
const missingFromHierarchy = [...chainParamKeys].filter((k) => !hierarchyKeys.has(k));

console.log("\n--- Key-set cross-check ---");
console.log(`  hierarchy keys: ${hierarchyKeys.size}, chain_params keys: ${chainParamKeys.size}`);
if (missingFromChainParams.length) {
    console.log(`  in hierarchy but NOT in chain_params: ${missingFromChainParams.join(", ")}`);
}
if (missingFromHierarchy.length) {
    console.log(`  in chain_params but not referenced by hierarchy: ${missingFromHierarchy.join(", ")}`);
}
if (!missingFromChainParams.length && !missingFromHierarchy.length) {
    console.log("  match");
}

/* -------------------------------------------------------------- verdict */

const problems = [];
if (knobPages.length !== 4) {
    problems.push(`expected 4 knob pages, got ${knobPages.length}`);
}
for (const p of knobPages) {
    if (p.keys.length !== 6) {
        problems.push(`page "${p.name}" has ${p.keys.length} keys, expected 6`);
    }
}
const expectedNames = ["Main", "Secondary", "Mod Depth", "Mod Source"];
const actualNames = knobPages.map((p) => p.name);
if (JSON.stringify(actualNames) !== JSON.stringify(expectedNames)) {
    problems.push(`page names ${JSON.stringify(actualNames)} != expected ${JSON.stringify(expectedNames)}`);
}
if (errorFindings.length > 0) {
    problems.push(`${errorFindings.length} validator error(s)`);
}
if (missingFromChainParams.length > 0) {
    problems.push(`${missingFromChainParams.length} key(s) in hierarchy with no chain_params entry`);
}
if (missingFromHierarchy.length > 0) {
    problems.push(`${missingFromHierarchy.length} key(s) in chain_params not referenced by hierarchy`);
}

console.log("\n--- Verdict ---");
if (problems.length === 0) {
    console.log(`OK: ${knobPages.length} knob pages (${knobPages.map((p) => p.name).join(" / ")}), 0 validator errors, key sets match.`);
    process.exit(0);
} else {
    for (const p of problems) console.log(`  PROBLEM: ${p}`);
    process.exit(1);
}
