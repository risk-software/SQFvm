[   ["assertEqual",     ["waitUntil on a script returns its result", { private _handle = [] spawn { sleep 0.01; 40 + 2 }; waitUntil _handle } ], 42],
    ["assertEqual",     ["waitUntil with a timeout returns the script result", { private _handle = [] spawn { 7 }; waitUntil [_handle, 1] } ], 7],
    ["assertEqual",     ["a promise is not done until it gets a result", { private _promise = spawn "MyPromise"; scriptDone _promise } ], false],
    ["assertEqual",     ["terminate with a result completes a promise", { private _promise = spawn "MyPromise"; _promise terminate 5; [scriptDone _promise, waitUntil _promise] } ], [true, 5]],
    ["assertEqual",     ["a promise has its name", { str (spawn "MyPromise") } ], "MyPromise"],
    ["assertEqual",     ["continueWith runs when the promise completes", { sqfvm_test_continued = nil; private _promise = spawn "P"; _promise continueWith { sqfvm_test_continued = _this }; _promise terminate 3; sleep 0.05; sqfvm_test_continued } ], 3],
    ["assertEqual",     ["continueWith passes arguments in _thisArgs", { sqfvm_test_args = nil; private _promise = spawn "P"; _promise continueWith [{ sqfvm_test_args = _thisArgs }, "extra"]; _promise terminate 1; sleep 0.05; sqfvm_test_args } ], "extra"],
    ["assertEqual",     ["continueWith on a finished script runs at once", { sqfvm_test_late = nil; private _handle = [] spawn { 9 }; waitUntil _handle; _handle continueWith { sqfvm_test_late = _this }; sleep 0.05; sqfvm_test_late } ], 9],
    ["assertEqual",     ["terminate stops a running script", { sqfvm_test_reached = false; private _handle = [] spawn { sleep 0.05; sqfvm_test_reached = true }; terminate _handle; sleep 0.1; [scriptDone _handle, sqfvm_test_reached] } ], [true, false]]
]
