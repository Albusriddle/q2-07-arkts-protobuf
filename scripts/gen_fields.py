#!/usr/bin/env python3
"""Q2-07 proto3 subset field-table generator (deterministic, fail-closed).

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
OUT = ROOT / "src" / "host" / "decoder" / "generated" / "q207_demo_fields.h"

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
            self.ident()
            while self.peek().value == ".":
                self.next()
                self.ident()
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
        self.ident()
        self.expect("{")
        while self.peek().value != "}":
            tok = self.peek()
            if tok.kind == "eof":
                raise ParseError("unexpected EOF in oneof")
            if tok.value in ("optional", "repeated", "map", "oneof", "message"):
                raise ParseError(f"line {tok.line}: unsupported oneof member {tok.value!r}")
            fields.append(self.parse_field("", oneof=True))
        self.expect("}")

    def parse_field(self, label: str, oneof: bool = False) -> Field:
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
        return Field(name, number, typ, label, wire, map_key, map_value, oneof)


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


def normalize_newlines(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify checked-in header matches proto")
    ap.add_argument("--write", action="store_true", help="write generated header (default)")
    args = ap.parse_args()

    proto_text = PROTO.read_text(encoding="utf-8")
    try:
        messages = Parser(tokenize(proto_text)).parse()
        generated = generate(messages, proto_text)
    except ParseError as e:
        print(f"gen_fields: {e}", file=sys.stderr)
        return 2

    normalized = normalize_newlines(generated)
    if args.check:
        if not OUT.exists():
            print(f"gen_fields: missing generated header {OUT}", file=sys.stderr)
            return 1
        current = normalize_newlines(OUT.read_text(encoding="utf-8"))
        if current != normalized:
            diff = difflib.unified_diff(
                current.splitlines(keepends=True),
                normalized.splitlines(keepends=True),
                fromfile=str(OUT),
                tofile="generated",
            )
            sys.stderr.writelines(diff)
            print("gen_fields: generated header is stale; run scripts/codegen.sh --write", file=sys.stderr)
            return 1
        print(f"gen_fields: OK {OUT.relative_to(ROOT)}")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(normalized, encoding="utf-8", newline="\n")
    print(f"gen_fields: wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
