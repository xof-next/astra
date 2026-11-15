# Functions and closures

Astra supports named functions (`fn` and the `function` alias), arrow functions, and synchronous pipe-parameter closures.

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

## Synchronous functions and `await`

Only `sync fn` declarations and `sync` arrows may use `await`. The function itself executes on the calling thread; native asynchronous operations return Future values and run through the bounded runtime task executor.

```astra
sync fn fetchStatus(url) {
    const response = await launch.get(url);
    return response.status;
}

const fetchJson = sync (url) => {
    const response = await launch.get(url);
    return json.parse(response.body);
};

pub sync fn main() {
    const pending = launch.get("https://example.com");
    console.log(type(pending));
    console.log(await pending);
    console.log(await fetchJson("https://example.com/data"));
}
```

A normal `fn` or ordinary arrow cannot use `await`. Calling a native asynchronous operation from a normal function is allowed when its Future is returned or passed elsewhere; resolving it requires a `sync` function or `sync` arrow. Language-level `async fn` and async arrows are rejected with `E8001`. `await` on a non-Future value returns that value unchanged. Nested Future results are unwrapped, and rejected native operations are raised at the awaiting expression so they can be handled with `try`/`catch`.

The bounded task executor allows up to 16 active native operations at once. Additional work runs inline when the limit is reached. Astra does not provide a general event-loop scheduler or promise chaining methods such as `.then()`.

## Pipe-parameter synchronous closure

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

## Explicit synchronous arrow function

An arrow function may be marked with `sync` to permit `await` while executing on the calling thread:

```astra
let loadData = sync () => {
    return await fetchData();
};
```

The normal `fn` and arrow forms cannot contain `await`. Only `sync fn` and `sync` arrows can resolve native Future values.
