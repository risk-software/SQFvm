[   ["assertIsNil",     ["privateAll hides parent locals", { private _outer = 1; call { privateAll; _outer } } ] ],
    ["assertEqual",     ["privateAll keeps parent locals unchanged on assignment", { private _outer = 1; call { privateAll; _outer = 2 }; _outer } ], 1],
    ["assertEqual",     ["import copies a parent local after privateAll", { private _outer = 5; call { privateAll; import "_outer"; _outer } } ], 5],
    ["assertEqual",     ["import with an array", { private _a = 1; private _b = 2; call { privateAll; import ["_a", "_b"]; _a + _b } } ], 3],
    ["assertEqual",     ["without privateAll parent locals are visible", { private _outer = 1; call { _outer } } ], 1],
    ["assertEqual",     ["isNil with a namespace for a missing variable", { missionNamespace isNil "sqfvm_test_missing" } ], true],
    ["assertEqual",     ["isNil with a namespace for a set variable", { missionNamespace setVariable ["sqfvm_test_set", 1]; missionNamespace isNil "sqfvm_test_set" } ], false],
    ["assertEqual",     ["localNamespace keeps variables", { localNamespace setVariable ["sqfvm_test_local", 3]; localNamespace getVariable ["sqfvm_test_local", 0] } ], 3],
    ["assertEqual",     ["localNamespace is separate from missionNamespace", { localNamespace setVariable ["sqfvm_test_only_local", 3]; missionNamespace getVariable ["sqfvm_test_only_local", 0] } ], 0],
    ["assertEqual",     ["missionProfileNamespace is available", { isMissionProfileNamespaceLoaded } ], true],
    ["assertEqual",     ["unary spawn returns a script handle", { typeName (spawn {}) } ], "SCRIPT"],
    ["assertEqual",     ["isNil code runs unscheduled", { private _inside = true; isNil { _inside = canSuspend }; [_inside, canSuspend] } ], [false, true]]
]
