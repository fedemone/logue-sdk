#!/usr/bin/env python3
"""
Convert FPGA register description XML files into C style headers.

Each module XML produces a <ModuleName>.h with one union per register and
the structs grouping them (multiplied blocks and LU RAMs become arrays);
the subsystems summary (top.xml) produces CommonAddresses.h and is always
processed last. Problems are reported as warnings without stopping the
conversion and the list of the affected files is printed at the end.
"""
import shutil
import sys
from pathlib import Path
import typing
import logging
import os
import xml.etree.ElementTree as ET

import config
import printing
import filecmp
import support_functions as sf

from git_mgr.git_mgr import checkout_submodules
from lib.utils import (
    get_root,
    temporary_change_dir,
)

log = logging.getLogger(config.LOGGER_NAME)

python_dir = (Path(__file__).resolve()).parents[1]
sys.path.append(str(python_dir))
sys.path.append(str(python_dir) + "/lib")

# ------------------------------------------------------------------
# XML parsing
# ------------------------------------------------------------------
def parse_signal(element) -> dict[str, typing.Any]:
    """One <signal> as a dictionary."""
    msb, lsb = sf.parse_bits(sf.find_text(element, config.TAG_BIT_MSB_LSB, "0:0"))
    value = element.find(config.TAG_SIGNAL_VALUE)
    literal = sf.find_text(value, config.TAG_VALUE_LITERAL, "0") if value is not None else "0"
    return {
        "name": sf.find_text(element, config.TAG_NAME, ""),
        "description": sf.find_text(element, config.TAG_DESCRIPTION, ""),
        "msb": msb,
        "lsb": lsb,
        "value_literal": literal,
    }


def parse_register(element) -> dict[str, typing.Any]:
    """One <register> as a dictionary."""
    return {
        "name": sf.find_text(element, config.TAG_NAME, ""),
        "description": sf.find_text(element, config.TAG_DESCRIPTION, ""),
        "address": sf.parse_int(sf.find_text(element, config.TAG_ADDRESS), 0),
        "width": sf.parse_int(sf.find_text(element, config.TAG_WIDTH)),
        "implementation": sf.find_text(element, config.TAG_IMPLEMENTATION, ""),
        "length": sf.parse_int(sf.find_text(element, config.TAG_LENGTH)),
        "multiply": sf.parse_int(sf.find_text(element, config.TAG_MULTIPLY)),
        "multiply_offset": sf.parse_int(sf.find_text(element, config.TAG_MULTIPLY_OFFSET)),
        "signals": [parse_signal(signal) for signal in element.findall(config.TAG_SIGNAL)],
    }


def parse_module(root) -> dict[str, typing.Any]:
    """A whole module XML as a dictionary."""
    bus_width = sf.parse_int(sf.find_text(root, config.TAG_DATA_BUS_WIDTH),
                             config.DEFAULT_DATA_BUS_WIDTH)
    return {
        "name": sf.find_text(root, config.TAG_NAME, ""),
        "description": sf.find_text(root, config.TAG_DESCRIPTION, ""),
        "bus_bytes": bus_width // 8,
        "registers": [parse_register(register)
                      for register in root.findall(config.TAG_REGISTER)],
    }


# ------------------------------------------------------------------
# C names of registers and groups
# ------------------------------------------------------------------
def union_type(system_name, register, group_suffix="") -> str:
    """Base type name (without _u/_s) of the union of a register."""
    if (register["name"] in config.PREFIX_WITH_MODULE
            or register["name"] in config.COMMON_REGISTERS):
        return sf.to_camel(system_name) + sf.to_camel(register["name"])
    return sf.to_camel(register["name"]) + group_suffix


def member_name(system_name, register, group_suffix="") -> str:
    """Name of the register member inside the group/global struct."""
    override = config.MEMBER_NAME_OVERRIDES.get((system_name, register["name"]))
    if override:
        return override
    if (register["name"] in config.PREFIX_WITH_MODULE
            or register["name"] in config.COMMON_REGISTERS):
        return sf.to_lower_camel(system_name + "_" + register["name"])
    return sf.to_lower_camel(register["name"]) + group_suffix


def group_names(system_name, index) -> tuple[typing.Any |
                                             str, typing.Any |
                                             str, str |
                                             typing.Any]:
    """(struct tag, member name, summary title) of a register group."""
    override = config.GROUP_NAMES.get((system_name, index))
    number = "" if index == 0 else str(index + 1)
    if override:
        tag, member, title = override
    else:
        base = sf.to_camel(system_name) + config.DEFAULT_GROUP_BASENAME + number
        tag, member, title = (base + config.STRUCT_SUFFIX,
                              base[:1].lower() + base[1:], None)
    if title is None:
        title = sf.to_title(system_name) + (" " + number if number else "")
    return tag, member, title


