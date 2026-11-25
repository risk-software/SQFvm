#!/usr/bin/env python3
"""Generates src/operators/ops_simulated.cpp from the acemod/arma3-wiki command data.

Every syntax of every script command in the data gets a handler that accepts the documented argument
types and returns a default value of the documented return type. At run time a handler is only
registered when no real implementation exists for the same syntax, so real implementations always win.

Only command names, argument types and return types are taken from the data; no wiki text is copied.

Usage:
    generate_simulated_ops.py <commands-dir> <output.cpp> [--prune-placeholders <src/operators>]

<commands-dir> is the `commands` folder of the dataset's `dist` branch (one YAML file per command).
--prune-placeholders removes the "NOT IMPLEMENTED" entries in ops_dummy_*.cpp for commands the data
covers, since the generated handlers replace them.
"""

import glob
import os
import re
import sys

import yaml

IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Wiki parameter types to SQF-VM types. Anything not listed accepts any type.
ARGUMENT_TYPES = {
    "Number": "t_scalar",
    "String": "t_string",
    "Boolean": "t_boolean",
    "Code": "t_code",
    "Object": "t_object",
    "Group": "t_group",
    "Side": "t_side",
    "Config": "t_config",
    "Control": "t_control",
    "Display": "t_display",
    "Location": "t_location",
    "Task": "t_task",
    "TeamMember": "t_teammember",
    "DiaryRecord": "t_diaryrecord",
    "Namespace": "t_namespace",
    "ScriptHandle": "t_script",
    "StructuredText": "t_text",
    "ForType": "t_for",
    "IfType": "t_if",
    "SwitchType": "t_switch",
    "WhileType": "t_while",
    "WithType": "t_with",
}

# Wiki return types to simulated_return values.
RETURN_TYPES = {
    "Nothing": "nothing",
    "Boolean": "boolean",
    "Number": "number",
    "String": "string",
    "Object": "object",
    "Group": "group",
    "Side": "side",
    "Config": "config",
    "StructuredText": "text",
    "Display": "display",
    "Control": "control",
    "Task": "task",
    "Location": "location",
    "TeamMember": "teammember",
    "DiaryRecord": "diaryrecord",
    "ScriptHandle": "script",
    "Namespace": "namespace_",
    "Code": "code",
    "ArrayColor": "color",
    "Vector3d": "position",
}


class CommandLoader(yaml.SafeLoader):
    pass


def construct_call(kind):
    def construct(loader, node):
        if isinstance(node, yaml.SequenceNode):
            return {"kind": kind, "args": loader.construct_sequence(node, deep=True)}
        return {"kind": kind, "args": [loader.construct_scalar(node)]}
    return construct


for call_kind in ("Unary", "Binary", "Nular"):
    CommandLoader.add_constructor("!" + call_kind, construct_call(call_kind))


def argument_type(argument, parameter_types):
    if isinstance(argument, list):
        return "t_array"
    wiki_type = parameter_types.get(argument)
    if wiki_type is None:
        return "t_any"
    if wiki_type.startswith("Array") or wiki_type.startswith("Position") or wiki_type in ("Vector3d", "Waypoint"):
        return "t_array"
    if wiki_type.startswith("HashMap"):
        return "t_hashmap"
    return ARGUMENT_TYPES.get(wiki_type, "t_any")


def return_type(wiki_type):
    if wiki_type is None:
        return "nothing"
    if wiki_type.startswith("Position"):
        return "position"
    if wiki_type.startswith("Array") or wiki_type == "Waypoint":
        return "array"
    if wiki_type.startswith("HashMap"):
        return "hashmap"
    # Unknown and Anything have no meaningful default, so they return nil.
    return RETURN_TYPES.get(wiki_type, "nothing")


