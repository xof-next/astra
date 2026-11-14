# Functions and closures

Astra supports named functions (`fn` and the JavaScript-compatible `function` alias), arrow functions, and Rust-style synchronous closures.

## Normal arrow function

```astra
let add = (a, b) => a + b;
let double = value => value * 2;

console.log(add(2, 3));
console.log(double(4));
```

## Block-bodied arrow function

```astra
const multiply = (a, b) => {
    let result = a * b;
    return result;
};
```

## Rust-style synchronous closure

```astra
let increment = |value| value + 1;
let combine = |a, b| a + b;
let getAnswer = || 42;
```

The `|parameters| expression` form is synchronous. It does not create a thread, task, or asynchronous operation.

## `const` arrow binding

`const` controls whether the variable can be reassigned. It does not create a different function kind:

```astra
const launchRequest = (url) => {
    return launch.get(url);
};
```

Use `let` when the function variable itself must be replaced later. The function value's behavior is otherwise the same.

## Shadowing built-in globals

Core module namespaces are available without imports. A declaration in the current lexical scope shadows that built-in name; it never mutates the runtime's global namespace or other files:

```astra
fn launch(url) {
    return `custom:${url}`;
}

const json = (value) => `custom:${value}`;

console.log(launch("/health"));
console.log(json("ready"));
```

`let`, `const`, and named function declarations all use ordinary lexical scope rules. Assigning to a built-in global without declaring a local binding is rejected as an attempt to reassign a constant. This prevents accidental process-wide mutation.

The following built-in module namespaces are global in this preview: `launch`, `events`, `json`, `math`, `crypto`, `fs`, `path`, and `tls`. They may still be imported under an alias when that is clearer or when a separate local name is desired.