# ------------------------------------------------------------------
# layout building
# ------------------------------------------------------------------
def build_fields(register: dict[str, typing.Any], total_bits: int,
                 warnings: list[str]) -> list[tuple[str, int, str]]:
    """Bit-fields of one register union, gaps filled with res paddings."""
    fields, cursor, gaps = [], 0, 0
    base = sf.last_digit(register["name"])
    for signal in sorted(register["signals"], key=lambda signal: signal["lsb"]):
        if signal["lsb"] < cursor:
            warnings.append(f"register {register['name']}: signal "
                            f"{signal['name']} overlaps the previous bits")
            continue
        if signal["lsb"] > cursor:
            fields.append((sf.res_field_name(base, gaps),
                           signal["lsb"] - cursor, config.RES_DESCRIPTION))
            gaps += 1
        fields.append((sf.to_lower_camel(signal["name"]),
                       signal["msb"] - signal["lsb"] + 1, signal["description"]))
        cursor = signal["msb"] + 1
    if cursor > total_bits:
        warnings.append(f"register {register['name']}: signals exceed the "
                        f"{total_bits} bits of the bus")
    elif cursor < total_bits:
        fields.append((sf.res_field_name(base, gaps),
                       total_bits - cursor, config.RES_DESCRIPTION))
    return fields


def ram_length_defines(payload: list[dict[str, typing.Any]]
                       ) -> tuple[dict[int, str], list[tuple[str, str]]]:
    """Map every LU RAM length to its define suffix and list the rows.

    One shared LENGTH_OF_LU_RAM_REGISTER when all the RAMs have the same
    length, otherwise a numbered LENGTH_OF_LU_RAM_REGISTER_N per distinct one.
    """
    lengths = [register["length"] or 0
               for register in payload if sf.is_ram(register)]
    distinct = list(dict.fromkeys(lengths))
    if len(distinct) <= 1:
        suffixes = {length: config.DEFINE_LU_RAM_LENGTH for length in distinct}
    else:
        suffixes = {length: f"{config.DEFINE_LU_RAM_LENGTH}_{number}"
                    for number, length in enumerate(distinct, start=1)}
    rows = [(suffixes[length], str(length)) for length in distinct]
    return suffixes, rows


def split_groups(registers: list[dict[str, typing.Any]], bus_bytes: int,
                 uniform_ram: bool) -> list[dict[str, typing.Any]]:
    """Adjacent registers with the same replication kind form one group."""
    groups: list[dict[str, typing.Any]] = []
    for register in registers:
        key = sf.group_key(register, bus_bytes, uniform_ram)
        if not groups or groups[-1]["key"] != key:
            groups.append({"key": key, "registers": []})
        groups[-1]["registers"].append(register)
    return groups


def set_define(defines: dict[str, int], key: str, value: typing.Any,
               register: dict[str, typing.Any], warnings: list[str]) -> None:
    """Record a define value, warning when two registers disagree."""
    previous = defines.setdefault(key, value)
    if previous != value:
        warnings.append(f"register {register['name']}: {key} {value} does not "
                        f"match the previous value {previous}")


