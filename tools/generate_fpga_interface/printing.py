"""
Printing functions of the FPGA register-map XML to C header generator.

Every function here writes (or formats) one piece of the generated header:
doxygen blocks, defines, bit-fields, unions and structs. No XML knowledge,
only formatting; all the layout rules and fixed texts live in config.py.
"""

import textwrap
from typing import TextIO

import config
import support_functions as sf


# ------------------------------------------------------------------
# doxygen blocks
# ------------------------------------------------------------------
def write_file_header(out: TextIO, file_name: str, brief: str) -> None:
    """Top of a module header; brief comes from the XML description."""
    out.write(config.FILE_HEADER_TEMPLATE.format(
        banner=config.BANNER, file_name=file_name,
        brief=brief + config.GENERATED_SUFFIX, ingroup=config.INGROUP,
        author=config.AUTHOR, year=config.YEAR, company=config.COMPANY))


def write_common_addresses_header(out: TextIO) -> None:
    """Top of CommonAddresses.h (fixed brief, no '#pragma once')."""
    out.write(config.COMMON_ADDRESSES_HEADER_TEMPLATE.format(
        banner=config.BANNER, file_name=config.COMMON_ADDRESSES_FILE,
        ingroup=config.INGROUP, author=config.AUTHOR, year=config.YEAR,
        company=config.COMPANY))


def write_register_comment(out: TextIO, brief: str) -> None:
    """Block comment above a register union (one empty line before it)."""
    out.write("\n" * config.BLANKS_BETWEEN_BLOCKS)
    out.write(config.REGISTER_COMMENT_TEMPLATE.format(
        banner=config.BANNER, brief=brief))


def write_summary_comment(out: TextIO, title: str) -> None:
    """Block comment above a group struct (two empty lines before it)."""
    out.write("\n" * config.BLANKS_BEFORE_STRUCT)
    out.write(config.SUMMARY_COMMENT_TEMPLATE.format(
        banner=config.BANNER, title=title))


def write_global_comment(out: TextIO, title: str, module: str) -> None:
    """Block comment above the global struct of a module."""
    out.write("\n" * config.BLANKS_BEFORE_STRUCT)
    out.write(config.GLOBAL_COMMENT_TEMPLATE.format(
        banner=config.BANNER, title=title, module=module))


# ------------------------------------------------------------------
# defines
# ------------------------------------------------------------------
def write_module_define(out: TextIO, prefix: str, suffix: str,
                        value: str) -> None:
    """One '/** doc */' plus '#define <PREFIX>_<SUFFIX> (value)' couple."""
    base = sf.define_base(suffix)
    out.write(f"/** {config.DEFINE_COMMENTS[base]} */\n")
    field = suffix.ljust(config.DEFINE_FIELD_WIDTHS[base])
    if not field.endswith(" "):
        field += " "
    out.write(f"#define {prefix}_{field}({value})\n")


def write_address_define(out: TextIO, name: str, value: str,
                         comment: str) -> None:
    """One CommonAddresses.h entry, preceded by an empty line.

    value is already the literal to emit (e.g. '0xA0000000'); the callers
    format it, so there is no '%'/str mismatch here.
    """
    field = name.ljust(config.ADDRESS_DEFINE_WIDTH)
    if not field.endswith(" "):
        field += " "
    out.write(f"\n/** {comment} */\n")
    out.write(f"#define {field}({value})\n")


# ------------------------------------------------------------------
# bit-fields and their doxygen comments
# ------------------------------------------------------------------
def wrap_description(text: str) -> list[str]:
    """Field description split in lines of config.COMMENT_TEXT_WIDTH."""
    normalized = " ".join(text.split())
    lines = textwrap.wrap(normalized, config.COMMENT_TEXT_WIDTH,
                          break_long_words=False, break_on_hyphens=False)
    return lines or [""]


def write_bitfield(out: TextIO, name: str, bits: int, description: str) -> None:
    """One 'uint32_t name : N;' line with its wrapped comment."""
    head = (f"{config.FIELD_INDENT}{config.C_WORD_TYPE} "
            f"{name.ljust(config.FIELD_NAME_WIDTH)}:"
            f"{bits:{config.FIELD_BITS_WIDTH}d};")
    lines = wrap_description(description)
    lines = [config.COMMENT_OPEN + lines[0]] + [
        " " * config.COMMENT_CONTINUATION_COLUMN + line for line in lines[1:]]
    lines[-1] += config.COMMENT_CLOSE
    out.write(f"{head}  {lines[0]}\n")
    for line in lines[1:]:
        out.write(f"{line}\n")


# ------------------------------------------------------------------
# unions and structs
# ------------------------------------------------------------------
def write_union(out: TextIO, type_base: str, instance: str, brief: str,
                fields: list[tuple[str, int, str]]) -> None:
    """Register union: doxygen brief, inner bit-field struct, u32 alias."""
    write_register_comment(out, brief)
    out.write(f"union {type_base}{config.UNION_SUFFIX}\n{{\n")
    out.write(f"{config.INDENT}struct {type_base}{config.STRUCT_SUFFIX}\n"
              f"{config.INDENT}{{\n")
    for name, bits, description in fields:
        write_bitfield(out, name, bits, description)
    out.write(f"{config.INDENT}}} {instance};\n")
    out.write(f"{config.INDENT}{config.C_WORD_TYPE} {config.UNION_RAW_MEMBER};\n")
    out.write("};\n")


def format_member(ctype: str, name: str, array: str = "") -> str:
    """Aligned struct member line: '    Type_u<pad>name[array];'."""
    return f"{config.INDENT}{ctype.ljust(config.MEMBER_TYPE_WIDTH)}{name}{array};"


def format_group_padding(name: str, bound: str) -> str:
    """Short padding member closing a group struct (8 spaces of gap)."""
    return (f"{config.INDENT}{config.C_BYTE_TYPE}"
            f"{' ' * config.PADDING_MEMBER_GAP}{name}[{bound}];")


def write_struct(out: TextIO, tag: str, member_lines: list[str]) -> None:
    """A struct from its tag and the already formatted member lines."""
    out.write(f"struct {tag}\n{{\n")    # escape sequence
    for line in member_lines:
        out.write(f"{line}\n")
    out.write("};\n")
