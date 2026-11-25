[   ["assertEqual",     ["breakTo continues in the named scope", { private _log = []; call { scopeName "outer"; call { _log pushBack 1; breakTo "outer"; _log pushBack 2 }; _log pushBack 3 }; _log } ], [1, 3]],
    ["assertEqual",     ["the C-style for loop", { private _sum = 0; for [{ _i = 0 }, { _i < 5 }, { _i = _i + 1 }] do { _sum = _sum + _i }; _sum } ], 10],
    ["assertEqual",     ["break leaves a C-style for loop", { private _last = 0; for [{ _i = 0 }, { _i < 10 }, { _i = _i + 1 }] do { if (_i == 3) then { break }; _last = _i }; _last } ], 2],
    ["assertEqual",     ["fileExists finds the test file", { fileExists "framework.sqf" } ], true],
    ["assertEqual",     ["fileExists of a missing file", { fileExists "sqfvm_missing_file.sqf" } ], false],
    ["assertEqual",     ["localize of a missing key is empty", { localize "STR_sqfvm_missing" } ], ""],
    ["assertEqual",     ["isLocalized of a missing key", { isLocalized "STR_sqfvm_missing" } ], false],
    ["assertEqual",     ["publicVariable is accepted", { sqfvm_test_public = 1; publicVariable "sqfvm_test_public"; sqfvm_test_public } ], 1],
    ["assertEqual",     ["isUIContext is false", { isUIContext } ], false],
    ["assertEqual",     ["diag_codePerformance runs the given cycles", { (diag_codePerformance [{ 1 + 1 }, nil, 50]) select 1 } ], 50],
    ["assertEqual",     ["diag_codePerformance runs unscheduled", { sqfvm_test_suspend = true; diag_codePerformance [{ sqfvm_test_suspend = canSuspend }, nil, 1]; sqfvm_test_suspend } ], false],
    ["assertEqual",     ["diag_stacktrace has an entry per scope", { (count diag_stacktrace) > 0 } ], true],
    ["assertEqual",     ["diag_fps is nominal", { diag_fps } ], 60]
]
