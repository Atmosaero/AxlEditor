"""Private transport for the editor automation tool; not a gameplay runtime."""
import ast
import json
import sys
import traceback
import types
import inspect
import threading

sys.stdout.reconfigure(encoding="utf-8")
sys.stdin.reconfigure(encoding="utf-8")
_out, _in = sys.stdout, sys.stdin
_sequence = 0
_run = 0
_execution_thread = threading.get_ident()
_output_lock = threading.Lock()


def _send(message):
    text = json.dumps(message, ensure_ascii=True, allow_nan=False) + "\n"
    with _output_lock:
        _out.write(text)
        _out.flush()


class Object:
    """Session-scoped editor object ID, not an Entity or a live world copy."""
    __slots__ = ("id", "generation")

    def __init__(self, object_id, generation):
        self.id, self.generation = int(object_id), int(generation)

    def __repr__(self):
        return f"Object({self.id}, generation={self.generation})"

    def __eq__(self, other):
        return isinstance(other, Object) and (self.id, self.generation) == (other.id, other.generation)

    def __hash__(self):
        return hash((self.id, self.generation))


def _encode(value):
    if isinstance(value, Object):
        return {"$object": str(value.id), "generation": str(value.generation)}
    if isinstance(value, int) and not isinstance(value, bool) and abs(value) > 2**53 - 1:
        return {"$integer": str(value)}
    if isinstance(value, (list, tuple)):
        return [_encode(item) for item in value]
    if isinstance(value, dict):
        return {key: _encode(item) for key, item in value.items()}
    return value


def _decode(value):
    if isinstance(value, dict):
        if "$object" in value:
            return Object(value["$object"], value["generation"])
        return {key: _decode(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_decode(item) for item in value]
    return value


class EditorError(RuntimeError):
    pass


def _call(name, *args, **kwargs):
    global _sequence
    if threading.get_ident() != _execution_thread:
        raise EditorError("Editor API is available on the console execution thread")
    _sequence += 1
    request = _sequence
    _send({"type": "call", "run": _run, "id": request, "name": name,
           "args": _encode(args), "kwargs": _encode(kwargs)})
    reply = json.loads(_in.readline())
    if reply.get("type") != "reply" or reply.get("id") != request:
        raise EditorError("Editor connection interrupted")
    if not reply.get("ok"):
        raise EditorError(reply.get("error", "Editor command failed"))
    return _decode(reply.get("value"))


class _Output:
    encoding = "utf-8"
    errors = "strict"
    def __init__(self, stream):
        self.stream = stream

    def write(self, text):
        text = str(text)
        for start in range(0, len(text), 4096):
            _send({"type": "output", "stream": self.stream, "text": text[start:start + 4096]})
        return len(text)

    def flush(self):
        pass

    def isatty(self):
        return False


sys.stdout, sys.stderr = _Output("stdout"), _Output("stderr")
# input() is not a second protocol reader and cannot consume editor replies.
import io
sys.stdin = io.StringIO("")
axl = types.ModuleType("axl", "Editor automation. Use axl.commands() to list registered functions.")
axl.Object, axl.EditorError = Object, EditorError
sys.modules["axl"] = axl
_definitions = []
axl.commands = lambda: {item["name"]: item["description"] for item in _definitions}
_globals = {"__name__": "__console__", "axl": axl}


def _configure(definitions):
    global _definitions
    for item in _definitions:
        if hasattr(axl, item["name"]):
            delattr(axl, item["name"])
    _definitions = definitions
    for item in definitions:
        def make_command(name):
            def command(*args, **kwargs):
                return _call(name, *args, **kwargs)
            return command
        command = make_command(item["name"])
        command.__name__ = item["name"]
        command.__module__ = "axl"
        command.__doc__ = item["description"]
        command.__signature__ = inspect.Signature([
            inspect.Parameter(name, inspect.Parameter.POSITIONAL_OR_KEYWORD,
                              default=item["defaults"].get(name, inspect.Parameter.empty))
            for name in item["parameters"]])
        setattr(axl, item["name"], command)


_send({"type": "ready", "version": sys.version.split()[0]})
for line in _in:
    message = json.loads(line)
    if message.get("type") != "execute":
        continue
    _run = message["run"]
    _configure(message["commands"])
    ok = True
    try:
        filename = message.get("filename", "<Axl Python Console>")
        _globals["__file__"] = filename
        tree = ast.parse(message["source"], filename, "exec")
        expression = tree.body.pop() if tree.body and isinstance(tree.body[-1], ast.Expr) else None
        exec(compile(tree, filename, "exec"), _globals)
        if expression:
            result = eval(compile(ast.Expression(expression.value), filename, "eval"), _globals)
            if result is not None:
                print(repr(result))
    except BaseException:
        ok = False
        traceback.print_exc()
    _send({"type": "done", "run": _run, "ok": ok})
