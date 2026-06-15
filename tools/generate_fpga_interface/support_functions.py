"""
Small helper functions (plus the command line parser) shared by the FPGA
register-map XML to C header generator modules.

Every function here is kept tiny on purpose: anything bigger belongs to
printing.py (output formatting) or convert_xml_in_c_header.py (logic).
"""

import argparse
import re
from xml.etree.ElementTree import Element

import config

# a group key tells which registers share one C struct: either the literal
# "plain" or a tuple tagging a replicated block / an isolated LU RAM
GroupKey = str | tuple


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    """Command line interface: extend here when new options are needed."""
    parser = argparse.ArgumentParser(
        prog=config.TOOL_NAME,
        description="Convert FPGA register description XML files into C "
                    "style headers (the subsystems summary XML produces "
                    + config.COMMON_ADDRESSES_FILE + ").")
    parser = argparse.ArgumentParser(
            prog="convert_xml_in_c_header",
            description=""
            "read the FPGS XML files as a dictionary and create "
            "the equivalent C header file. Just try simple call.",
        )
    parser.add_argument(
        "-p",
        "--xml_path",
        help="Full path containing XML files to "
        "be parsed. Default path will be sw/submodules/fpga/xhw/modules/",
        default=config.DEFAULT_INPUT_PATH,
    )
    parser.add_argument(
        "-x",
        "--xml_files",
        help="list of alternative XML files to "
        "be parsed (no path in name!), default files will be "
        "cell_configuration.xml, dl_cluster_pb_schedule.xml "
        "cell_timing.xml, top.xml",
        nargs="*",
        default=config.default_files,
    )
    parser.add_argument(
        "-d",
        "--dest_path",
        help="custom path list where to save the resulting "
        "header file, alternative to default sw/fpgaInterface.",
        default=config.DEFAULT_OUTPUT_PATH,
    )
    parser.add_argument(
        "-u",
        "--submodules_update",
        help="remove current directory containing "
        "the submodules, i.e. the FPGA xml files, and does a git checkout."
        "Default is FALSE.",
        action="store_true",
        default=False,
    )
    parser.add_argument(
        "-o",
        "--overwrite",
        help="remove destination directory contents and "
        "replace with script results. Otherwise, update only changed files"
        " (to avoid unneeded build trigger). Default is FALSE.",
        action="store_true",
        default=False,
    )
    return parser.parse_args(argv)


def _cap_word(word: str) -> str:
    """Capitalize the first letter and every letter following a digit."""
    return re.sub(r"(^|\d)([a-z])",
                  lambda match: match.group(1) + match.group(2).upper(), word)


def to_camel(name: str) -> str:
    """PRACH_CLUSTER_SCHEDULE -> PrachClusterSchedule, L1TF -> L1Tf."""
    return "".join(_cap_word(word.lower()) for word in name.split("_") if word)


def to_lower_camel(name: str) -> str:
    """UL_STATUS_TASK -> ulStatusTask."""
    camel = to_camel(name)
    return camel[:1].lower() + camel[1:]


def to_title(name: str) -> str:
    """DL_CLUSTER_PB_SCHEDULE -> Dl Cluster Pb Schedule (doxygen briefs)."""
    return " ".join(_cap_word(word.lower()) for word in name.split("_") if word)


def parse_int(text: str | None, default: int | None = None) -> int | None:
    """Integer from a decimal or hexadecimal literal ('64', '0x1C', ...)."""
    if text is None:
        return default
    return int(text.strip(), 0)


def strip_hex_prefix(text: str) -> str:
    """'0x10F' -> '10F', '0' -> '0' (for the MODULE_ID concatenation)."""
    text = text.strip()
    return text[2:] if text[:2].lower() == "0x" else text


def parse_bits(text: str) -> tuple[int, int]:
    """'31:16' -> (31, 16)."""
    msb, lsb = (int(part) for part in text.split(":"))
    return msb, lsb


def find_text(element: Element | None, tag: str,
              default: str | None = None) -> str | None:
    """Stripped text of a child tag, or default when missing or empty."""
    if element is None:
        return default
    value = element.findtext(tag)
    return value.strip() if value and value.strip() else default


def normalize_text(text: str) -> str:
    """Collapse every whitespace run to a single space."""
    return " ".join(text.split())


def last_digit(name: str) -> str:
    """Last digit of a register name ('' if none): base of the res names."""
    digits = [char for char in name if char.isdigit()]
    return digits[-1] if digits else ""


def res_field_name(base: str, index: int) -> str:
    """res, res1... without a base digit; resN, resN+1... with it."""
    if not base:
        return config.RES_FIELD + (str(index) if index else "")
    return config.RES_FIELD + str(int(base) + index)


def reserved_field_name(index: int) -> str:
    """reserved, reserved1, reserved2... inside a single struct."""
    return config.RESERVED_FIELD + (str(index) if index else "")


def header_file_name(system_name: str) -> str:
    """TIMER -> Timer.h"""
    return to_camel(system_name) + config.HEADER_EXTENSION


def define_base(suffix: str) -> str:
    """'LENGTH_OF_LU_RAM_REGISTER_2' -> 'LENGTH_OF_LU_RAM_REGISTER'."""
    head, separator, tail = suffix.rpartition("_")
    if separator and tail.isdigit() and head in config.DEFINE_COMMENTS:
        return head
    return suffix


def define_field_width(suffix: str) -> int:
    """Name column width of a define; numbered names reuse their base."""
    return config.DEFINE_FIELD_WIDTHS[define_base(suffix)]


def is_top_system(root: Element) -> bool:
    """True for the subsystems summary (top.xml): no registers inside."""
    return (root.find(config.TAG_REGISTER) is None
            and root.find(config.TAG_SUBSYSTEM) is not None)


def is_ram(register: dict) -> bool:
    """True when the register is a LU RAM, cloned as an array."""
    return register["implementation"] == config.IMPLEMENTATION_RAM


def is_inline_multiply(register: dict, bus_bytes: int) -> bool:
    """Multiplied register repeated back to back: an array of itself."""
    return (register["multiply"] is not None
            and register["multiply_offset"] == bus_bytes)


def is_block_multiply(register: dict, bus_bytes: int) -> bool:
    """Multiplied register belonging to a repeated block of registers."""
    return (register["multiply"] is not None
            and register["multiply_offset"] is not None
            and register["multiply_offset"] != bus_bytes)


def group_key(register: dict, bus_bytes: int, uniform_ram: bool) -> GroupKey:
    """Adjacent registers with the same key share the same C struct.

    LU RAMs join the plain registers only when every RAM of the module has
    the same length; otherwise each one is isolated in its own struct so it
    can use its own LENGTH_OF_LU_RAM_REGISTER_N define.
    """
    if is_block_multiply(register, bus_bytes):
        return ("block", register["multiply"], register["multiply_offset"])
    if is_ram(register) and not uniform_ram:
        return ("ram", register["address"])
    return "plain"
