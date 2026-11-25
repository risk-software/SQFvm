[   ["assertEqual",     ["waitUntil with a timeout returns the condition value", { waitUntil [{ true }, 1] } ], true],
    ["assertIsNil",     ["waitUntil with a timeout returns nil when it times out", { waitUntil [{ false }, 0.05, 0.01] } ] ],
    ["assertEqual",     ["the result of a timed-out waitUntil can be assigned", { private _result = waitUntil [{ false }, 0.02]; isNil "_result" } ], true],
    ["assertEqual",     ["waitUntil with a timeout stops waiting", { private _start = diag_tickTime; waitUntil [{ false }, 0.05]; (diag_tickTime - _start) < 0.5 } ], true],
    ["assertEqual",     ["waitUntil on a script handle waits until it is done", { private _handle = [] spawn { sleep 0.02 }; waitUntil _handle; scriptDone _handle } ], true],
    ["assertEqual",     ["waitUntil on a script handle with a timeout", { private _handle = [] spawn { sleep 5 }; waitUntil [_handle, 0.05]; private _finished = scriptDone _handle; terminate _handle; _finished } ], false]
]
