"""
Printing functions of the FPGA register-map XML to C header generator.

Every function here writes (or formats) one piece of the generated header:
doxygen blocks, defines, bit-fields, unions and structs. No XML knowledge,
only formatting; all the layout rules and fixed texts live in config.py.
"""

import textwrap

import config


# ------------------------------------------------------------------
# doxygen blocks
# ------------------------------------------------------------------
def write_file_header(out, file_name, brief):
    """Top of a module header; brief comes from the XML description."""
    out.write(config.FILE_HEADER_TEMPLATE.format(
        banner=config.BANNER, file_name=file_name,
        brief=brief + config.GENERATED_SUFFIX, ingroup=config.INGROUP,
        author=config.AUTHOR, year=config.YEAR, company=config.COMPANY))


def write_common_addresses_header(out):
    """Top of CommonAddresses.h (fixed brief, no '#pragma once')."""
    out.write(config.COMMON_ADDRESSES_HEADER_TEMPLATE.format(
        banner=config.BANNER, file_name=config.COMMON_ADDRESSES_FILE,
        ingroup=config.INGROUP, author=config.AUTHOR, year=config.YEAR,
        company=config.COMPANY))


def write_register_comment(out, brief):
    """Block comment above a register union (one empty line before it)."""
    out.write("\n" * config.BLANKS_BETWEEN_BLOCKS)
    out.write(config.REGISTER_COMMENT_TEMPLATE.format(
        banner=config.BANNER, brief=brief))


def write_summary_comment(out, title):
    """Block comment above a group struct (two empty lines before it)."""
    out.write("\n" * config.BLANKS_BEFORE_STRUCT)
    out.write(config.SUMMARY_COMMENT_TEMPLATE.format(
        banner=config.BANNER, title=title))


def write_global_comment(out, title, module):
    """Block comment above the global struct of a module."""
    out.write("\n" * config.BLANKS_BEFORE_STRUCT)
    out.write(config.GLOBAL_COMMENT_TEMPLATE.format(
        banner=config.BANNER, title=title, module=module))


# ------------------------------------------------------------------
# defines
# ------------------------------------------------------------------
def write_module_define(out, prefix, suffix, value) -> None:
    """One '/** doc */' plus '#define <PREFIX>_<SUFFIX> (value)' couple."""
    out.write("/** %s */\n" % config.DEFINE_COMMENTS[suffix])
    field = suffix.ljust(config.DEFINE_FIELD_WIDTHS[suffix])
    if not field.endswith(" "):
        field += " "
    out.write("#define %s_%s(%s)\n" % (prefix, field, value))


def write_address_define(out, name: str, value: str, comment: str) -> None:
    """One CommonAddresses.h entry, preceded by an empty line."""
    field = name.ljust(config.ADDRESS_DEFINE_WIDTH)
    if not field.endswith(" "):
        field += " "
    out.write(f"\n/** {comment} */\n")
    out.write(f"#define {field}({config.ADDRESS_VALUE_FORMAT % value})\n")


# ------------------------------------------------------------------
# bit-fields and their doxygen comments
# ------------------------------------------------------------------
def wrap_description(text) -> list[str]:
    """Field description split in lines of config.COMMENT_TEXT_WIDTH."""
    normalized = " ".join(text.split())
    lines = textwrap.wrap(normalized, config.COMMENT_TEXT_WIDTH,
                          break_long_words=False, break_on_hyphens=False)
    return lines or [""]


def write_bitfield(out, name, bits, description):
    """One 'uint32_t name : N;' line with its wrapped comment."""
    head = "%s%s %s:%*d;" % (config.FIELD_INDENT, config.C_WORD_TYPE,
                             name.ljust(config.FIELD_NAME_WIDTH),
                             config.FIELD_BITS_WIDTH, bits)
    lines = wrap_description(description)
    lines = [config.COMMENT_OPEN + lines[0]] + [
        " " * config.COMMENT_CONTINUATION_COLUMN + line for line in lines[1:]]
    lines[-1] += config.COMMENT_CLOSE
    out.write(head + "  " + lines[0] + "\n")
    for line in lines[1:]:
        out.write(line + "\n")


# ------------------------------------------------------------------
# unions and structs
# ------------------------------------------------------------------
def write_union(out, type_base, instance, brief, fields):
    """Register union: doxygen brief, inner bit-field struct, u32 alias."""
    write_register_comment(out, brief)
    out.write("union %s%s\n{\n" % (type_base, config.UNION_SUFFIX))
    out.write("%sstruct %s%s\n%s{\n" % (config.INDENT, type_base,
                                        config.STRUCT_SUFFIX, config.INDENT))
    for name, bits, description in fields:
        write_bitfield(out, name, bits, description)
    out.write("%s} %s;\n" % (config.INDENT, instance))
    out.write("%s%s %s;\n" % (config.INDENT, config.C_WORD_TYPE,
                              config.UNION_RAW_MEMBER))
    out.write("};\n")


def format_member(ctype, name, array=""):
    """Aligned struct member line: '    Type_u<pad>name[array];'."""
    return "%s%s%s%s;" % (config.INDENT, ctype.ljust(config.MEMBER_TYPE_WIDTH),
                          name, array)


def format_group_padding(name, bound):
    """Short padding member closing a group struct (8 spaces of gap)."""
    return "%s%s%s%s[%s];" % (config.INDENT, config.C_BYTE_TYPE,
                              " " * config.PADDING_MEMBER_GAP, name, bound)


def write_struct(out, tag, member_lines):
    """A struct from its tag and the already formatted member lines."""
    out.write(f"struct {tag}\n{{\n")    # escape sequence
    for line in member_lines:
        out.write(line + "\n")
    out.write("};\n")
