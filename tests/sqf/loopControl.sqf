[   ["assertEqual",     ["break leaves a forEach loop", { private _seen = []; { if (_x == 3) then { break }; _seen pushBack _x } forEach [1, 2, 3, 4]; _seen } ], [1, 2]],
    ["assertEqual",     ["break leaves a for loop", { private _last = -1; for "_i" from 0 to 10 do { if (_i == 4) then { break }; _last = _i }; _last } ], 3],
    ["assertEqual",     ["break leaves a while loop", { private _i = 0; while { true } do { _i = _i + 1; if (_i == 5) then { break } }; _i } ], 5],
    ["assertEqual",     ["breakWith returns its value from the loop", { { if (_x > 2) then { breakWith (_x * 10) } } forEach [1, 2, 3, 4] } ], 30],
    ["assertEqual",     ["breakWith only leaves the innermost loop", { private _rows = 0; { { breakWith 0 } forEach [1, 2]; _rows = _rows + 1 } forEach [1, 2, 3]; _rows } ], 3],
    ["assertEqual",     ["continue skips the rest of the iteration", { private _seen = []; { if (_x % 2 == 0) then { continue }; _seen pushBack _x } forEach [1, 2, 3, 4, 5]; _seen } ], [1, 3, 5]],
    ["assertEqual",     ["continue in a for loop", { private _sum = 0; for "_i" from 1 to 5 do { if (_i == 3) then { continue }; _sum = _sum + _i }; _sum } ], 12],
    ["assertEqual",     ["continueWith sets the result of an apply iteration", { [1, 2, 3] apply { if (_x == 2) then { continueWith 0 }; _x } } ], [1, 0, 3]],
    ["assertEqual",     ["code after the loop still runs", { private _after = false; { break } forEach [1]; _after = true; _after } ], true],
    ["assertEqual",     ["forEachReversed visits from the end", { private _seen = []; { _seen pushBack _x } forEachReversed [1, 2, 3]; _seen } ], [3, 2, 1]],
    ["assertEqual",     ["forEachReversed gives the index", { private _indexes = []; { _indexes pushBack _forEachIndex } forEachReversed ["a", "b", "c"]; _indexes } ], [2, 1, 0]],
    ["assertEqual",     ["forEachReversed allows deleting the current element", { private _arr = [1, 2, 3, 4]; { if (_x % 2 == 0) then { _arr deleteAt _forEachIndex } } forEachReversed _arr; _arr } ], [1, 3]],
    ["assertEqual",     ["forEachReversed on an empty array does nothing", { private _count = 0; { _count = _count + 1 } forEachReversed []; _count } ], 0]
]
