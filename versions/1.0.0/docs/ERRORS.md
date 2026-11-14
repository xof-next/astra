# Diagnostics

Compiler/runtime diagnostics use stable error code families and source locations:

- `E1xxx`: lexical and syntax errors
- `E2xxx`: variables and scope
- `E3xxx`: modules and imports
- `E4xxx`: types and conversions
- `E5xxx`: functions and arguments
- `E6xxx`: runtime operations
- `E7xxx`: memory/resources (reserved)
- `E8xxx`: async/concurrency (reserved)
- `E9xxx`: project/compiler/CLI
- `Wxxxx`: warnings (reserved in this preview)

Example:

```text
error[E3002]: Module `std:crypto` does not export member `tes`
  --> src/main.astra:5:12
   |
 5 | crypto.tes();
   |            ^
   = help: Check the module's exported members; Astra does not invent APIs.
```

Application output is separate. `console.log()` and `console.info()` produce plain text; they do not receive diagnostic codes. `console.warn()` and `console.error()` add an ordinary label but are not compiler diagnostics.
