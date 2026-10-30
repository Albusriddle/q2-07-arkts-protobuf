#!/usr/bin/env python3
"""Q2-07 proto3 subset schema/codegen generator (deterministic, fail-closed).

This is intentionally NOT a general protobuf compiler.  It parses the small
proto3 subset used by proto/example.proto (message, repeated, optional, oneof,
map, field numbers) and emits C++ constants consumed by pod.h / decode_user.cpp.

    python scripts/gen_fields.py --write
    python scripts/gen_fields.py --check

Unknown constructs are hard errors (with line numbers); the generator must never
silently skip a field, because that would make the generated table disagree with
the checked-in schema.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO = ROOT / "proto" / "example.proto"
GENDIR = ROOT / "src" / "host" / "decoder" / "generated"
OUT_FIELDS = GENDIR / "q207_demo_fields.h"
OUT_POD = GENDIR / "q207_demo_pod.h"
OUT_SCHEMA_H = GENDIR / "q207_demo_schema.h"
OUT_SCHEMA_JSON = GENDIR / "q207_demo_schema.json"
OUT_DECODER = GENDIR / "q207_demo_decoder.inc"
OUT_DTS = ROOT / "app" / "entry" / "src" / "main" / "ets" / "types" / "libentry" / "index.d.ts"

TOKEN_RE = re.compile(
    r"""
    (?P<ws>\s+)
  | (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\])*")
  | (?P<number>-?\d+)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<punct>[{}\[\]()<>,;=.] )
    """,
    re.VERBOSE | re.DOTALL,
)

# Wire types from wire_reader.h; duplicated here to avoid a C++ dependency.
WIRE_VARINT = 0
WIRE_FIXED64 = 1
WIRE_LENGTH = 2
WIRE_FIXED32 = 5

SCALAR_WIRE = {
    "int32": WIRE_VARINT, "int64": WIRE_VARINT, "uint32": WIRE_VARINT,
    "uint64": WIRE_VARINT, "sint32": WIRE_VARINT, "sint64": WIRE_VARINT,
    "bool": WIRE_VARINT,
    "fixed64": WIRE_FIXED64, "sfixed64": WIRE_FIXED64, "double": WIRE_FIXED64,
    "string": WIRE_LENGTH, "bytes": WIRE_LENGTH,
    "fixed32": WIRE_FIXED32, "sfixed32": WIRE_FIXED32, "float": WIRE_FIXED32,
}


class ParseError(Exception):
    pass


@dataclass
class Token:
    kind: str
    value: str
    line: int


@dataclass
class Field:
    name: str
    number: int
    kind: str          # scalar/type name, or map
    label: str         # "", "repeated", "optional"
    wire: int
    map_key: str = ""
    map_value: str = ""
    oneof: bool = False
    oneof_name: str = ""


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    i = 0
    line = 1
    while i < len(text):
        m = TOKEN_RE.match(text, i)
        if not m:
            raise ParseError(f"line {line}: unrecognized character {text[i]!r}")
        kind = m.lastgroup or ""
        value = m.group()
        if kind in ("ws", "line_comment", "block_comment"):
            line += value.count("\n")
            i = m.end()
            continue
        if kind == "string":
            tokens.append(Token("string", value[1:-1], line))
        else:
            tokens.append(Token(kind, value, line))
        line += value.count("\n")
        i = m.end()
    tokens.append(Token("eof", "", line))
    return tokens


class Parser:
    def __init__(self, tokens: list[Token]):
        self.t = tokens
        self.i = 0
        self.messages: dict[str, list[Field]] = {}
        self.package = ""

    def peek(self) -> Token:
        return self.t[self.i]

    def next(self) -> Token:
        tok = self.t[self.i]
        self.i += 1
        return tok

    def expect(self, value: str) -> Token:
        tok = self.next()
        if tok.value != value:
            raise ParseError(f"line {tok.line}: expected {value!r}, got {tok.value!r}")
        return tok

    def ident(self) -> str:
        tok = self.next()
        if tok.kind != "ident":
            raise ParseError(f"line {tok.line}: expected identifier, got {tok.value!r}")
        return tok.value

    def number(self) -> int:
        tok = self.next()
        if tok.kind != "number":
            raise ParseError(f"line {tok.line}: expected field number, got {tok.value!r}")
        return int(tok.value)

    def skip_balanced(self, open_ch: str, close_ch: str) -> None:
        depth = 1
        while depth:
            tok = self.next()
            if tok.kind == "eof":
                raise ParseError("unexpected EOF in field options")
            if tok.value == open_ch:
                depth += 1
            elif tok.value == close_ch:
                depth -= 1

    def parse(self) -> dict[str, list[Field]]:
        if self.peek().value != "syntax":
            raise ParseError("missing syntax declaration")
        self.next()
        self.expect("=")
        syn = self.next()
        if syn.kind != "string" or syn.value != "proto3":
            raise ParseError(f"line {syn.line}: only proto3 is supported")
        self.expect(";")

        if self.peek().value == "package":
            self.next()
            parts = [self.ident()]
            while self.peek().value == ".":
                self.next()
                parts.append(self.ident())
            self.package = ".".join(parts)
            self.expect(";")

        while self.peek().kind != "eof":
            if self.peek().value == "message":
                self.parse_message()
            else:
                tok = self.peek()
                raise ParseError(f"line {tok.line}: unsupported top-level construct {tok.value!r}")
        if not self.messages:
            raise ParseError("no messages found")
        return self.messages

    def parse_message(self) -> None:
        self.expect("message")
        name = self.ident()
        if name in self.messages:
            raise ParseError(f"duplicate message {name}")
        self.expect("{")
        fields: list[Field] = []
        while self.peek().value != "}":
            tok = self.peek()
            if tok.kind == "eof":
                raise ParseError(f"unexpected EOF in message {name}")
            if tok.value == "oneof":
                self.parse_oneof(fields)
            elif tok.value in ("repeated", "optional"):
                label = self.next().value
                fields.append(self.parse_field(label))
            elif tok.value in ("reserved", "enum", "option", "extensions", "extend",
                               "service", "import", "required", "group"):
                raise ParseError(f"line {tok.line}: unsupported construct {tok.value!r}")
            elif tok.value == "message":
                raise ParseError(f"line {tok.line}: nested messages are not supported")
            else:
                fields.append(self.parse_field(""))
        self.expect("}")
        self.messages[name] = fields

    def parse_oneof(self, fields: list[Field]) -> None:
        self.expect("oneof")
        oneof_name = self.ident()
        self.expect("{")
        while self.peek().value != "}":
            tok = self.peek()
            if tok.kind == "eof":
                raise ParseError("unexpected EOF in oneof")
            if tok.value in ("optional", "repeated", "map", "oneof", "message"):
                raise ParseError(f"line {tok.line}: unsupported oneof member {tok.value!r}")
            fields.append(self.parse_field("", oneof=True, oneof_name=oneof_name))
        self.expect("}")

    def parse_field(self, label: str, oneof: bool = False, oneof_name: str = "") -> Field:
        typ = self.ident()
        map_key = map_value = ""
        if typ == "map":
            self.expect("<")
            map_key = self.ident()
            self.expect(",")
            map_value = self.ident()
            self.expect(">")
            wire = WIRE_LENGTH
        else:
            while self.peek().value == ".":
                self.next()
                typ += "." + self.ident()
            wire = SCALAR_WIRE.get(typ, WIRE_LENGTH)
        name = self.ident()
        self.expect("=")
        number = self.number()
        if number < 1 or number > 0x1FFFFFFF:
            raise ParseError(f"field {name}: number {number} outside [1, 2^29-1]")
        if 19000 <= number <= 19999:
            raise ParseError(f"field {name}: number {number} is reserved by protobuf")
        if self.peek().value == "[":
            self.next()
            self.skip_balanced("[", "]")
        self.expect(";")
        return Field(name, number, typ, label, wire, map_key, map_value, oneof, oneof_name)


def pascal(name: str) -> str:
    parts = [p for p in re.split(r"_+", name) if p]
    if not parts:
        raise ParseError("empty field name")
    return "".join(p[0].upper() + p[1:] for p in parts)


def generate(messages: dict[str, list[Field]], proto_text: str) -> str:
    # Validate uniqueness and build constants.
    constants: list[tuple[str, int]] = []
    wire_constants: list[tuple[str, int]] = []
    max_fields: list[tuple[str, int]] = []
    oneof_members: list[tuple[str, int]] = []

    for msg, fields in messages.items():
        seen_nums: dict[int, str] = {}
        seen_names: set[str] = set()
        max_field = 0
        for f in fields:
            if f.number in seen_nums:
                raise ParseError(f"{msg}.{f.name}: duplicate field number {f.number} "
                                 f"(also {seen_nums[f.number]})")
            if f.name in seen_names:
                raise ParseError(f"{msg}.{f.name}: duplicate field name")
            seen_nums[f.number] = f.name
            seen_names.add(f.name)
            max_field = max(max_field, f.number)
            stem = msg + pascal(f.name)
            constants.append(("k" + stem, f.number))
            wire_constants.append(("k" + stem + "Wire", f.wire))
        max_fields.append(("k" + msg + "MaxField", max_field))
        for f in fields:
            if f.oneof:
                oneof_members.append(("k" + msg + pascal(f.name), f.number))
        if max_field > 32:
            raise ParseError(f"{msg}: max field {max_field} > 32 (presence bitmap is uint32)")

    lines = [
        "// GENERATED by scripts/gen_fields.py -- DO NOT EDIT.",
        "// Source: proto/example.proto",
        f"// proto sha256: {hashlib.sha256(proto_text.encode('utf-8')).hexdigest()}",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace q207 {",
        "namespace pod {",
        "namespace f {",
    ]
    for name, value in constants:
        lines.append(f"constexpr uint32_t {name} = {value};")
    for name, value in wire_constants:
        lines.append(f"constexpr uint32_t {name} = {value};")
    for name, value in max_fields:
        lines.append(f"constexpr uint32_t {name} = {value};")
    lines.append("")
    for name, value in max_fields:
        lines.append(f"static_assert({name} <= 32, \"presence bitmap is uint32\");")
    for name, _ in oneof_members:
        lines.append(f"static_assert({name} <= 31, \"oneof contact_bits uses 1u << field\");")
    lines += [
        "}  // namespace f",
        "}  // namespace pod",
        "}  // namespace q207",
        "",
    ]
    return "\n".join(lines)


CPP_SCALAR = {
    "int32": "int32_t", "int64": "int64_t", "uint32": "uint32_t",
    "uint64": "uint64_t", "sint32": "int32_t", "sint64": "int64_t",
    "bool": "bool", "fixed32": "uint32_t", "fixed64": "uint64_t",
    "sfixed32": "int32_t", "sfixed64": "int64_t", "float": "float",
    "double": "double", "string": "std::string", "bytes": "std::string",
}


def cpp_type(field: Field, messages: dict[str, list[Field]]) -> str:
    if field.kind == "map":
        return "std::vector<MapEntry>"
    if field.kind in messages:
        return field.kind
    return CPP_SCALAR.get(field.kind, "std::string")


def cpp_decl(field: Field, messages: dict[str, list[Field]]) -> str:
    t = cpp_type(field, messages)
    if field.label == "repeated" or field.kind == "map":
        return f"std::vector<{t}> {field.name};" if field.kind != "map" else f"{t} {field.name};"
    if field.kind in messages:
        return f"{t} {field.name};"
    if t == "bool":
        return f"bool {field.name} = false;"
    if t in ("std::string",):
        return f"std::string {field.name};"
    return f"{t} {field.name} = 0;"


def _scalar_cpp(kind: str) -> str:
    if kind not in CPP_SCALAR:
        raise ParseError(f"unsupported scalar type {kind}")
    return CPP_SCALAR[kind]


def _map_shape(messages: dict[str, list[Field]]) -> tuple[str, str]:
    shapes = {(f.map_key, f.map_value) for fields in messages.values() for f in fields if f.kind == "map"}
    if not shapes:
        return ("", "")
    if len(shapes) != 1:
        raise ParseError(f"map fields use multiple key/value shapes: {sorted(shapes)}")
    return next(iter(shapes))


def _field_cpp_type(f: Field, messages: dict[str, list[Field]]) -> str:
    if f.kind == "map":
        return "MapEntry"
    if f.kind in messages:
        return f.kind
    return _scalar_cpp(f.kind)


def _field_cpp_decl(f: Field, messages: dict[str, list[Field]]) -> str:
    t = _field_cpp_type(f, messages)
    if f.kind == "map":
        return f"std::vector<{t}> {f.name};"
    if f.label == "repeated":
        return f"std::vector<{t}> {f.name};"
    if f.kind in messages:
        return f"{t} {f.name};"
    if t == "std::string":
        return f"std::string {f.name};"
    if t == "bool":
        return f"bool {f.name} = false;"
    return f"{t} {f.name} = 0;"


def _message_order(messages: dict[str, list[Field]]) -> list[str]:
    order: list[str] = []
    seen: set[str] = set()
    active: set[str] = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        if name in active:
            raise ParseError(f"message dependency cycle at {name}")
        active.add(name)
        for f in messages.get(name, []):
            if f.kind in messages:
                visit(f.kind)
        active.remove(name)
        seen.add(name)
        order.append(name)

    for name in messages:
        visit(name)
    return order


def _oneof_meta(msg: str, group: str) -> tuple[str, str]:
    # Preserve the historical User.contact field names for object_model/tape.
    if group == "contact":
        return ("contact_bits", "oneof_case")
    return (group + "_bits", group + "_case")


def _field_clear(f: Field, messages: dict[str, list[Field]]) -> str:
    if f.kind == "map" or f.label == "repeated":
        return f"    {f.name}.clear();"
    if f.kind in messages:
        return f"    {f.name}.clear();"
    if _field_cpp_type(f, messages) == "std::string":
        return f"    {f.name}.clear();"
    if _field_cpp_type(f, messages) == "bool":
        return f"    {f.name} = false;"
    return f"    {f.name} = 0;"


def generate_pod(messages: dict[str, list[Field]], proto_text: str) -> str:
    order = _message_order(messages)
    map_key, map_value = _map_shape(messages)
    lines = [
        "// GENERATED by scripts/gen_fields.py -- DO NOT EDIT.",
        "// Source: proto/example.proto",
        f"// proto sha256: {hashlib.sha256(proto_text.encode('utf-8')).hexdigest()}",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <string>",
        "#include <vector>",
        "",
        "namespace q207 {",
        "namespace pod {",
        "",
    ]
    if map_key and map_value:
        lines += [
            "struct MapEntry {",
            f"  {_scalar_cpp(map_key)} key;",
            f"  {_scalar_cpp(map_value)} value = 0;",
            "};",
            "",
        ]
    for msg in order:
        fields = messages[msg]
        lines += ["struct " + msg + " {"]
        if msg == "Profile":
            lines.append("  bool present = false;")
        lines += ["  uint32_t presence = 0;", "  uint32_t unknown_fields = 0;"]
        groups = sorted({f.oneof_name for f in fields if f.oneof})
        for group in groups:
            bits, case = _oneof_meta(msg, group)
            lines += [f"  uint32_t {bits} = 0;", f"  uint32_t {case} = 0;"]
        for f in fields:
            if f.label == "optional":
                lines.append(f"  bool has_{f.name} = false;")
        for f in fields:
            lines.append(f"  {_field_cpp_decl(f, messages)}")
        lines += [
            "",
        ]
        if msg == "Profile":
            lines.append("  bool present_field(uint32_t field) const {")
        else:
            lines.append("  bool present(uint32_t field) const {")
        lines += [
            "    return field >= 1 && field <= 32 && ((presence >> (field - 1)) & 1u) != 0;",
            "  }",
            "  void mark(uint32_t field) {",
            "    if (field >= 1 && field <= 32) presence |= (1u << (field - 1));",
            "  }",
            "  void clear() {",
        ]
        if msg == "Profile":
            lines.append("    present = false;")
        lines += [
            "    presence = 0;",
            "    unknown_fields = 0;",
        ]
        for group in groups:
            bits, case = _oneof_meta(msg, group)
            lines += [f"    {bits} = 0;", f"    {case} = 0;"]
        for f in fields:
            if f.label == "optional":
                lines.append(f"    has_{f.name} = false;")
        for f in fields:
            lines.append(_field_clear(f, messages))
        lines += ["  }", "};", ""]
    lines += ["}  // namespace pod", "}  // namespace q207", ""]
    return "\n".join(lines)


def generate_schema_json(parser: Parser, proto_text: str) -> str:
    import json
    messages = []
    for name, fields in parser.messages.items():
        messages.append({
            "name": name,
            "fields": [{
                "name": f.name,
                "number": f.number,
                "type": f.kind,
                "label": f.label,
                "wire": f.wire,
                "map": f.kind == "map",
                "map_key": f.map_key,
                "map_value": f.map_value,
                "oneof": f.oneof,
                "oneof_name": f.oneof_name,
            } for f in fields],
        })
    return json.dumps({
        "syntax": "proto3",
        "package": parser.package,
        "proto_sha256": hashlib.sha256(proto_text.encode("utf-8")).hexdigest(),
        "messages": messages,
    }, ensure_ascii=False, indent=2) + "\n"


def generate_schema_header(parser: Parser, proto_text: str) -> str:
    lines = [
        "// GENERATED by scripts/gen_fields.py -- DO NOT EDIT.",
        "// Source: proto/example.proto",
        f"// proto sha256: {hashlib.sha256(proto_text.encode('utf-8')).hexdigest()}",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace q207 {",
        "namespace schema {",
        "",
        "struct FieldInfo {",
        "  const char* name;",
        "  uint32_t number;",
        "  uint32_t wire;",
        "  bool repeated;",
        "  bool optional;",
        "  bool oneof;",
        "  bool map;",
        "  const char* oneof_name;",
        "};",
        "struct MessageInfo {",
        "  const char* name;",
        "  const FieldInfo* fields;",
        "  uint32_t count;",
        "};",
        "",
    ]
    names = []
    for msg, fields in parser.messages.items():
        arr = "k" + msg + "Fields"
        names.append((msg, arr, len(fields)))
        lines.append(f"inline constexpr FieldInfo {arr}[] = {{")
        for f in fields:
            lines.append(
                f'    {{"{f.name}", {f.number}, {f.wire}, '
                f'{"true" if f.label == "repeated" else "false"}, '
                f'{"true" if f.label == "optional" else "false"}, '
                f'{"true" if f.oneof else "false"}, '
                f'{"true" if f.kind == "map" else "false"}, '
                f'"{f.oneof_name}"' + '},'
            )
        lines.append("};")
        lines.append("")
    lines.append("inline constexpr MessageInfo kMessages[] = {")
    for msg, arr, count in names:
        lines.append(f'    {{"{msg}", {arr}, {count}}},')
    lines.append("};")
    lines.append('inline constexpr const char* kPackage = "' + parser.package + '";')
    lines += ["", "}  // namespace schema", "}  // namespace q207", ""]
    return "\n".join(lines)


def _dts_name(kind: str) -> str:
    if kind == "UserBatch":
        return "DecodedBatch"
    return "Decoded" + kind


def _dts_scalar(kind: str) -> str:
    if kind == "string" or kind == "bytes":
        return "string"
    if kind == "bool":
        return "boolean"
    if kind in ("int64", "uint64", "sint64", "fixed64", "sfixed64"):
        return "string"
    return "number"


def dts_type(field: Field, messages: dict[str, list[Field]]) -> str:
    if field.kind == "map":
        return f"Record<string, {_dts_scalar(field.map_value)}>"
    if field.kind in messages:
        base = _dts_name(field.kind)
    else:
        base = _dts_scalar(field.kind)
    if field.label == "repeated":
        base += "[]"
    if field.kind in messages and field.label != "repeated":
        base += " | null"
    return base


def generate_dts(parser: Parser) -> str:
    lines = [
        "// GENERATED by scripts/gen_fields.py -- DO NOT EDIT.",
        "// Source: proto/example.proto",
        "// ArkTS types for the NAPI decodeUser/decodeMany object shape.",
        "",
    ]
    for msg in _message_order(parser.messages):
        lines.append(f"export interface {_dts_name(msg)} {{")
        for f in parser.messages[msg]:
            t = dts_type(f, parser.messages)
            optional = f.oneof or f.label == "optional"
            lines.append(f"  {f.name}{'?' if optional else ''}: {t};")
        lines += ["}", ""]
    lines += [
        "export const ping: () => string;",
        "export const add: (a: number, b: number) => number;",
        "export const decodeUser: (bytes: Uint8Array) => DecodedUser;",
        "export const decodeMany: (bytes: Uint8Array) => DecodedBatch;",
        "",
        "declare const entry: {",
        "  ping: () => string;",
        "  add: (a: number, b: number) => number;",
        "  decodeUser: (bytes: Uint8Array) => DecodedUser;",
        "  decodeMany: (bytes: Uint8Array) => DecodedBatch;",
        "};",
        "",
        "export default entry;",
        "",
    ]
    return "\n".join(lines)


def _snake(name: str) -> str:
    out = []
    for i, ch in enumerate(name):
        if ch.isupper() and i and (name[i - 1].islower() or (i + 1 < len(name) and name[i + 1].islower())):
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


SUPPORTED_DECODER_SCALARS = {"string", "int32", "int64"}
SUPPORTED_MAP_SCALARS = {"string", "int32", "int64"}


def validate_decoder_support(messages: dict[str, list[Field]]) -> None:
    for msg, fields in messages.items():
        for f in fields:
            if f.kind == "map":
                if f.map_key not in SUPPORTED_MAP_SCALARS or f.map_value not in SUPPORTED_MAP_SCALARS:
                    raise ParseError(
                        f"{msg}.{f.name}: decoder generator does not support map<{f.map_key},{f.map_value}>")
                continue
            if f.kind in messages:
                continue
            if f.kind not in SUPPORTED_DECODER_SCALARS:
                raise ParseError(f"{msg}.{f.name}: decoder generator does not support type {f.kind}")


def _read_expr(kind: str, ptr: str, lim: str = "lim") -> str:
    if kind == "string":
        return f"r.read_string(&{ptr}, {lim}.max_string_bytes)"
    if kind == "int32":
        return f"r.read_int32(&{ptr})"
    if kind == "int64":
        return f"r.read_int64(&{ptr})"
    raise ParseError(f"unsupported decoder read type {kind}")


def _oneof_meta(msg: str, group: str) -> tuple[str, str]:
    if group == "contact":
        return ("contact_bits", "oneof_case")
    return (group + "_bits", group + "_case")


def _wire_expr(wire: int) -> str:
    return {
        WIRE_VARINT: "wire::kWireVarint",
        WIRE_FIXED64: "wire::kWireFixed64",
        WIRE_LENGTH: "wire::kWireLength",
        WIRE_FIXED32: "wire::kWireFixed32",
    }[wire]


def _field_case(msg: str, f: Field, target: str, messages: dict[str, list[Field]], indent: str) -> list[str]:
    c = f"f::k{msg}{pascal(f.name)}"
    w = _wire_expr(f.wire)
    lines = [f"{indent}case {c}: {{"]
    lines.append(f"{indent}  if (w != {w}) {{ e = mismatch(r, lim, field, w, &{target}->unknown_fields); break; }}")
    if f.kind == "map":
        lines += [
            f"{indent}  if ({target}->{f.name}.size() >= lim.max_repeated_elements) {{",
            f"{indent}    e = r.fail_with(Code::FieldBudget);",
            f"{indent}    break;",
            f"{indent}  }}",
            f"{indent}  {target}->mark(field);",
            f"{indent}  const uint64_t cap = std::min<uint64_t>(r.remaining(), lim.max_message_bytes);",
            f"{indent}  e = r.enter_length(cap);",
            f"{indent}  if (e != Code::Ok) break;",
            f"{indent}  pod::MapEntry entry;",
            f"{indent}  e = parse_map_entry(r, lim, &entry);",
            f"{indent}  if (e == Code::Ok) e = r.leave();",
            f"{indent}  if (e == Code::Ok) {target}->{f.name}.push_back(std::move(entry));",
            f"{indent}  break;",
            f"{indent}}}",
        ]
        return lines
    if f.kind in messages and f.label == "repeated":
        # UserBatch.users keeps the batch-specific limits/object accounting.
        if msg == "UserBatch" and f.name == "users" and f.kind == "User":
            lines += [
                f"{indent}  if ({target}->{f.name}.size() >= lim.max_batch_users) {{",
                f"{indent}    e = r.fail_with(Code::BatchTooLarge);",
                f"{indent}    break;",
                f"{indent}  }}",
                f"{indent}  if ({target}->{f.name}.size() >= lim.max_objects) {{",
                f"{indent}    e = r.fail_with(Code::ObjectBudget);",
                f"{indent}    break;",
                f"{indent}  }}",
            ]
        else:
            lines += [
                f"{indent}  if ({target}->{f.name}.size() >= lim.max_repeated_elements) {{",
                f"{indent}    e = r.fail_with(Code::FieldBudget);",
                f"{indent}    break;",
                f"{indent}  }}",
            ]
        lines += [
            f"{indent}  {target}->mark(field);",
            f"{indent}  const uint64_t cap = std::min<uint64_t>(r.remaining(), lim.max_message_bytes);",
            f"{indent}  e = r.enter_length(cap);",
            f"{indent}  if (e != Code::Ok) break;",
            f"{indent}  {target}->{f.name}.emplace_back();",
            f"{indent}  e = parse_{_snake(f.kind)}(r, lim, &{target}->{f.name}.back());",
            f"{indent}  if (e == Code::Ok) e = r.leave();",
            f"{indent}  break;",
            f"{indent}}}",
        ]
        return lines
    if f.kind in messages:
        lines += [
            f"{indent}  {target}->mark(field);",
            f"{indent}  const uint64_t cap = std::min<uint64_t>(r.remaining(), lim.max_message_bytes);",
            f"{indent}  e = r.enter_length(cap);",
            f"{indent}  if (e != Code::Ok) break;",
            f"{indent}  e = parse_{_snake(f.kind)}(r, lim, &{target}->{f.name});",
            f"{indent}  if (e == Code::Ok) e = r.leave();",
            f"{indent}  break;",
            f"{indent}}}",
        ]
        return lines
    if f.label == "repeated":
        lines += [
            f"{indent}  if ({target}->{f.name}.size() >= lim.max_repeated_elements) {{",
            f"{indent}    e = r.fail_with(Code::FieldBudget);",
            f"{indent}    break;",
            f"{indent}  }}",
            f"{indent}  {target}->mark(field);",
            f"{indent}  {target}->{f.name}.emplace_back();",
            f"{indent}  e = {_read_expr(f.kind, target + '->' + f.name + '.back()')};",
            f"{indent}  break;",
            f"{indent}}}",
        ]
        return lines
    lines.append(f"{indent}  {target}->mark(field);")
    if f.label == "optional":
        lines.append(f"{indent}  {target}->has_{f.name} = true;")
    if f.oneof:
        bits, case = _oneof_meta(msg, f.oneof_name)
        lines += [
            f"{indent}  {target}->{bits} |= (1u << f::k{msg}{pascal(f.name)});",
            f"{indent}  {target}->{case} = f::k{msg}{pascal(f.name)};",
        ]
    lines.append(f"{indent}  e = {_read_expr(f.kind, target + '->' + f.name)};")
    lines += [f"{indent}  break;", f"{indent}}}"]
    return lines


def generate_decoder(messages: dict[str, list[Field]], proto_text: str) -> str:
    validate_decoder_support(messages)
    order = _message_order(messages)
    map_key, map_value = _map_shape(messages)
    lines = [
        "// GENERATED by scripts/gen_fields.py -- DO NOT EDIT.",
        "// Source: proto/example.proto",
        f"// proto sha256: {hashlib.sha256(proto_text.encode('utf-8')).hexdigest()}",
        "// Included inside q207::decode's anonymous namespace; relies on mismatch/Reader/f.",
        "",
    ]
    for msg in order:
        lines.append(f"Code parse_{_snake(msg)}(Reader& r, const Limits& lim, pod::{msg}* out);")
    if map_key and map_value:
        lines.append("Code parse_map_entry(Reader& r, const Limits& lim, pod::MapEntry* out);")
    lines.append("")
    if map_key and map_value:
        lines += [
            "Code parse_map_entry(Reader& r, const Limits& lim, pod::MapEntry* out) {",
            "  Code e = r.count_object();",
            "  if (e != Code::Ok) return e;",
            "  out->key = {};",
            "  out->value = 0;",
            "  while (!r.at_end()) {",
            "    uint32_t field = 0, w = 0;",
            "    e = r.read_tag(&field, &w);",
            "    if (e != Code::Ok) return e;",
            "    if (w == kWireEndGroup) return r.fail_with(Code::GroupUnexpected);",
            "    e = r.count_field();",
            "    if (e != Code::Ok) return e;",
            f"    if (field == 1 && w == {_wire_expr(WIRE_LENGTH if map_key == 'string' else WIRE_VARINT)}) {{ e = {_read_expr(map_key, 'out->key')}; }}",
            f"    else if (field == 2 && w == {_wire_expr(WIRE_LENGTH if map_value == 'string' else WIRE_VARINT)}) {{ e = {_read_expr(map_value, 'out->value')}; }}",
            "    else { e = r.skip_field(field, w); }",
            "    if (e != Code::Ok) return e;",
            "  }",
        ]
        if map_key == "string":
            lines += [
                "  if (lim.map_reject_dangerous) {",
                "    static const char* kDangerous[] = {",
                '        "__proto__", "constructor", "prototype", "toString", "valueOf", "hasOwnProperty",',
                '        "isPrototypeOf", "propertyIsEnumerable", "__defineGetter__", "__defineSetter__",',
                '        "__lookupGetter__", "__lookupSetter__",',
                "    };",
                "    for (const char* d : kDangerous) if (out->key == d) return r.fail_with(Code::MapKeyDangerous);",
                "  }",
            ]
        lines += ["  return Code::Ok;", "}", ""]
    for msg in order:
        fields = messages[msg]
        lines += [
            f"Code parse_{_snake(msg)}(Reader& r, const Limits& lim, pod::{msg}* out) {{",
            "  Code e = r.count_object();",
            "  if (e != Code::Ok) return e;",
        ]
        if msg == "Profile":
            lines.append("  out->present = true;")
        lines += [
            "  while (!r.at_end()) {",
            "    uint32_t field = 0, w = 0;",
            "    e = r.read_tag(&field, &w);",
            "    if (e != Code::Ok) return e;",
            "    if (w == kWireEndGroup) return r.fail_with(Code::GroupUnexpected);",
            "    e = r.count_field();",
            "    if (e != Code::Ok) return e;",
            "    switch (field) {",
        ]
        for f in fields:
            lines += _field_case(msg, f, "out", messages, "      ")
        lines += [
            "      default: ++out->unknown_fields; e = r.skip_field(field, w); break;",
            "    }",
            "    if (e != Code::Ok) return e;",
            "  }",
            "  return Code::Ok;",
            "}",
            "",
        ]
    return "\n".join(lines)


def normalize_newlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify generated files match proto")
    ap.add_argument("--write", action="store_true", help="write generated files (default)")
    args = ap.parse_args()

    proto_text = PROTO.read_text(encoding="utf-8")
    try:
        parser = Parser(tokenize(proto_text))
        messages = parser.parse()
        outputs = {
            OUT_FIELDS: generate(messages, proto_text),
            OUT_POD: generate_pod(messages, proto_text),
            OUT_SCHEMA_H: generate_schema_header(parser, proto_text),
            OUT_SCHEMA_JSON: generate_schema_json(parser, proto_text),
            OUT_DECODER: generate_decoder(messages, proto_text),
            OUT_DTS: generate_dts(parser),
        }
    except ParseError as e:
        print(f"gen_fields: {e}", file=sys.stderr)
        return 2

    normalized = {p: normalize_newlines(text) for p, text in outputs.items()}
    if args.check:
        failed = False
        for path, text in normalized.items():
            if not path.exists():
                print(f"gen_fields: missing generated file {path}", file=sys.stderr)
                failed = True
                continue
            current = normalize_newlines(path.read_text(encoding="utf-8"))
            if current != text:
                diff = difflib.unified_diff(
                    current.splitlines(keepends=True),
                    text.splitlines(keepends=True),
                    fromfile=str(path),
                    tofile="generated",
                )
                sys.stderr.writelines(diff)
                print(f"gen_fields: generated file is stale: {path.relative_to(ROOT)}", file=sys.stderr)
                failed = True
        if failed:
            print("gen_fields: run scripts/codegen.sh --write", file=sys.stderr)
            return 1
        print(f"gen_fields: OK ({len(normalized)} generated files)")
        return 0

    for path, text in normalized.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"gen_fields: wrote {path.relative_to(ROOT)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())