def build_group(module: dict[str, typing.Any],
                group: dict[str, typing.Any],
                index: int,
                defines: dict[str, typing.Any],
                ram_define_suffix: dict[int, str],
                warnings: list[str]) -> tuple[list[typing.Any],
                                              list[typing.Any],
                                              int, int, bool]:
    """Unions, struct member lines and address extent of one group."""
    system_name = module["name"]
    bus = module["bus_bytes"]
    suffix = config.GROUP_TYPE_SUFFIXES.get((system_name, index), "")
    registers = group["registers"]
    key = group["key"]
    block = isinstance(key, tuple) and key[0] == "block"
    unions, members = [], []
    reserved_index = 0
    start = cursor = registers[0]["address"]
    ram_registers = [register for register in registers if sf.is_ram(register)]
    last_ram = ram_registers[-1] if ram_registers else None
    for position, register in enumerate(registers):
        following = (registers[position + 1]
                     if position + 1 < len(registers) else None)
        if register["address"] > cursor:
            members.append(printing.format_member(
                config.C_BYTE_TYPE, sf.reserved_field_name(reserved_index),
                f"[{register['address'] - cursor}]"))
            reserved_index += 1
            cursor = register["address"]
        elif register["address"] < cursor:
            warnings.append(f"register {register['name']}: address "
                            f"0x{register['address']:X} overlaps the previous "
                            f"register")
            cursor = register["address"]
        type_base = union_type(system_name, register, suffix)
        unions.append((type_base, sf.to_lower_camel(register["name"]),
                       sf.normalize_text(register["description"]),
                       build_fields(register, bus * 8, warnings)))
        array = ""
        if sf.is_ram(register):
            length = register["length"] or 0
            array = f"[{system_name}_{ram_define_suffix[length]}]"
            cursor += length * bus
        elif sf.is_inline_multiply(register, bus):
            set_define(defines, "payload_multiply", register["multiply"],
                       register, warnings)
            array = f"[{system_name}_{config.DEFINE_PAYLOAD_MULTIPLY}]"
            cursor += register["multiply"] * bus
        else:
            cursor += bus
        members.append(printing.format_member(
            type_base + config.UNION_SUFFIX,
            member_name(system_name, register, suffix), array))
        if sf.is_inline_multiply(register, bus):
            gap = (following["address"] - cursor) if following else 0
            if gap < 0:
                warnings.append(f"register {register['name']}: the multiplied "
                                f"array overlaps the next register")
                gap = 0
            set_define(defines, "multiple_padding", gap, register, warnings)
            members.append(printing.format_group_padding(
                config.PADDING_FIELD,
                f"{system_name}_{config.DEFINE_PADDING_MULTIPLE}"))
            cursor += gap
        if register is last_ram:
            gap = (following["address"] - cursor) if following else 0
            if gap < 0:
                warnings.append(f"register {register['name']}: the LU RAM "
                                f"overlaps the next register")
                gap = 0
            set_define(defines, "after_lu_ram", gap, register, warnings)
            members.append(printing.format_group_padding(
                config.PADDING_FIELD_LU_RAM,
                f"{system_name}_{config.DEFINE_PADDING_AFTER_LU_RAM}"))
            cursor += gap
    if block:
        multiply, offset = key[1], key[2]
        span = cursor - start
        if span > offset:
            warnings.append(f"registers multiplied every 0x{offset:X} bytes "
                            f"span 0x{span:X}")
        set_define(defines, "payload_multiply", multiply,
                   registers[0], warnings)
        set_define(defines, "multiple_padding", max(offset - span, 0),
                   registers[0], warnings)
        members.append(printing.format_group_padding(
            config.PADDING_FIELD,
            f"{system_name}_{config.DEFINE_PADDING_MULTIPLE}"))
        end = start + (multiply - 1) * offset + span
    else:
        end = cursor
    return unions, members, start, end, block


def module_id_value(commons: list[dict[str, typing.Any]],
                    warnings: list[str]) -> str:
    """The MODULE_ID define: concatenation of the version literals."""
    register = next((register for register in commons
                     if register["name"] == config.DEFINE_MODULE_ID), None)
    if register is None:
        warnings.append(f"missing the {config.DEFINE_MODULE_ID} register")
        return "0x0"
    parts = []
    for name in config.MODULE_ID_SIGNALS:
        signal = next((signal for signal in register["signals"]
                       if signal["name"] == name), None)
        if signal is None:
            warnings.append(f"register {config.DEFINE_MODULE_ID}: missing the "
                            f"{name} signal")
            continue
        parts.append(sf.strip_hex_prefix(signal["value_literal"]))
    return "0x" + "".join(parts)


def build_define_rows(defines: dict[str, typing.Any],
                      ram_define_rows: list[tuple[str, str]],
                      total_size: int,
                      module_id: str) -> list[tuple[str, str]]:
    """The '#define' couples of a module header, in the reference order."""
    rows = [(config.DEFINE_TOTAL_SIZE, str(total_size))]
    if "payload_multiply" in defines:
        rows.append((config.DEFINE_PAYLOAD_MULTIPLY,
                     str(defines["payload_multiply"])))
        rows.append((config.DEFINE_PADDING_MULTIPLE,
                     str(defines.get("multiple_padding", 0))))
    if ram_define_rows:
        rows.extend(ram_define_rows)
        rows.append((config.DEFINE_PADDING_AFTER_LU_RAM,
                     str(defines.get("after_lu_ram", 0))))
    if "payload_multiply" not in defines:
        rows.append((config.DEFINE_PAYLOAD_MULTIPLY, "1"))
    if "registers_padding" in defines:
        rows.append((config.DEFINE_PADDING_REGISTERS,
                     str(defines["registers_padding"])))
    rows.append((config.DEFINE_MODULE_ID, module_id))
    return rows


