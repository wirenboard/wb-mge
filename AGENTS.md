# Agent Instructions

## Build or Test Command (macOS)

```bash
source ~/.espressif/tools/activate_idf_v5.4.sh && make build-frontend unittests build-idf-project
```

## Rules

- **Always build before delivering results to the user.** Run the build command above and confirm it succeeds before presenting any changes.
- **Always run tests after every fix.** After each code change, run the relevant test suite and confirm all tests pass before proceeding. For backend (C) changes: `make unittests`. For frontend changes: `cd main/frontend && npm run test`.
- **Always run `make clean` after editing `sdkconfig.defaults`.** The IDF build system caches configuration; without a clean the new defaults will not be picked up correctly.

## Backend (Embedded C) Coding Standards

Full style guide: https://raw.githubusercontent.com/wirenboard/codestyle/refs/heads/master/embedded_c.ru.md

### Naming
- Use `snake_case` everywhere — no CamelCase for functions or variables.
- All functions and `#define`s must be prefixed with the module/library name (e.g. `mcp230xx_read_gpio()`).
- `typedef`-d types end with `_t`; plain `struct` names do not.
- Enum variants must start with the enum type name as prefix (e.g. `W1_TRANSACTION_SEND` for enum `w1_transaction`).

### Declarations
- Functions not intended for external use must be `static`.
- Prototypes of `static` functions go in the `.c` file; public interface goes in the `.h` file.
- No magic numbers — use `#define` constants.

### Formatting
- 4 spaces per indentation level; tabs are forbidden.
- Max line length: 120 characters.
- Binary operators surrounded by spaces; unary operators are not.
- Always use braces `{}` around block bodies, even single-line ones.
- Opening brace on the same line as the statement, except for function definitions and multi-line conditions — those get the brace on a new line.
- Operations must always be explicitly parenthesised even when precedence is obvious: `(a || (b && (!c)))`.

### Macros
- Avoid preprocessor macros where language features suffice; prefer `const`, `static inline`, or compile-time `if`.
- Macros that contain statements must use `do {} while (0)`.
- All macro names are UPPER_CASE.

### Comments
- All code comments must be written in **English**.
- Short comments go after the line; long comments go before the line.
- `//` is aligned to the nearest position that is a multiple of 4 characters; one space between `//` and the text.

## Frontend Coding Standards

### SVG Icons
- All SVG files must reside in the `assets/` folder as separate files — no inline SVG markup in templates.

### Inline Styles
- No inline styles (`style="..."`) are allowed anywhere in templates.

### Localization
- Every user-visible text string in the UI must be localized via the i18n system. No hardcoded strings in templates or scripts.

### CSS Class Naming (BEM)
We use BEM (Block, Element, Modifier) methodology for CSS class names.

- **Block** — independent component: `.button`, `.menu`, `.card`
- **Element** — part of a block: `.button-icon`, `.menu-item`, `.card-title`
- **Modifier** — variant of a block or element: `.button-iconMobile`, `.menu-itemActive`, `.card-titleWithButton`

Block→Element separator: `-` (hyphen).
Element→Modifier separator: camelCase (no additional separator): `.menu-itemActive`.

## Hardware: GPIO and Interface Pin Mapping

### Ethernet Interface RTL8201FI

| ESP32    | GPIO18 | GPIO23 | GPIO0 | GPIO5 | GPIO19 | GPIO22 | GPIO21 | GPIO25 | GPIO26 | GPIO27  |
|----------|--------|--------|-------|-------|--------|--------|--------|--------|--------|---------|
| RTL8201FI | MDIO  | MDC    | CLK   | RST   | TXD0   | TXD1   | TXEN   | RXD0   | RXD1   | CRS_DV  |

### GPIO Expander TCA9535 (I2C address: 0x20)

ESP32 connects to TCA9535 via: SDA → GPIO32, SCL → GPIO33.

In the table below, ON means a logic high on the port enables the node. For the Status LED, if the port is in HiZ state, the LED is on.

| TCA9535 port | Function              | Logic |
|--------------|-----------------------|-------|
| PD00         | RS485-1 terminator    | ON    |
| PD01         | RS485-2 terminator    | ON    |
| PD02         | RS485-1 pull-up       | ON    |
| PD03         | RS485-2 pull-up       | ON    |
| PD04         | LED Wi-Fi             | OFF   |
| PD05         | LED Eth               | OFF   |
| PD06         | VOut and LED VOut     | ON    |
| PD07         | Status LED            | ON    |
| PD10         | MIO disable           | OFF   |

### RS-485 Interfaces

| ESP32   | GPIO10 | GPIO9 | GPIO4 | GPIO14 | GPIO12 | GPIO15 |
|---------|--------|-------|-------|--------|--------|--------|
| RS485-1 | TX     | RX    | RTS   |        |        |        |
| RS485-2 |        |       |       | TX     | RX     | RTS    |

Note: for ESP32, RX is input and TX is output.

**Important:** Inside the module, the MIO part on STM32 is connected to RS485-2 and operates via Modbus RTU at the Modbus address printed on the device label. The MIO part can be disabled via the TCA9535 PD10 pin.

### Buttons

| ESP32      | GPIO34 |
|------------|--------|
| Config (B1) | +     |

Note: the button drives the ESP32 pin to logic low when pressed.
