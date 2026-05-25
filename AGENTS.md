# Agent Instructions

## Build or Test Command

```bash
make build-frontend unittests test-frontend build-idf-project
```

To run all tests:

```bash
make test
```

The Makefile sources the EIM activate script (`~/.espressif/tools/activate_idf_v5.4.sh`) internally — no manual `source` needed.

## Running individual QEMU API tests

To run a specific test file against QEMU:

```bash
make qemu-test PYTEST_ARGS="23_test_tx_disabled.py"
```

To run a specific test function:

```bash
make qemu-test PYTEST_ARGS="23_test_tx_disabled.py::test_tx_disabled_blocks_uart_transmission"
```

The `make qemu-test` target:
1. Rebuilds the QEMU firmware (incremental)
2. Creates the QEMU flash image
3. Launches QEMU
4. Runs pytest with `--qemu` flag
5. Kills QEMU after tests complete

## Rules

- **Always build before delivering results to the user.** Run the build command above and confirm it succeeds before presenting any changes.
- **Always run tests after every fix.** After each code change, run the relevant test suite and confirm all tests pass before proceeding. For backend (C) changes: `make unittests`. For frontend changes: `make test-frontend`.
- **Always update documentation when renaming or changing build targets.** After any change to `Makefile` or `qemu.mk` (adding, removing, or renaming targets; changing dependencies), grep all `*.md` files for the old target names and update every occurrence. README.md, README_QEMU.md, AGENTS.md, and `bugs/*/README.md` all reference make targets by name.

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

## QEMU and tests — gotchas

### Killing QEMU correctly

`pkill -9 qemu-system-xtensa` **silently fails** (stderr warning, exit 1, zero
processes killed): the process name `qemu-system-xtensa` is longer than 15
characters, and without `-f` `pkill` matches against the 15-char-truncated name
in `/proc/PID/comm`, which mismatches the real one.

**Always** use `-f` (match against the full command line):
```bash
pkill -9 -f qemu-system-xtensa
```

Verify it actually died:
```bash
pgrep -af qemu-system-xtensa            # must be empty
ss -tnlp 2>/dev/null | grep 8080        # port must be free
```

### Killing the wrapper bash script does NOT kill the QEMU child

`pkill -9 -f make qemu-tests` kills the script but the `qemu-system-xtensa` it
spawned keeps running → it still holds port 8080 → the next QEMU cannot bind
(`Could not set up host forwarding rule`) → the next run is garbage. After
killing any test-runner script, always also explicitly kill its children:
```bash
pkill -9 -f qemu-system-xtensa
pkill -9 -f pytest
```

### `qemu_flash.bin` is a writable MTD!

The firmware writes to the NVS partition and those changes **persist in the
file** across QEMU runs. Tests that change network settings (e.g. `eth_dhcpc=false`
plus a static IP) leave the flash in a state where the next boot makes the
server unreachable on the QEMU network (`hostfwd` only NATs to the
DHCP-assigned 10.0.2.15). Before each test run **always** regenerate the image:
```bash
rm -f build/qemu_flash.bin && make qemu-create-flash-image
```

## Shell command style

Claude Code's circuit breakers fire even in bypass mode on "dangerous-looking"
commands. To avoid getting stuck on confirmation prompts, follow these rules:

### Don't chain commands with `;` or `&&` on one line
Each command is a separate Bash call. Exception: trivial `cd dir && cmd` and etc
If there are more than two steps, write a script and run it.

### No `ps … | grep … | awk … | xargs kill`
To kill processes by name, use:
```bash
pkill -f '<stable pattern>'
```
`kill -9` only when `pkill` (SIGTERM) already failed, and only against a
specific PID you just obtained and displayed in the output. Never kill
processes based on `ps | grep` results without explicitly verifying the PID.

### Destructive operations — one per call
- `rm` with a glob (`rm -f dir/*`) — separate command, no neighbors on the line.
- Truncating redirect (`> file`) — separate command.
- `rm -rf` on paths with variables (`$VAR`, `~`, `$HOME/…`) — forbidden.
  First `echo` the path, confirm it expanded correctly, then delete.
- Never `rm -rf /…` or `rm -rf ~` under any framing.

### Background processes
`nohup … &`, `disown`, `setsid` — separate command. Do not silence stderr
(`2>/dev/null` is forbidden when launching workers — the log is needed for
diagnostics). After launch, print the PID and verify with `ps -p $PID` as
a separate call.

### Long sequences — use a script
If you need to: stop old → clean up → start new — that's three steps,
three calls. Or put them in `scripts/<task>.sh`, commit it, and invoke the
script. Easier to review, and the circuit breakers stay quiet.

### What NOT to do (antipatterns from real interruptions)
```bash
# ❌ everything on one line, kill via grep, glob rm, truncate, nohup, hidden stderr
cd /x; ps -ef|grep foo|grep -v grep|awk '{print $2}'|xargs -r kill -9; sleep 2
> /x/out.txt; rm -f /x/fails/* 2>/dev/null
nohup /x/run.sh > /x/log 2>&1 &
```
Split this into 4–5 separate calls with clearly named steps.