# ------------------------------------------------------------------
# header writers
# ------------------------------------------------------------------
def convert_module(module: dict[str, typing.Any], output_dir: str,
                   warnings: list[str]) -> str:
    """Write the C header of one module XML, return its path."""
    system_name = module["name"]
    bus = module["bus_bytes"]
    registers = sorted(module["registers"],
                       key=lambda register: register["address"])
    commons = [register for register in registers
               if register["name"] in config.COMMON_REGISTERS]
    payload = [register for register in registers
               if register["name"] not in config.COMMON_REGISTERS]
    if len(commons) != len(config.COMMON_REGISTERS):
        warnings.append(f"expected the {'/'.join(config.COMMON_REGISTERS)} "
                        f"common registers, found {len(commons)} of them")
    ram_define_suffix, ram_define_rows = ram_length_defines(payload)
    uniform_ram = len(ram_define_rows) <= 1
    defines: dict[str, typing.Any] = {}
    groups: list[dict[str, typing.Any]] = []
    for index, raw_group in enumerate(split_groups(payload, bus, uniform_ram)):
        unions, members, start, end, block = build_group(
            module, raw_group, index, defines, ram_define_suffix, warnings)
        tag, member, title = group_names(system_name, index)
        groups.append({"unions": unions, "members": members, "start": start,
                       "end": end, "block": block, "tag": tag,
                       "member": member, "title": title, "gap_after": 0})
    if groups and commons and groups[0]["start"] != len(commons) * bus:
        warnings.append(f"the registers do not start right after the common "
                        f"ones (0x{groups[0]['start']:X})")
    for current, following in zip(groups, groups[1:]):
        gap = following["start"] - current["end"]
        if gap < 0:
            warnings.append(f"register groups overlap around "
                            f"0x{following['start']:X}")
            gap = 0
        if gap > 0 and "registers_padding" in defines:
            warnings.append("more than one padding between register groups "
                            "is not supported")
            gap = 0
        if gap > 0:
            defines["registers_padding"] = gap
        current["gap_after"] = gap
    total_size = max((register["address"]
                      for register in registers), default=0) + bus
    rows = build_define_rows(defines, ram_define_rows, total_size,
                             module_id_value(commons, warnings))
    file_name = sf.header_file_name(system_name)
    path = os.path.abspath(os.path.join(output_dir, file_name))
    with open(path, "w", encoding="utf-8") as out:
        printing.write_file_header(out, file_name, module["description"])
        out.write("\n")
        for suffix, value in rows:
            printing.write_module_define(out, system_name, suffix, value)
        for register in commons:
            printing.write_union(out, union_type(system_name, register),
                                 sf.to_lower_camel(register["name"]),
                                 sf.normalize_text(register["description"]),
                                 build_fields(register, bus * 8, warnings))
        for group in groups:
            for union in group["unions"]:
                printing.write_union(out, *union)
            printing.write_summary_comment(out, group["title"])
            printing.write_struct(out, group["tag"], group["members"])
        printing.write_global_comment(out, sf.to_title(system_name),
                                      system_name)
        global_members: list[str] = []
        for register in commons:
            global_members.append(printing.format_member(
                union_type(system_name, register) + config.UNION_SUFFIX,
                member_name(system_name, register)))
        for group in groups:
            array = ""
            if group["block"]:
                array = f"[{system_name}_{config.DEFINE_PAYLOAD_MULTIPLY}]"
            global_members.append(printing.format_member(
                group["tag"], group["member"], array))
            if group["gap_after"] > 0:
                global_members.append(printing.format_member(
                    config.C_BYTE_TYPE, config.PADDING_FIELD,
                    f"[{system_name}_{config.DEFINE_PADDING_REGISTERS}]"))
        printing.write_struct(
            out, sf.to_camel(system_name) + config.STRUCT_SUFFIX,
            global_members)
    return path


def get_address() -> str:
    """get the value from file if exist or use default"""
    base_address = config.PL_BASE_ADDRESS_VALUE
    if os.path.exists(config.DEFAULT_BASE_ADDR_FILE):
        with open(config.DEFAULT_BASE_ADDR_FILE, "r", encoding="utf-8") as f:
            for line in f.readlines():
                if "[get_bd_addr_segs axi_mst/Reg] -force" in line:
                    base_address = line.split()[2]
    return base_address


