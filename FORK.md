# About this fork

This is a modified version of [SQF-VM](https://github.com/SQFvm/runtime) maintained by RISK Software. It
is distributed under the same licence, the GNU Lesser General Public License v3 (see `LICENSE`).
Upstream SQF-VM is by the SQFvm contributors; this fork adds support for commands and syntaxes from
Arma 3 2.x so that current scripts can be tested.

## Changes from upstream

November 2026, based on upstream commit `ed9f5f5`:

- **Loop control:** `break`, `breakWith`, `continue` and `continueWith` work in `for`, `forEach`,
  `forEachReversed`, `while`, `count`, `select`, `apply` and `findIf`.
- **New loops:** `forEachReversed`, plus `forEach` and `apply` over HashMaps (`_x` key, `_y` value).
- **HashMaps:**
  - new commands: `getOrDefault` (with `setDefault`), `getOrDefaultCall`, `merge` (both forms),
    `values`, `toArray` (both forms) and `insert` (both forms);
  - `keys createHashMapFromArray values`;
  - `set` takes `insertOnly` and returns whether a value was overwritten.
- **Arrays:**
  - `flatten`, `findAny`, `selectRandomWeighted` (both forms), `isEqualRef` and `isNotEqualRef`;
  - `deleteAt [indexes]`;
  - `resize [count, fill]`;
  - `insert` for arrays and strings;
  - negative indexes for `set`.
- **Strings:** `trim` (both forms), `toLowerANSI`, `toUpperANSI`, `forceUnicode` (accepted, no
  effect), `toString code`, `reverse string`, and `find [substring, startIndex]`.
- **Data exchange:** `toJSON`, `fromJSON` and `parseSimpleArray`.
- **Scopes and namespaces:**
  - new commands: `privateAll`, `import`, `localNamespace`, `missionProfileNamespace`,
    `serverNamespace`, `isMissionProfileNamespaceLoaded`, `saveMissionProfileNamespace` and
    `saveProfileNamespace`;
  - `namespace isNil name`.
- **Scheduling:**
  - unary `spawn code`;
  - `waitUntil [condition, timeout, interval]`, `waitUntil scriptHandle` and
    `waitUntil [scriptHandle, timeout]`;
  - code passed to `isNil` now runs unscheduled, as in the engine.
- **Fixes:**
  - Scripts given with `--sqf` and `--input-sqf` now run scheduled, as the help text says, so
    `sleep` and `waitUntil` work in them.
  - Assigning to a local variable respects the same scope boundary as reading it.
- **Placeholders:** the "NOT IMPLEMENTED" placeholders for the commands above were removed.

Later the same month:

- **Every command exists:** `tools/generate_simulated_ops.py` writes `src/operators/ops_simulated.cpp`
  from the acemod/arma3-wiki data. It covers 3,076 syntaxes with their documented argument types and a
  default return value of the documented type.
  - These handlers apply only where no real implementation exists.
  - The first call to each one logs `[SIMULATED]`.
  - Undocumented argument types fail as in the engine.
  - Locations, displays, controls, tasks, team members, diary records and net objects are handles with
    null values and `isNull`.
- **Regular expressions:** `regexMatch`, `regexFind` and `regexReplace` use Boost.Regex (Boost Software
  License, fetched at build time), the engine the game uses. Patterns use `expression/flags`.
- **HashMap objects:** `createHashMapObject` with `#base`, `#create`, `#clone`, `#type` and the
  `sealed` and `noCopy` flags. `hashMapObject call [method, args]` works, and `+` runs `#clone`.
  `compileFinal` takes strings, code and HashMaps, final values cannot be overwritten, and `isFinal`
  reports the flag.
- **Math:** `linearConversion`, `vectorLinearConversion`, `vectorFromTo`, `bezierInterpolation`,
  `distanceSqr`, the 2.18 number forms of `distance` and `distanceSqr`, the `random` forms, `finite`,
  `tg`, `systemTime`, `systemTimeUTC`, `hashValue` and `formatText`. The vector commands take any length
  (2.14) or 2D vectors, as documented.
- **Scripts:**
  - `waitUntil` returns a script's result.
  - `spawn "name"` promises are completed by `terminate [handle, result]`.
  - `continueWith` callbacks run when a script or promise completes.
- **Config:**
  - `missionConfigFile` and `campaignConfigFile` read classes `MissionConfigFile` and
    `CampaignConfigFile`.
  - New: `getMissionConfigValue`, `getMissionConfig`, `getTextRaw`, and the 2.22 array forms of
    `configClasses` and `configHierarchy`.
- **System:**
  - New: `breakTo`, the C-style `for`, `fileExists`, and `localize`/`isLocalized` from every
    `stringtable.xml`.
  - Single-machine `publicVariable*`, and `isUIContext`.
  - `diag_codePerformance`, `diag_activeSQFScripts`, `diag_activeScripts` and `diag_stacktrace`.
  - Frame diagnostics at a nominal 60 frames per second.
- **Nil arguments:** a command given nil evaluates to nil instead of aborting the expression. The nil
  forms of `isEqualTo`, `isNotEqualTo`, `isEqualType`, `str`, `pushBack`, `call` and `spawn` are
  supported.
- **Fixes:**
  - `terminate` now stops scripts.
  - `configHierarchy` returns config entries.
  - `inheritsFrom` returns the base class.
  - `str objNull` and `str grpNull` match the engine.
- **CBA_A3 test:** it no longer patches out commands that now exist. 349 of 355 tests pass.

Known differences from the engine:

- Commands that need the game world (objects, units, AI, UI, sound, terrain) are simulated: they
  accept their documented arguments and return defaults, but do not keep world state yet.
- Seeded `random` and `hashValue` are deterministic but differ from the engine's undocumented values.
- `#delete` and `#str` of HashMap objects are not run.
- String commands work on bytes, not Unicode characters.
- HashMap iteration order differs from the engine's.
