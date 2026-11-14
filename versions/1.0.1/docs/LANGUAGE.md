# Astra 1.0.1 language and runtime reference

This document describes the language behavior covered by the 1.0.1 interpreter and regression suite. It does not describe unsupported syntax or promise behavior that the current runtime does not implement.

## A runnable starting point

Save this as `main.astra`:

```astra
let greeting;
greeting = "hello";
const answer = 42;

fn message(value) {
    return greeting + " " + value;
}

pub fn main() {
    console.log(message("Astra"));
    console.log(answer);
}
```

Run it with:

```sh
astra run main.astra
```

A project entry point may expose `pub fn main()`. The CLI invokes `main()` after executing the module body. The `main` function must not require parameters.

## Variables and constants

`let` creates a mutable binding. An initializer is optional; without one, the value is `undefined`.

```astra
let data;
data = "hello";
data = 10;
```

`const` requires an initializer and cannot be rebound:

```astra
const value = 10;
```

A declaration with a name that already exists in the same lexical scope fails with `E2003`. Assigning to a missing name fails with `E2001`; assigning to a constant or imported binding fails with `E2002`. A `const` without an initializer fails with `E2004`.

## Lexical and block scope

Each `{ ... }` block creates a child lexical environment. A block can read names from its parent, and a declaration inside the block does not become visible outside it. A child scope may shadow a parent binding without changing the parent binding.

```astra
const value = "outer";

fn localValue() {
    const value = "inner";
    return value;
}

fn outerValue() {
    return value;
}

pub fn main() {
    assert(localValue() == "inner");
    assert(outerValue() == "outer");
    {
        let temporary = 1;
        assert(temporary == 1);
    }
}
```

Functions capture the lexical environment where they are declared. A closure reads the current value of a captured mutable binding:

```astra
fn makeCounter() {
    let count = 0;
    fn next() {
        count++;
        return count;
    }
    return next;
}
```

Function-local bindings are not visible to the caller after the function returns.

## Function declarations and hoisting

Named declarations use `fn` or `function`. Function declarations are hoisted within their lexical scope, so a function can be called before its declaration. `let` and `const` declarations are not hoisted.

```astra
const result = calculate();

fn calculate() {
    return 100;
}
```

Two function declarations with the same name in one scope fail with `E2003`. A function declaration conflicts with a variable of the same name in that scope. A function declared in a nested block is local to that block.

## Parameters and destructuring

Functions accept comma-separated parameters, and the call must supply exactly the declared number of arguments:

```astra
fn combine(data, options) {
    return data + options;
}
```

An object pattern binds named properties from an object argument:

```astra
fn read({ data, options }) {
    return data;
}
```

Nested object properties use a property key followed by a nested pattern:

```astra
fn readMode({ data, options: { mode } }) {
    return data + mode;
}
```

The compact nested group in `fn read({ data, { options } })` is defined as a flattened group: both `data` and `options` are read from the top-level argument object. Use the keyed form `options: { ... }` when the nested pattern should be read from a nested property. This removes ambiguity about which object supplies each property.

Destructuring requires an object. A non-object argument, a missing required property, a malformed pattern, a duplicate parameter name, or an incorrect argument count produces a function diagnostic (`E5001`, `E5002`, or `E5003`). Destructured properties are required; there are no default values for missing properties.

Arrow functions support identifier parameters and expression or block bodies:

```astra
const add = (left, right) => left + right;
const double = value => value * 2;
```

## Return and control flow

`return` preserves the returned Astra value, including strings, numbers, booleans, objects, arrays, `null`, `undefined`, and Future values. `return` outside a function fails with `E5003`.

`if`, `else if`, and `else` execute only the selected branch. A branch may be a block or a permitted single statement:

```astra
if (true) result = 1;
else if (ready) result = 2;
else result = 0;
```

The interpreter also supports `while`, `for`, `for ... of`, and `try`/`catch` within the implemented grammar. Loop execution has a one-million-iteration guard.

## Operators and assignment

The expression parser supports arithmetic (`+`, `-`, `*`, `/`, `%`, `**`), comparisons (`<`, `>`, `<=`, `>=`), equality (`==`, `!=`, `===`, `!==`), logical (`&&`, `||`), nullish coalescing (`??`), unary `!`, unary `+` and `-`, and postfix increment/decrement (`++`, `--`).

Variable assignment supports `=`, `+=`, `-=`, `*=`, and `/=`. Indexed and property assignment support `=`, `+=`, and `-=`. Reassignment still follows the binding's mutability rules. Assignment to an undeclared name is an error. Logical operators currently evaluate their right-hand expression; they should not be used to guard side effects or operations that may fail.

## Type inspection

`type(value)` returns the interpreter's internal type name. The tested names are:

| Astra value | `type()` result |
|---|---|
| `undefined` | `undefined` |
| `null` | `null` |
| `true` / `false` | `bool` |
| integer | `int` |
| floating-point number | `float` |
| BigInt | `bigint` |
| string | `string` |
| array | `array` |
| object | `object` |
| function | `function` |
| Future | `promise` |

