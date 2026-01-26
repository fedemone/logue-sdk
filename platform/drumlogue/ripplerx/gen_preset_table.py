import os
import xml.etree.ElementTree as ET
import re

# Paths
PRESET_DIR = os.path.join(os.path.dirname(__file__), '../presets')
CONSTANTS_H = os.path.join(os.path.dirname(__file__), 'constants.h')
TABLE_C = os.path.join(os.path.dirname(__file__), 'table.c')

# Helper: parse ProgramParameters enum from constants.h
def parse_enum_params(constants_h):
    with open(constants_h, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    enum_start = None
    enum_end = None
    for i, line in enumerate(lines):
        if 'enum ProgramParameters' in line:
            enum_start = i
        if enum_start is not None and 'last_param' in line:
            enum_end = i
            break
    params = []
    for line in lines[enum_start+1:enum_end]:
        m = re.match(r'\s*([a-zA-Z0-9_]+)', line)
        if m and not m.group(1).startswith('//'):
            name = m.group(1)
            if name != 'last_param':
                params.append(name)
    return params

# Helper: parse programs array from constants.h
def parse_programs_array(constants_h):
    # Robustly extract the programs array, ignoring comments and handling multi-line formatting
    with open(constants_h, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    in_array = False
    array_lines = []
    for line in lines:
        if 'static float32_t programs' in line:
            in_array = True
            continue
        if in_array:
            if '};' in line:
                break
            # Remove comments and whitespace
            line = line.split('//')[0].strip()
            if line:
                array_lines.append(line)
    arr_text = ' '.join(array_lines)
    # Split by '},' for each program
    programs = []
    for prog in re.findall(r'\{([^}]*)\}', arr_text):
        vals = [float(x.strip()) for x in prog.split(',') if x.strip()]
        programs.append(vals)
    if not programs:
        raise RuntimeError('Could not find programs array')
    return programs

# Helper: get program names from constants.h
def parse_program_names(constants_h):
    # Robustly extract c_programName array, ignoring comments and handling multi-line formatting
    with open(constants_h, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    in_array = False
    names = []
    for line in lines:
        if 'const char* const c_programName' in line:
            in_array = True
            continue
        if in_array:
            if '};' in line:
                break
            # Remove comments and whitespace
            line = line.split('//')[0].strip()
            # Remove braces and trailing commas
            line = line.replace('{', '').replace('}', '').replace(',', ' ')
            # Extract quoted names
            for match in re.finditer(r'"([^"]+)"', line):
                names.append(match.group(1))
    if not names:
        raise RuntimeError('Could not find c_programName')
    return names

# Helper: parse XML preset file
def parse_xml_preset(xml_path):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    params = {}
    for param in root.findall('PARAM'):
        pid = param.get('id')
        val = param.get('value')
        if val is not None:
            try:
                params[pid] = float(val)
            except Exception:
                params[pid] = val
        else:
            params[pid] = None
    return params

# Main mapping logic
def main():
    params_enum = parse_enum_params(CONSTANTS_H)
    program_names = parse_program_names(CONSTANTS_H)
    programs = parse_programs_array(CONSTANTS_H)
    preset_files = sorted([f for f in os.listdir(PRESET_DIR) if f.endswith('.xml')])
    mapping = []
    for idx, prog_name in enumerate(program_names):
        xml_file = next((f for f in preset_files if f.lower().startswith(prog_name.lower())), None)
        if not xml_file:
            continue
        xml_params = parse_xml_preset(os.path.join(PRESET_DIR, xml_file))
        c_vals = programs[idx] if idx < len(programs) else []
        xml_vals = []
        c_vals_fixed = []
        status = []
        for i, pname in enumerate(params_enum):
            v_xml = xml_params.get(pname, None)
            v_c = c_vals[i] if i < len(c_vals) else None
            # Use 0 for missing/empty values
            if v_xml is None:
                xml_vals.append(0)
                status.append(1)
            else:
                xml_vals.append(v_xml)
            if v_c is None:
                c_vals_fixed.append(0)
            else:
                c_vals_fixed.append(v_c)
            if v_xml is not None and v_c is not None:
                if abs(float(v_xml) - float(v_c)) < 1e-4:
                    status.append(0)
                else:
                    status.append(3)
            elif v_xml is not None and v_c is None:
                status.append(2)
        mapping.append((prog_name, params_enum, xml_vals, c_vals_fixed, status, len(params_enum)))
    # Write table.c
    with open(TABLE_C, 'w', encoding='utf-8') as f:
        f.write('// Auto-generated: XML <-> C preset mapping table\n')
        f.write('#include "table.h"\n\n')
        f.write('const PresetParamMapping preset_table[] = {\n')
        for entry in mapping:
            prog, pnames, xmlv, cv, st, plen = entry
            f.write('    {\n')
            f.write(f'        "{prog}",\n')
            f.write('        { ' + ', '.join(f'"{n}"' for n in pnames) + ' },\n')
            f.write('        { ' + ', '.join(f'{v:.8g}' for v in xmlv) + ' },\n')
            f.write('        { ' + ', '.join(f'{v:.8g}' for v in cv) + ' },\n')
            f.write('        { ' + ', '.join(str(s) for s in st) + ' },\n')
            f.write(f'        {plen}\n')
            f.write('    },\n')
        f.write('};\n')
        f.write(f'const int preset_table_size = {len(mapping)};\n')

if __name__ == '__main__':
    main()
