import pathlib
import re
import subprocess
import sys
import tempfile

ASTRA = pathlib.Path(sys.argv[1]).resolve()


def write(root, relative, source):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")
    return path


def run(path, expected, code=None):
    result = subprocess.run([str(ASTRA), "run", str(path)], text=True, capture_output=True, timeout=20)
    if expected and result.returncode != 0:
        raise RuntimeError(f"expected success for {path.name}:\n{result.stdout}{result.stderr}")
    if not expected and result.returncode == 0:
        raise RuntimeError(f"expected failure for {path.name}, but execution succeeded")
    if code and f"error[{code}]" not in result.stderr:
        raise RuntimeError(f"expected {code} for {path.name}:\n{result.stdout}{result.stderr}")
    if not expected and f"{path}:" not in result.stderr:
        raise RuntimeError(f"diagnostic omitted source path for {path.name}:\n{result.stderr}")
    if not expected and not re.search(r"--> .+:\d+:\d+", result.stderr):
        raise RuntimeError(f"diagnostic omitted line/column for {path.name}:\n{result.stderr}")
    return result


with tempfile.TemporaryDirectory(prefix="astra-language-regressions-") as temporary:
    root = pathlib.Path(temporary)
    core = write(root, "core.astra", r'''# top-level comment
let uninitialized;
uninitialized = "ready"; # trailing comment
const rootValue = "global";
fn readRoot() { return rootValue; }
fn shadowRoot() { const rootValue = "local"; return rootValue; }
fn makeCounter() {
    let count = 0;
    const initial = next();
    fn next() { count++; return count; }
    assert(initial == 1);
    return next;
}
fn destructure({ data, { options } }) { return data + options; }
fn nested({ payload: { count } }) { return count; }
fn earlyCall() { return later(); }
fn later() { return 41; }
const hoistedResult = calculate();
fn calculate() { return 100; }
async fn loadData() { return 42; }
async fn nestedAsync() { return await loadData() + 1; }
async fn nestedFuture() { return loadData(); }
async fn deepAsync(depth) { if (depth == 0) return 0; return await deepAsync(depth - 1) + 1; }
async fn failingFuture() { throw "future failed"; }
sync fn resolveNested() { const result = await nestedAsync(); return result; }
sync fn resolveNestedFuture() { return await nestedFuture(); }
sync fn resolveDeepAsync() { return await deepAsync(32); }
sync fn catchFutureFailure() { try { return await failingFuture(); } catch (error) { return error.message; } }
sync fn resolvedType() { return type(await loadData()); }
fn returnFuture() { return loadData(); }
fn returnString() { return "text"; }
fn returnNumber() { return 12; }
fn returnBoolean() { return true; }
fn returnObject() { return { value: 1 }; }
fn returnArray() { return [1, 2]; }
fn returnNull() { return null; }
fn returnUndefined() { return undefined; }
pub sync fn main() {
    assert(uninitialized == "ready");
    assert(readRoot() == "global");
    assert(shadowRoot() == "local");
    let scoped = "outer";
    { let scoped = "inner"; assert(scoped == "inner"); }
    assert(scoped == "outer");
    assert(earlyCall() == 41);
    assert(hoistedResult == 100);
    const next = makeCounter();
    assert(next() == 2 && next() == 3);
    let flexible = "text";
    flexible = 6;
    assert(type(flexible) == "int");
    assert(destructure({ data: "a", options: "b" }) == "ab");
    assert(nested({ payload: { count: 9 } }) == 9);
    assert(type({}.missing) == "undefined");
    assert(type(uninitialized) == "string");
    assert(type(null) == "null");
    assert(type(undefined) == "undefined");
    let user = null;
    assert(type(user?.profile?.url) == "undefined");
    let absent = undefined;
    assert(type(absent?.profile?.url) == "undefined");
    assert(await 7 == 7);
    let result = 0;
    if (false) result = 1; else if (true) result = 2; else result = 3;
    assert(result == 2);
    if (true) result = 4; else if (true) result = 5; else result = 6;
    assert(result == 4);
    assert(type(returnString()) == "string");
    assert(type(returnNumber()) == "int");
    assert(type(returnBoolean()) == "bool");
    assert(type(returnObject()) == "object");
    assert(type(returnArray()) == "array");
    assert(type(returnNull()) == "null");
    assert(type(returnUndefined()) == "undefined");
    assert(type(returnFuture()) == "promise");
    assert(resolveNested() == 43);
    assert(resolveNestedFuture() == 42);
    assert(resolveDeepAsync() == 32);
    assert(catchFutureFailure() == "future failed");
    assert(resolvedType() == "int");
    let syncArrow = sync () => { return await loadData(); };
    assert(syncArrow() == 42);
    let asyncArrow = async () => await loadData();
    assert(await asyncArrow() == 42);
    import "std:validate" as validate;
    assert(validate.isEmpty("") == true);
    assert(validate.isEmpty("  \t\n") == true);
    assert(validate.isEmpty("x") == false);
    assert(validate.isEmpty(null) == true);
    assert(validate.isEmpty(undefined) == true);
    assert(validate.isEmpty([]) == true);
    assert(validate.isEmpty({}) == true);
    console.log("language regression checks passed");
}
''')
    run(core, True)

    write(root, "modules/fetch.astra", r'''let counter = 1;
pub counter;
pub fn get() { return counter; }
pub fn update(value) { counter = value; return counter; }
pub fn fetch(value) { return value + 1; }
pub async fn asyncFetch(value) { return value + 2; }
let mapValue = value => value * 2;
pub mapValue;
let pending;
pub pending;
pub { alias: get, default: fetch };
''')
    write(root, "modules/fallback.astra", "pub fn fallback(value) { return value + 3; }\n")
    write(root, "modules/nested/index.astra", "pub const nestedValue = 7;\n")
    write(root, "modules/group/index.astra", "pub const groupValue = 13;\n")
    write(root, "modules/parent/index.astra", "pub const parentValue = 11;\n")
    module_test = root / "modules/module-test.astra"
    module_test.parent.mkdir(parents=True, exist_ok=True)
    module_test.write_text(r'''let counter;
import { get, update, alias, counter, pending, asyncFetch, mapValue } as "./fetch";
import * as "./fetch";
import nestedValue as "./nested";
import * as "./nested";
pub sync fn main() {
    assert(get() == 1);
    assert(counter == 1);
    update(4);
    assert(counter == 4);
    assert(alias() == 4);
    assert(fetch.fetch(4) == 5);
    assert(fetch.counter == 4);
    assert(nestedValue == 7);
    assert(nested.nestedValue == 7);
    assert(mapValue(6) == 12);
    assert(type(pending) == "undefined");
    assert(await asyncFetch(3) == 5);
}
''', encoding="utf-8")
    run(module_test, True)

    all_test = write(root, "modules/all-test.astra", r'''import as "./fetch";
pub fn main() {
    assert(get() == 1);
    update(8);
    assert(counter == 8);
    assert(alias() == 8);
}
''')
    run(all_test, True)

    default_test = write(root, "modules/default-test.astra", r'''import fetch as "./fetch";
pub fn main() { assert(fetch(40) == 41); }
''')
    run(default_test, True)

    named_default_fallback = write(root, "modules/default-fallback.astra", 'import fallback as "./fallback";\npub fn main() { assert(fallback(40) == 43); }\n')
    run(named_default_fallback, True)

    explicit_extension = write(root, "modules/explicit-extension.astra", "import { get } as \"./fetch.astra\";\npub fn main() { assert(get() == 1); }\n")
    run(explicit_extension, True)

    parent_path = write(root, "modules/child/parent-path.astra", "import parentValue as \"../parent\";\npub fn main() { assert(parentValue == 11); }\n")
    run(parent_path, True)

    current_folder_index = write(root, "modules/group/entry.astra", "import groupValue as \"./\";\npub fn main() { assert(groupValue == 13); }\n")
    run(current_folder_index, True)

    write(root, "modules/cycle/a.astra", r'''import { b } as "./b";
let shared = "ready";
pub shared;
pub fn a() { return "a"; }
pub fn callB() { return b(); }
pub fn readShared() { return shared; }
''')
    write(root, "modules/cycle/b.astra", r'''import { a, shared } as "./a";
pub fn b() { return a() + shared; }
''')
    circular = write(root, "modules/cycle/main.astra", r'''import { callB, readShared } as "./a";
pub fn main() {
    assert(callB() == "aready");
    assert(readShared() == "ready");
}
''')
    run(circular, True)

    write(root, "modules/no-exports.astra", "let privateValue = 1;\n")
    write(root, "modules/failing.astra", 'throw "module initialization failed";\n')
    run(write(root, "modules/import-empty.astra", 'import as "./no-exports";\npub fn main() {}\n'), False, "E3002")
    run(write(root, "modules/import-missing.astra", 'import { absent } as "./fetch";\npub fn main() {}\n'), False, "E3002")
    run(write(root, "modules/import-file-missing.astra", 'import as "./missing";\npub fn main() {}\n'), False, "E3001")
    run(write(root, "modules/import-const-conflict.astra", 'const get = 1;\nimport { get } as "./fetch";\npub fn main() {}\n'), False, "E3004")
    run(write(root, "modules/import-initialized-let-conflict.astra", 'let get = 1;\nimport { get } as "./fetch";\npub fn main() {}\n'), False, "E3004")
    run(write(root, "modules/import-reassign.astra", 'import { get } as "./fetch";\npub fn main() { get = 1; }\n'), False, "E2002")
    failed_again = write(root, "modules/failing-again.astra", 'pub fn main() { try { import as "./failing"; } catch (error) {} import as "./failing"; }\n')
    run(failed_again, False, "E3006")

    negatives = [
        ("block-leak", "pub fn main() { { let inside = 1; } print(inside); }\n", "E2001"),
        ("function-leak", "pub fn main() { fn local() { return 1; } }\nlocal();\n", "E2001"),
        ("const-write", "const fixed = 1;\nfixed = 2;\npub fn main() {}\n", "E2002"),
        ("unknown-write", "unknown = 2;\npub fn main() {}\n", "E2001"),
        ("null-property", "pub fn main() { let value = null; value.name; }\n", "E6003"),
        ("undefined-property", "pub fn main() { let value = undefined; value.name; }\n", "E6003"),
        ("return-outside-function", "return 1;\npub fn main() {}\n", "E5003"),
        ("await-top", "let value = await Number(1);\npub fn main() {}\n", "E8002"),
        ("await-normal", "fn bad() { return await Number(1); }\npub fn main() { bad(); }\n", "E8002"),
        ("bad-destructure-type", "fn take({ data }) { return data; }\npub fn main() { take(1); }\n", "E5003"),
        ("bad-destructure-property", "fn take({ data }) { return data; }\npub fn main() { take({}); }\n", "E5003"),
        ("bad-arg-count", "fn take(a, b) { return a; }\npub fn main() { take(1); }\n", "E5001"),
        ("duplicate-param", "fn take(a, a) { return a; }\npub fn main() {}\n", "E5002"),
        ("duplicate-binding", "let value = 1;\nlet value = 2;\npub fn main() {}\n", "E2003"),
        ("duplicate-function", "fn same() {}\nfn same() {}\npub fn main() {}\n", "E2003"),
        ("function-variable-conflict", "let same = 1;\nfn same() {}\npub fn main() {}\n", "E2003"),
        ("const-function-conflict", "fn same() {}\nconst same = 1;\npub fn main() {}\n", "E2003"),
        ("invalid-parameter-pattern", "fn bad(1) {}\npub fn main() {}\n", "E5002"),
        ("duplicate-arrow-parameter", "const bad = (value, value) => value;\npub fn main() {}\n", "E5002"),
        ("const-without-initializer", "const value;\npub fn main() {}\n", "E2004"),
        ("duplicate-export", "let value = 1;\npub { same: value, same: value };\npub fn main() {}\n", "E3005"),
        ("pub-in-function", "fn bad() { let value = 1; pub value; }\npub fn main() { bad(); }\n", "E3004"),
        ("pub-undeclared", "pub absent;\npub fn main() {}\n", "E3002"),
        ("pub-block-local", "{ let privateValue = 1; pub privateValue; }\npub fn main() {}\n", "E3002"),
        ("syntax-unclosed", "pub fn main() {\n", "E1001"),
    ]
    for name, source, code in negatives:
        run(write(root, f"negative/{name}.astra", source), False, code)

    diagnostic = run(write(root, "negative/unterminated-string.astra", '# comment\nconst value = "unterminated\n'), False, "E1002")
    if f"{root / 'negative/unterminated-string.astra'}:2:15" not in diagnostic.stderr:
        raise RuntimeError(f"lexer diagnostic has incorrect line/column after a # comment:\n{diagnostic.stderr}")
    run(write(root, "negative/unterminated-block-comment.astra", "/* open comment\n"), False, "E1002")

    print("Astra language and runtime regressions passed")