def convert_top(root: typing.Any, output_dir: str) -> str:
    """Write CommonAddresses.h from the subsystems summary (top.xml)."""
    path = os.path.abspath(os.path.join(output_dir, config.COMMON_ADDRESSES_FILE))
    with open(path, "w", encoding="utf-8") as out:
        printing.write_common_addresses_header(out)
        printing.write_address_define(out, config.PL_BASE_ADDRESS_NAME,
                                      get_address(),
                                      config.PL_BASE_ADDRESS_COMMENT)
        for subsystem in root.findall(config.TAG_SUBSYSTEM):
            instance = sf.find_text(subsystem, config.TAG_INSTANCE_NAME, "")
            address = sf.parse_int(
                sf.find_text(subsystem, config.TAG_BASE_ADDRESS), 0)
            printing.write_address_define(
                out, instance + config.ADDRESS_DEFINE_SUFFIX, str(address),
                config.ADDRESS_COMMENT % instance)
    return path


def prepare_folders(args: typing.Any) -> None:
    """create missing folders and update submodules if requested"""
    if args.submodules_update or not os.path.exists(config.DEFAULT_INPUT_PATH):
        # import FPGA submodules, used in default path
        with temporary_change_dir(get_root()):
            checkout_submodules(force_option=True)
    if os.path.exists(config.TEMP_OUTPUT_PATH):
        shutil.rmtree(config.TEMP_OUTPUT_PATH, ignore_errors=True)
    os.makedirs(config.TEMP_OUTPUT_PATH, exist_ok=True)
    if not os.path.exists(args.dest_path):
        os.makedirs(args.dest_path)
        log.info("Created destination path:\n\t%s\n", args.dest_path)


# ------------------------------------------------------------------
# main
# ------------------------------------------------------------------
def output_dir_for(arguments: typing.Any, xml_path: str) -> typing.Any | str:
    """Destination directory of the header generated from one XML."""
    directory = arguments.dest_path if arguments.overwrite\
        else config.TEMP_OUTPUT_PATH\
            or os.path.dirname(os.path.abspath(xml_path))
    os.makedirs(directory, exist_ok=True)
    return directory


def finalize_files(args: typing.Any) -> None:
    """if overwriting is not forced, replace only different result files"""
    if args.overwrite:
        return
    for dirs, _, files in os.walk(config.TEMP_OUTPUT_PATH):
        for file in files:
            destination_file = args.dest_path + os.sep + file
            temporary_file = dirs + os.sep + file
            if not os.path.exists(destination_file):
                shutil.copyfile(temporary_file, destination_file)
            elif not filecmp.cmp(temporary_file, destination_file, shallow=False):
                os.remove(destination_file)
                shutil.copyfile(temporary_file, destination_file)


def main(argv=None) -> int:
    """read the XML file as a dictionary and create the equivalent C header file"""
    arguments = sf.parse_arguments(argv)
    logging.basicConfig(level=logging.INFO, format=config.LOG_FORMAT)
    problem_files, top_files = [], []

    if not arguments.overwrite:
            log.info("No overwriting is selected, so files will be updated only if changed\n")
    prepare_folders(arguments)

    for xml_path in [os.path.join(arguments.xml_path, x) for x in arguments.xml_files]:
        try:
            root = ET.parse(xml_path).getroot()
        except (ET.ParseError, OSError) as error:
            log.warning(config.WARN_FILE_PROBLEM, xml_path, error)
            problem_files.append(xml_path)
            continue
        if sf.is_top_system(root):
            top_files.append((xml_path, root))      # processed last
            continue
        warnings: list[typing.Any] = []
        try:
            header = convert_module(parse_module(root),
                                    output_dir_for(arguments, xml_path),
                                    warnings)
        except Exception as error:                  # keep converting the rest
            log.warning(config.WARN_FILE_PROBLEM, xml_path, error)
            problem_files.append(xml_path)
            continue
        for message in warnings:
            log.warning(config.WARN_FILE_PROBLEM, xml_path, message)
        if warnings:
            problem_files.append(xml_path)
        log.info(config.INFO_PARSING_OVER, header)
    for xml_path, root in top_files:
        try:
            header = convert_top(root, output_dir_for(arguments, xml_path))
        except Exception as error:                  # keep converting the rest
            log.warning(config.WARN_FILE_PROBLEM, xml_path, error)
            problem_files.append(xml_path)
            continue
        log.info(config.INFO_PARSING_OVER, header)
    if problem_files:
        log.warning(config.WARN_PROBLEM_SUMMARY, ", ".join(problem_files))

    finalize_files(arguments)

    return 1 if problem_files else 0


if __name__ == "__main__":
    raise SystemExit(main())
