# generate_fpga_interface

Generate C style register-map headers from the FPGA register description
XML files (Rohde & Schwarz `pi3` schema).

## Usage

```sh
python3 convert_xml_in_c_header.py [-o OUTPUT_DIR] file1.xml file2.xml ...
```

* Each *module* XML (the ones holding `<register>` elements) produces a
  `<ModuleName>.h`, e.g. `timer.xml` -> `Timer.h`.
* The *subsystems summary* XML (`top.xml`, only `<subsystem>` elements)
  produces `CommonAddresses.h` with the base-address `#define`s, and is
  always processed after the modules.
* Without `-o`, each header is written next to its source XML.
* A problem in one file is logged as a warning and does not stop the run;
  the list of the files that had problems is printed at the end and the
  exit status is non-zero.

On success each file logs:

```
INFO:generate_fpga_interface:XML parsing is over, find the result file as:
/path/to/Header.h
```

## Files

| file                          | content                                            |
|-------------------------------|----------------------------------------------------|
| `config.py`                   | constants, fixed names, templates, column widths   |
| `printing.py`                 | functions writing defines / fields / unions / docs |
| `support_functions.py`        | small helpers and the `argparse` parser            |
| `convert_xml_in_c_header.py`  | main: XML parsing and header building              |

## Layout rules

* `uint32_t res*` bit-fields pad the unused bits inside a register; `res`
  takes the trailing digit of the register name as base (`res5`, `res6`...).
* `uint8_t reserved*[]` pads the byte gap between two registers of the same
  struct.
* Adjacent registers are merged into one struct/array when they share the
  same replication kind (plain, `multiply`, or RAM `length`).
* A `multiply` register repeated every bus word becomes an array followed by
  `uint8_t paddingToNextRegister[..._PADDING_TO_NEXT_MULTIPLE_ADDRESS]`
  (pattern `ABC<pad>ABC<pad>`); a RAM register becomes an array with each
  element cloned in place (pattern `AA<pad>BB<pad>CC<pad>`).
* `uint8_t paddingToNextRegister[..._PADDING_TO_NEXT_REGISTERS_ADDRESS]`
  joins two non-adjacent groups.

## Known limitation: comment wrapping

The doxygen field descriptions are wrapped greedily to
`config.COMMENT_TEXT_WIDTH` (53). The reference headers are *not* wrappable
by any single width: within `CellConfiguration.h`, for instance, the
TDD-slot comments break around 43 columns while the `UT_TEST` "Low part"
comments break around 54, so a few long comments wrap at a different column
than the originals. The generated code is otherwise byte-identical to the
references (verified on Timer, Prach, CellConfiguration, CellTiming,
DlClusterPbSchedule and CommonAddresses); only the position of those line
breaks differs. Adjust `COMMENT_TEXT_WIDTH` if a different column is wanted.
