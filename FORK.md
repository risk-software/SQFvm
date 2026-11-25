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

Known differences from the engine:

- `waitUntil scriptHandle` returns nil, because SQF-VM does not keep script results.
- String commands work on bytes, not Unicode characters.
- HashMap iteration order differs from the engine's.
