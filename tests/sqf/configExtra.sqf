[   ["assertEqual",     ["configHierarchy returns configs from the root", { private _path = configHierarchy (configFile >> "flat_tests" >> "C"); [count _path, configName (_path select 2)] } ], [3, "C"]],
    ["assertEqual",     ["inheritsFrom returns the base class", { configName inheritsFrom (configFile >> "flat_tests" >> "D") } ], "C"],
    ["assertEqual",     ["inheritsFrom of a class without base is configNull", { isNull inheritsFrom (configFile >> "flat_tests" >> "A") } ], true],
    ["assertEqual",     ["configHierarchy with bases follows inheritance", { configHierarchy [configFile >> "flat_tests" >> "D", true, true, false, true] } ], ["A", "C", "D"]],
    ["assertEqual",     ["configHierarchy with bases reversed", { configHierarchy [configFile >> "flat_tests" >> "D", true, true, true, true] } ], ["D", "C", "A"]],
    ["assertEqual",     ["configClasses array form returns direct subclass names", { private _names = configClasses [configFile >> "flat_tests", 0, false, true, true]; _names sort true; _names } ], ["A", "B", "C", "D", "E"]],
    ["assertEqual",     ["configClasses array form skips properties", { configClasses [configFile >> "test_config_classes_only_returns_config_entries", 0, false, true, true] } ], ["TestSub"]],
    ["assertEqual",     ["configClasses with depth collects deeper classes", { count configClasses [configFile >> "nested_tests", 3, false, true, true] } ], 4],
    ["assertEqual",     ["configClasses with depth and only the last tier", { configClasses [configFile >> "nested_tests", 3, false, false, true] } ], ["nested4"]],
    ["assertEqual",     ["getTextRaw returns the text as written", { getTextRaw (configFile >> "type_tests" >> "type_string") } ], "test"],
    ["assertEqual",     ["getMissionConfigValue returns the default without a mission config", { getMissionConfigValue ["respawnDelay", 10] } ], 10],
    ["assertIsNil",     ["getMissionConfigValue without a default returns nil", { getMissionConfigValue "respawnDelay" } ] ]
]
