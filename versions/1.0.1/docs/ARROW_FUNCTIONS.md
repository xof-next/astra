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

## Asynchronous functions and arrow functions

```astra
async fn fetchStatus(url) {
    const response = await launch.get(url);
    return response.status;
}

const fetchJson = async (url) => {
    const response = await launch.get(url);
    return json.parse(response.body);
};

pub async fn main() {
    const first = fetchStatus("https://example.com");
    console.log(type(first));
    console.log(await first);
    console.log(await fetchJson("https://example.com/data"));
}
```

Calling an `async fn` or async arrow returns a promise value immediately. The function body runs on the bounded native task executor, which permits up to 16 active tasks at once. `await` waits for that value and unwraps nested asynchronous results. Rejected tasks raise a runtime error at the `await` expression and can be handled with `try`/`catch`. HTTP, DNS, and TCP operations also return promise values. `sync fn` and `pub sync fn` remain supported for compatibility; they execute on the calling thread, while awaited native operations still run through the worker executor. Ordinary functions and top-level statements cannot use `await`.

Astra uses a bounded worker executor. It does not provide a general event-loop scheduler. Promise chaining methods such as `.then()` are not available. Shared mutable arrays and objects should not be mutated concurrently by separate asynchronous tasks.

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

The normal `fn` and arrow forms cannot contain `await`. An `async fn` or async arrow returns a Future; callers may return that Future without awaiting it, or await it from an allowed `sync fn` / `async fn` context.
