
// Auto-generated: XML <-> C preset mapping table for RipplerX
// Format: Each entry contains program name, parameter names (enum order), XML values, C values, and status per param.
// Note: When a value is missing or not found, 0 is used instead of -99999.
// Status: 0=match, 1=missing in XML, 2=extra in XML, 3=value mismatch

#ifndef RIPPLERX_PRESET_TABLE_H
#define RIPPLERX_PRESET_TABLE_H

#include <stdint.h>

typedef struct {
    const char* program_name;
    const char* param_names[64]; // ProgramParameters order
    float xml_values[64];        // Value from XML (or 0 if missing)
    float c_values[64];          // Value from C array (or 0 if missing)
    int status[64];              // 0=match, 1=missing in XML, 2=extra in XML, 3=value mismatch
    int param_count;
} PresetParamMapping;

// Table is lexicographically sorted by program_name
extern const PresetParamMapping preset_table[];
extern const int preset_table_size;

#endif // RIPPLERX_PRESET_TABLE_H
