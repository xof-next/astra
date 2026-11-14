# Diagnostics

Compiler/runtime diagnostics use stable error code families and source locations:

- `E1xxx`: lexical and syntax errors
- `E2xxx`: variables, assignment, and scope
- `E3xxx`: modules, imports, and exports
- `E4xxx`: types and conversions
- `E5xxx`: functions and arguments
- `E6xxx`: runtime operations
- `E7xxx`: memory/resources (reserved)
- `E8001`: reserved async declaration diagnostic
- `E8002`: `await` used outside an allowed `sync fn` or `async fn` / arrow context
- `E8003`: awaited asynchronous operation failed
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


Additional implemented diagnostics include `E2001` for an undefined identifier, `E2002` for an immutable assignment, `E2003` for a duplicate declaration, `E2004` for a `const` without an initializer, `E3001` for an unresolved module path, `E3002` for a missing export, `E3004` for invalid publication/import binding conflicts, `E3005` for duplicate exports, `E3006` for a module that previously failed, `E5001`–`E5003` for function calls and parameter patterns, and `E6003` for property access through `null` or `undefined`.
