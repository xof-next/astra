# Astra Script v1.0.0 — Native Runtime Preview

Astra is a personal programming-language experiment. Its syntax takes JavaScript as the main reference and uses C-style program structure. This source distribution contains a native C++17 interpreter preview with a lexer, source checker, runtime, arbitrary-precision integers, and 37 built-in modules with 172 registered exports.

> Scope: this build supports an interpreted subset of `.astra`. It does not emit native machine code and is not a production-stable compiler.

## Development source layout

```text
versions/1.0.0/
├── Astra.toml                 # Compiler project configuration
├── Astra.lock                 # Development lock placeholder
├── config/                    # Compiler and diagnostic settings
├── include/astra/             # C++ interfaces
├── src/                       # C++ compiler, CLI, runtime and updater
├── stdlib/modules.toml        # Built-in module inventory
├── assets/                    # Astra-specific assets
├── examples/                  # Runnable .astra examples
├── tests/                     # Runtime and integration tests
└── docs/                      # CLI, syntax and module documentation
```

The tree above is the language developer's source repository. It is not the layout that Astra application users need in each project.

## Build requirements for language development

- CMake 3.16+
- C++17 compiler
- libcurl with a TLS backend
- zlib
- Python 3 is optional and used by local integration tests

```sh
cd versions/1.0.0
./scripts/build.sh
./build/astra --version
./build/astra modules
ctest --test-dir build --output-on-failure
```

## Minimal application project

`astra init --project` creates a minimal application project:

```text
my-project/
├── Astra.toml
├── main.astra
├── test.astra
└── lib/
    └── test.astra
```

Example `Astra.toml`:

```toml
[project]
name = "my-project"
version = "1.0.0"
entry = "main.astra"
```

Example `main.astra`:

```astra
pub sync fn main() {
    let response = await launch.get("https://example.com");
    console.log(response.status);
    console.log(response.body);
}
```

## CLI

```sh
astra                         # run project entry
astra test                    # task alias from [devDependencies]
astra test.astra              # run a source file directly
astra run lib/test            # .astra extension is optional
astra check file.astra
astra modules
astra init                    # install this build globally
astra init --project [dir]    # create an app project
astra install [version]       # install pack.json new or selected version
astra update                  # install and activate pack.json new
astra versions                # list versions from pack.json
astra use 1.0.0               # activate an installed version
```

Project task example:

```toml
[devDependencies]
test = "astra lib/test.astra"
```

The CLI never launches a shell for task aliases; task values are restricted to Astra source invocations. See [docs/CLI.md](docs/CLI.md) for global install and task resolution behavior.

`astra install` reads the `new` version from the repository-root `pack.json`, or accepts an explicit version. It fetches the matching GitHub Release tag, checks the manifest's platform asset name, verifies SHA-256, stores the binary under the user's version directory, and activates it. `astra use` activates an already-installed version without deleting the others. The repository and release assets must be published before online installation is available. See [docs/CLI.md](docs/CLI.md) for the exact release/checksum contract.

The global runtime is a platform executable that contains the interpreter and standard modules. User project folders do not need C++ sources, CMake, `include/`, `src/`, `stdlib/`, `astra_modules/`, or a copy of the runtime. The only project files needed to start are `.astra` source and an optional `Astra.toml` configuration. Some native OS libraries may still be required by a given platform build unless they are linked statically.

## Global standard module namespaces

These core namespaces are available without `import`:

- `launch`
- `server`
- `dns`
- `tcp`
- `events`
- `json`
- `math`
- `crypto`
- `fs`
- `path`
- `tls`

They can also be imported under an alias. Local `let`, `const`, `fn`, or `function` declarations can shadow a built-in name in their lexical scope without mutating the global runtime.

## HTTP server and networking

```astra
import "std:server" as http;

const app = http.create();
app.get("/", (req, res) => res.send("Astra is running"));
app.get("/users/:id", (req, res) => res.json({ id: req.params.id }));
app.post("/users", (req, res) => {
    res.status(201);
    res.json({ created: true, body: req.body });
});
app.listen(3000, { host: "127.1.0.0", maxRequestBytes: 1048576 });
```

The server supports multiple route registrations and `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, plus `route()`/`all()`. Route parameters are available in `req.params`; request headers, query values and raw body are exposed on `req`. Responses support `status`, `setHeader`, `send`, `end`, and `json`. Every response includes a non-overridable `Server: Astra` header. The preview server is sequential, handles one request per connection, and does not support chunked request bodies or TLS server sockets; do not expose it directly as a production internet-facing server.

`dns.lookup/resolve/reverse/isIP` provide DNS and address helpers. `tcp.connect(host, port, { timeoutMs })` creates a blocking TCP client with `write`, `read`, `close`, and `isOpen` methods. TCP methods operate on byte strings and do not add TLS; use the HTTPS-capable `launch` client for verified HTTP(S) requests.

## Function syntax

```astra
let add = (a, b) => a + b;
const multiply = (a, b) => {
    return a * b;
};
let increment = |value| value + 1;

pub sync fn main() {
    let result = await launch("https://example.com"); // launch(url) defaults to GET
    console.log(result.status);
}

function launch(url) {
    return `custom:${url}`;
}
```

The `|value| expression` form is a synchronous Rust-style closure. `sync fn` and `pub sync fn` mark functions where `await` syntax is permitted; `await` must not appear at top level or inside an ordinary `fn`. In this interpreter preview `await` currently unwraps an already-completed value: native HTTP and TCP calls remain blocking and there is no promise scheduler or event loop yet. `async fn` is intentionally not accepted. `const` controls reassignment of the variable, not a different arrow-function type. More examples are in [docs/ARROW_FUNCTIONS.md](docs/ARROW_FUNCTIONS.md).

## Runtime rules

- `int` is signed 64-bit; overflow raises `E6002`.
- `float` uses IEEE-754 double precision.
- `bigint` uses arbitrary-length decimal integer arithmetic.
- `undefined` and `null` are distinct.
- Unknown module members raise `E3002`; Astra does not invent APIs.
- Normal console output does not use compiler diagnostic IDs.

## Additional web/backend modules

```astra
import "std:html" as html;
import "std:mime" as mime;
import "std:cookie" as cookie;
import "std:collections" as collections;

pub fn main() {
    let safeText = html.escape("<Astra & XOF>");
    let contentType = mime.type("public/index.html");
    let session = cookie.serialize("sid", "abc 123", {
        httpOnly: true,
        secure: true,
        sameSite: "Lax",
        path: "/"
    });
    console.log(safeText, contentType, session);
    console.log(collections.chunk([1, 2, 3, 4], 2));
}
```

`std:html` provides HTML entity escaping/unescaping, `std:mime` maps 135 common file extensions and content types, and `std:cookie` parses Cookie headers and serializes Set-Cookie values. Cookie serialization validates names and attributes; `SameSite=None` requires `Secure`. `std:collections` adds `unique`, `chunk`, `flatten`, `zip`, and `groupBy` for array/object data processing.

See [CHANGELOG.md](CHANGELOG.md) for the latest additions and [docs/LIMITATIONS.md](docs/LIMITATIONS.md) for runtime safety bounds and unsupported features.

## Known limitations

This remains an interpreter preview. External package installation, local source-module loading, asynchronous task scheduling, native machine-code generation, complete Unicode semantics, production-grade sandboxing and a complete formal language specification are not included.
