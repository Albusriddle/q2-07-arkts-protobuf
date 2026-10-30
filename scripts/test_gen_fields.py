#!/usr/bin/env python3
"""Negative tests for the schema/codegen generator (host-only, no writes)."""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gen_fields", ROOT / "scripts" / "gen_fields.py")
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)


def parse(src: str):
    return mod.Parser(mod.tokenize(src)).parse()


def expect_parse_fail(name: str, src: str) -> None:
    try:
        messages = parse(src)
        mod.generate(messages, src)
    except mod.ParseError:
        print(f"  ok  {name}: rejected")
        return
    raise AssertionError(f"{name}: parser unexpectedly accepted input")


def main() -> int:
    current = (ROOT / "proto" / "example.proto").read_text(encoding="utf-8")
    messages = parse(current)
    mod.validate_decoder_support(messages)
    mod.generate_decoder(messages, current)
    print("  ok  current schema: accepted")

    base = 'syntax = "proto3";\npackage t;\n'
    expect_parse_fail("unknown construct", base + "service X {}\n")
    expect_parse_fail("duplicate field", base + 'message M { int32 a = 1; int32 b = 1; }\n')
    expect_parse_fail("required", base + 'message M { required int32 a = 1; }\n')

    try:
        bad = parse(base + 'message M { bytes blob = 1; }\n')
        mod.validate_decoder_support(bad)
    except mod.ParseError:
        print("  ok  unsupported scalar: rejected")
    else:
        raise AssertionError("unsupported scalar was accepted")

    try:
        bad = parse(base + 'message M { map<string,bytes> m = 1; }\n')
        mod.validate_decoder_support(bad)
    except mod.ParseError:
        print("  ok  unsupported map value: rejected")
    else:
        raise AssertionError("unsupported map value was accepted")

    try:
        bad = parse(base + 'message M { int64 a = 33; }\n')
        mod.generate(bad, base)
    except mod.ParseError:
        print("  ok  field number > 32: rejected")
    else:
        raise AssertionError("field number > 32 was accepted")

    generic_src = base + """
message Inner { string label = 1; }
message Outer {
  Inner inner = 1;
  repeated string tags = 2;
  optional int32 level = 3;
  oneof contact { string email = 4; string phone = 5; }
}
"""
    parser = mod.Parser(mod.tokenize(generic_src))
    generic = parser.parse()
    mod.validate_decoder_support(generic)
    pod = mod.generate_pod(generic, generic_src)
    dec = mod.generate_decoder(generic, generic_src)
    dts = mod.generate_dts(parser)
    assert "struct Inner" in pod and "Inner inner;" in pod
    assert "parse_outer" in dec and "parse_inner" in dec
    assert "export interface DecodedOuter" in dts
    print("  ok  generic schema: POD/decoder/DTS generated")

    map_src = base + """
message M {
  map<string,string> labels = 1;
  map<string,int32> counts = 2;
}
"""
    map_parser = mod.Parser(mod.tokenize(map_src))
    map_messages = map_parser.parse()
    mod.validate_decoder_support(map_messages)
    map_pod = mod.generate_pod(map_messages, map_src)
    map_dec = mod.generate_decoder(map_messages, map_src)
    map_dts = mod.generate_dts(map_parser)
    assert "MLabelsEntry" in map_pod and "MCountsEntry" in map_pod
    assert "std::string value;" in map_pod
    assert "parse_m_labels_entry" in map_dec and "parse_m_counts_entry" in map_dec
    assert "Record<string, string>" in map_dts
    print("  ok  multi-shape map: per-field entries and string defaults")

    print("gen_fields negative tests: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