`type()` reports a value's type; it does not validate string contents. For input checks use the relevant validation function, such as `std:validate.isEmpty(value)`. In 1.0.1, `isEmpty` treats an empty or whitespace-only string as empty; it also treats `null`, `undefined`, and empty arrays/objects as empty.

## `null`, `undefined`, and property access

`null` and `undefined` are distinct values. An uninitialized `let` and a missing object property evaluate to `undefined`. `null` is an explicit value.

Ordinary property access on `null` or `undefined` fails with `E6003`. Reading a property that is not present on an ordinary object returns `undefined`:

```astra
const record = {};
assert(type(record.missing) == "undefined");
```

Optional property access returns `undefined` when its receiver is `null` or `undefined`. Chaining can be repeated:

```astra
let user = null;
assert(type(user?.profile?.url) == "undefined");
```

The implemented optional-chain form is property access with `?.` followed by an identifier property name.

## Module imports

File modules resolve relative to the file containing the import, not the terminal's working directory. The supported forms are:

```astra
import { get, post } as "./main";
import * as "./main";
import as "./main";
import fetch as "./fetch";
import "std:math" as math;
```

- Named import binds the listed exported names.
- Namespace import creates a namespace binding inferred from the file or folder name. For `./main`, the namespace is `main`.
- Import-all binds every public export by its export name.
- Default import reads the export named `default`. For compatibility, when no `default` export exists it looks for a public export with the imported name.
- Built-in modules use the `std:` prefix.

Path resolution rules:

- `./` selects `index.astra` in the importing file's directory.
- `../` selects `index.astra` in the parent directory.
- `./main` tries `main.astra`, then `main/index.astra`.
- `./main.astra` selects that explicit file.

A missing file fails with `E3001`. A missing named export or an import-all from a module without public exports fails with `E3002`. Import bindings cannot be reassigned. An import may replace a same-scope `let` only when that `let` is still `undefined`; it cannot replace a constant or an initialized binding.

## Module exports with `pub`

Only explicitly published bindings are visible to importing modules. `pub` can publish a function or a binding that has already been declared:

```astra
pub fn main() {
    return "hello";
}

let data;
pub data;

fn process() {
    return "result";
}

pub { data: process, process };
```

`pub async fn` and `pub sync fn` are supported. Alias exports use `exportedName: localName`. Exported `let` values are live: an importer observes subsequent assignments to the source binding. Importer bindings remain read-only. Publishing an undeclared binding, publishing from inside a function, or declaring a conflicting export fails. Duplicate exports fail with `E3005`.

Modules are cached with loading, loaded, and failed states. A module already in the loading state can be referenced during a circular import; imports retain references to source environments, so they observe live exported values after initialization. A module that previously failed to load cannot be reused; the loader reports `E3006` on a later attempt.

## Asynchronous functions and `await`

Ordinary `fn` functions and top-level module statements cannot use `await`. `sync fn` is the synchronous execution form that is permitted to wait for asynchronous values:

```astra
sync fn loadAndInspect() {
    const result = await loadData();
    return type(result);
}
```

`sync` arrow functions follow the same rule:

```astra
let getData = sync () => {
    return await loadData();
};
```

The runtime also supports `async fn` and async arrow functions. Calling one returns a Future immediately. Ordinary functions do not become asynchronous just because they call a function that returns a Future; that Future can be returned or explicitly awaited from an allowed function. `await` on a non-Future value returns that value unchanged. Nested Future results are unwrapped up to a bounded depth of 64. Async work runs through an executor with up to 16 active worker tasks; work beyond that bound runs inline. Rejected asynchronous work is reported at the awaiting expression with `E8003`.

## Comments

The `#` character starts a line comment. All text up to the newline is ignored. It works by itself or after a statement:

```astra
# Full-line comment
const value = "hello" # Trailing comment
```

The lexer also retains support for `//` line comments and `/* ... */` block comments. Comment text does not become a parser token; line and column counting continues across line breaks.

## Diagnostics

Diagnostics include an error code and, where available, the source path, line, column, source line, and caret context. Common codes include:

| Code | Meaning |
|---|---|
| `E1001` / `E1002` | Syntax or lexical error |
| `E2001` | Undefined identifier or assignment target |
| `E2002` | Assignment to a constant or imported binding |
| `E2003` | Duplicate declaration |
| `E2004` | `const` without an initializer |
| `E3001` | Invalid or unresolved module path |
| `E3002` | Missing export or invalid public binding |
| `E3004` / `E3005` / `E3006` | Invalid publication, duplicate export, or previously failed module |
| `E4001`–`E4004` | Invalid value/type operation or numeric conversion |
| `E5001`–`E5003` | Invalid function call, parameter pattern, destructuring, or return placement |
| `E6003` | Property access through `null` or `undefined` |
| `E8002` / `E8003` | `await` outside an allowed function or asynchronous operation failure |

`astra check` is a limited static checker; it does not replace executing the program with `astra run`. Runtime behavior is the authoritative check for the interpreter features documented here.

## Verified examples

The repository includes runnable examples in `examples/language_runtime.astra` and `examples/module_runtime/main.astra`. The CTest suite executes both and runs `tests/run_language_regressions.py` for additional scope, module, import, destructuring, async, comment, and diagnostic cases.