def load_syntaxes(commands_dir):
    """Returns sorted, de-duplicated (kind, name, left, right, return) tuples."""
    syntaxes = {}
    for path in sorted(glob.glob(os.path.join(commands_dir, "*.yml"))):
        with open(path, encoding="utf-8") as handle:
            try:
                command = yaml.load(handle, Loader=CommandLoader)
            except yaml.YAMLError as error:
                print(f"skipping {path}: {error}", file=sys.stderr)
                continue
        name = command.get("name", "")
        if not IDENTIFIER.match(name):
            continue
        for syntax in command.get("syntax") or []:
            call = syntax.get("call")
            params = {p.get("name"): p.get("type") for p in syntax.get("params") or []}
            ret = return_type((syntax.get("ret") or [None])[0])
            if not isinstance(call, dict):
                key = ("nular", name.lower(), None, None)
                left = right = None
            elif call["kind"] == "Unary":
                args = call["args"]
                right = "t_array" if len(args) > 1 else argument_type(args[0], params)
                left = None
                key = ("unary", name.lower(), None, right)
            elif call["kind"] == "Binary":
                left = argument_type(call["args"][0], params)
                right = argument_type(call["args"][1], params)
                key = ("binary", name.lower(), left, right)
            else:
                continue
            syntaxes.setdefault(key, (key[0], name, left, right, ret))
    return [syntaxes[key] for key in sorted(syntaxes)]


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render(syntaxes, data_version):
    lines = [
        "// Generated by tools/generate_simulated_ops.py from the acemod/arma3-wiki command data",
        f"// (Arma 3 {data_version}). Do not edit by hand; run the generator instead.",
        "#include \"ops_simulated.h\"",
        "#include \"d_handle.h\"",
        "#include \"../runtime/runtime.h\"",
        "#include \"../runtime/sqfop.h\"",
        "#include \"../runtime/type.h\"",
        "#include \"ops_hashmap.h\"",
        "#include \"ops_generic.h\"",
        "#include \"ops_namespace.h\"",
        "#include \"d_object.h\"",
        "#include \"d_group.h\"",
        "#include \"d_side.h\"",
        "#include \"d_config.h\"",
        "#include \"d_text.h\"",
        "#include \"../runtime/d_array.h\"",
        "#include \"../runtime/d_boolean.h\"",
        "#include \"../runtime/d_code.h\"",
        "#include \"../runtime/d_scalar.h\"",
        "#include \"../runtime/d_string.h\"",
        "",
        "using namespace sqf::runtime;",
        "using namespace sqf::types;",
        "using sqf::operators::simulated_result;",
        "using sqf::operators::simulated_return;",
        "",
        "void sqf::operators::ops_simulated(::sqf::runtime::runtime& runtime)",
        "{",
        "    using namespace sqf::runtime::sqfop;",
        "    const std::string description = \"Simulated: needs the game world, returns a default value.\";",
    ]
    for kind, name, left, right, ret in syntaxes:
        key_name = cpp_string(name.lower())
        shown = cpp_string(name)
        result = f"return simulated_result(runtime, {shown}, simulated_return::{ret});"
        # A real operator covers the syntax when it matches it exactly or takes any type on a side.
        if kind == "nular":
            lines.append(f"    if (!runtime.sqfop_exists(sqfop_nular::key{{ {key_name} }}))")
            lines.append(f"        runtime.register_sqfop(nular({shown}, description, [](::sqf::runtime::runtime& runtime) -> value {{ {result} }}));")
        elif kind == "unary":
            lines.append(f"    if (!runtime.sqfop_exists(sqfop_unary::key{{ {key_name}, {right}() }}) && !runtime.sqfop_exists(sqfop_unary::key{{ {key_name}, t_any() }}))")
            lines.append(f"        runtime.register_sqfop(unary({shown}, {right}(), description, [](::sqf::runtime::runtime& runtime, value::cref) -> value {{ {result} }}));")
        else:
            lines.append(f"    if (!runtime.sqfop_exists(sqfop_binary::key{{ {key_name}, {left}(), {right}() }})"
                         f" && !runtime.sqfop_exists(sqfop_binary::key{{ {key_name}, t_any(), {right}() }})"
                         f" && !runtime.sqfop_exists(sqfop_binary::key{{ {key_name}, {left}(), t_any() }})"
                         f" && !runtime.sqfop_exists(sqfop_binary::key{{ {key_name}, t_any(), t_any() }}))")
            lines.append(f"        runtime.register_sqfop(binary(4, {shown}, {left}(), {right}(), description, [](::sqf::runtime::runtime& runtime, value::cref, value::cref) -> value {{ {result} }}));")
    lines.append("}")
    return "\n".join(lines) + "\n"


def prune_placeholders(operators_dir, names):
    pattern = re.compile(r'register_sqfop\((?:nular|unary|binary)\((?:\d+,\s*)?"([^"]+)"')
    removed = 0
    for path in sorted(glob.glob(os.path.join(operators_dir, "ops_dummy_*.cpp"))):
        with open(path, encoding="utf-8", newline="") as handle:
            lines = handle.read().splitlines(keepends=True)
        kept = []
        for line in lines:
            match = pattern.search(line)
            if match and match.group(1).lower() in names:
                removed += 1
                continue
            kept.append(line)
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write("".join(kept))
    return removed


def main():
    if len(sys.argv) not in (3, 5):
        print(__doc__, file=sys.stderr)
        return 2
    commands_dir, output = sys.argv[1], sys.argv[2]
    version_file = os.path.join(os.path.dirname(os.path.abspath(commands_dir)), "version.txt")
    data_version = "unknown version"
    if os.path.exists(version_file):
        with open(version_file, encoding="utf-8") as handle:
            data_version = handle.read().strip()
    syntaxes = load_syntaxes(commands_dir)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(render(syntaxes, data_version))
    print(f"{len(syntaxes)} syntaxes written to {output}")
    if len(sys.argv) == 5 and sys.argv[3] == "--prune-placeholders":
        names = {name.lower() for _, name, _, _, _ in syntaxes}
        print(f"{prune_placeholders(sys.argv[4], names)} placeholders removed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
