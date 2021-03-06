import argparse
import contextlib
import itertools
import os
import shutil
import typing

_scriptdir = os.path.dirname(os.path.realpath(__file__))


class Namespace:
    def __init__(self, f):
        object.__setattr__(self, 'attrs', dict())
        object.__setattr__(self, 'name', f.__name__)
        f(self)

    def __getattribute__(self, key):
        attrs = object.__getattribute__(self, 'attrs')
        if key not in attrs:
            raise AttributeError(
                f'No attr {key} for {object.__getattribute__(self, "name")}')
        return attrs[key]

    def __setattr__(self, key, value):
        object.__getattribute__(self, 'attrs')[key] = value

    def __call__(self, x, name=None):
        if name is None:
            name = x.__name__
        setattr(self, name, x)
        return x

    def __repr__(self):
        return f'<namespace {object.__getattribute__(self, "name")}>'


class Multimethod:
    # TODO: support inheritance
    def __init__(self, name, n=1):
        # n = number of arguments whose types
        # we take into account for dispatch
        self.name = name
        self.n = n
        self.table = dict()

    def __repr__(self):
        return f'Multimethod({self.name}, {self.n})'

    def on(self, *types):
        if len(types) != self.n:
            raise TypeError(f'self.n = {self.n}, types = {types}')
        def wrapper(f):
            self.table[types] = f
            return self
        return wrapper

    def find(self, types):
        "Find and return the implementation for the given arg types"
        if types not in self.table:
            mrolist = [t.__mro__ for t in types]
            for basetypes in itertools.product(*mrolist):
                if basetypes in self.table:
                    self.table[types] = self.table[basetypes]
                    break
            else:
                raise KeyError(
                    f'{repr(self.name)} is not defined for types {types}')
        return self.table[types]

    def __call__(self, *args):
        types = tuple(type(arg) for arg in args[:self.n])
        f = self.find(types)
        return f(*args)


class Source(typing.NamedTuple):
    name: str
    filename: str
    data: str

    @classmethod
    def from_name_and_path(cls, name, path):
        with open(path) as f:
            data = f.read()
        return cls(name, path, data)


class Token(typing.NamedTuple):
    source: Source
    i: int
    type: str
    value: object

    def __repr__(self):
        return f'Token({repr(self.type)}, {repr(self.value)})'

    @property
    def lineno(self):
        return self.source.data.count('\n', 0, self.i) + 1

    @property
    def colno(self):
        s = self.source.data
        a = self.i
        while a > 0 and s[a - 1] != '\n':
            a -= 1
        return self.i - a + 1

    @property
    def line(self):
        s = self.source.data
        a = self.i
        while a > 0 and s[a - 1] != '\n':
            a -= 1
        b = self.i
        while b < len(s) and s[b] != '\n':
            b += 1
        return s[a:b]

    def format(self):
        return (
            f'  In {self.source.filename} on line {self.lineno}\n'
            f'  {self.line}\n'
            f'  {" " * (self.colno - 1)}*\n'
        )


class BaseError(Exception):
    def __init__(self, tokens, message):
        super().__init__(f'{message}\n{"".join(t.format() for t in tokens)}')
        self.tokens = tuple(tokens)
        self.message = message


class Error(BaseError):
    pass


class Fubar(BaseError):
    pass


class Promise:
    def __init__(self, callback):
        self._callback = callback
        self._resolved = False
        self._result = None

    def resolve(self):
        if not self._resolved:
            self._result = self._callback()
        return self._result

    def __repr__(self):
        return f'Promise({repr(self.resolve())})'

    @classmethod
    def value(cls, value):
        @Promise
        def promise():
            return value
        return promise


class Node(object):
    def __init__(self, token, *args):
        self.token = token
        for (fname, ftype), arg in zip(type(self).fields, args):
            if not isinstance(arg, ftype):
                raise TypeError('Expected type of %r to be %r, but got %r' % (
                    fname, ftype, arg))
            setattr(self, fname, arg)
        if len(type(self).fields) != len(args):
            raise TypeError('%s expects %s arguments, but got %s' % (
                type(self).__name__, len(type(self).fields), len(args)))

    def __repr__(self):
        return '%s(%s)' % (
            type(self).__name__,
            ', '.join(repr(getattr(self, n)) for n, _ in type(self).fields),
        )

    def foreach(self, f):
        self.map(f)

    def map(self, f):
        nt = type(self)
        return nt(self.token, *[
            Node._map_helper(getattr(self, fieldname))
            for fieldname, _ in nt.fields
        ])

    @classmethod
    def _map_helper(cls, f, value):
        if isinstance(value, (type(None), int, float, bool, str, IR.CType)):
            return value
        if isinstance(value, list):
            return [Node._map_helper(x) for x in value]
        if isinstance(value, tuple):
            return tuple(Node._map_helper(x) for x in value)
        if isinstance(value, set):
            return {Node._map_helper(x) for x in value}
        if isinstance(value, dict):
            return {
                Node._map_helper(k): Node._map_helper(v)
                for k, v in value.items()
            }
        if isinstance(value, Node):
            return f(value)
        raise TypeError(
            f'Unsupported Node.map element type '
            f'{type(value)} ({repr(value)})')


@Namespace
def typeutil(ns):
    class FakeType(object):
        @property
        def __name__(self):
            return repr(self)

    class ListType(FakeType):
        def __init__(self, subtype):
            self.subtype = subtype

        def __getitem__(self, subtype):
            return ListType(subtype)

        def __instancecheck__(self, obj):
            return (
                isinstance(obj, list) and
                all(isinstance(x, self.subtype) for x in obj))

        def __repr__(self):
            return 'List[%s]' % (self.subtype.__name__, )

    List = ListType(object)
    ns(List, 'List')

    class OptionalType(FakeType):
        def __init__(self, subtype):
            self.subtype = subtype

        def __getitem__(self, subtype):
            return OptionalType(subtype)

        def __instancecheck__(self, obj):
            return obj is None or isinstance(obj, self.subtype)

        def __repr__(self):
            return 'Optional[%s]' % (self.subtype.__name__, )

    Optional = OptionalType(object)
    ns(Optional, 'Optional')


@Namespace
def lexer(ns):
    KEYWORDS = {
      'is', 'not', 'null', 'true', 'false', 'new', 'and', 'or', 'in',
      'inline', 'extern', 'class', 'trait', 'final', 'def', 'auto',
      'struct',
      'for', 'if', 'else', 'while', 'break', 'continue', 'return',
      'with', 'from', 'import', 'as', 'try', 'catch', 'finally', 'raise',
      'except', 'case','switch', 'var',
    }
    ns(KEYWORDS, 'KEYWORDS')

    SYMBOLS = tuple(reversed(sorted([
      '\n',
      '||', '&&', '|', '&', '<<', '>>', '~', '...',
      ';', '#', '?', ':', '!', '++', '--',  # '**',
      '.', ',', '!', '@', '^', '&', '+', '-', '/', '%', '*', '.', '=', '==', '<',
      '>', '<=', '>=', '!=', '(', ')', '{', '}', '[', ']',
    ])))

    ESCAPE_MAP = {
      'n': '\n',
      't': '\t',
      'r': '\r',
      '\\': '\\',
      '"': '"',
      "'": "'",
    }

    @ns
    def lex_string(data: str):
        return lex(Source('main', None, data))

    @ns
    def lex(source: Source):
        return list(lex_gen(source))

    def lex_gen(source: Source):
        s = source.data
        i = 0
        is_delim = False

        def skip_spaces_and_comments():
            nonlocal i
            while s.startswith('//', i) or i < len(s) and s[i] in ' \t\r':
                if s.startswith('//', i):
                    while i < len(s) and s[i] != '\n':
                        i += 1
                else:
                    i += 1

        def mt(i, type_, data):
            # make token
            return Token(source, i, type_, data)

        def error(indices, message):
            tokens = [Token(source, i, 'ERR', None) for i in indices]
            return Error(tokens, message)

        while True:
            skip_spaces_and_comments()

            a = i
            last_was_delim = is_delim
            is_delim = False

            if i >= len(s):
                if not last_was_delim:
                    yield mt(i, '\n', 'EOF')
                yield mt(i, 'EOF', 'EOF')
                break

            # raw string literal
            if s.startswith(('r"', "r'"), i):
                i += 1  # 'r'
                q = s[i:i+3] if s.startswith(s[i] * 3, i) else s[i]
                i += len(q)
                while i < len(s) and not s.startswith(q, i):
                    i += 1
                if i >= len(s):
                    raise error([a], 'Unterminated raw string literal')
                i += len(q)
                yield mt(a, 'STRING', s[a+1+len(q):i-len(q)])
                continue

            # normal string literal
            if s.startswith(('"', "'"), i):
                q = s[i:i+3] if s.startswith(s[i] * 3, i) else s[i]
                i += len(q)
                sb = []
                while i < len(s) and not s.startswith(q, i):
                    if s[i] == '\\':
                        i += 1
                        if i >= len(s):
                            raise error([a], 'Expected string escape')
                        if s[i] in ESCAPE_MAP:
                            sb.append(ESCAPE_MAP[s[i]])
                            i += 1
                        else:
                            raise error([i], 'Invalid string escape')
                    else:
                        sb.append(s[i])
                        i += 1
                if i >= len(s):
                    raise error([a], 'Unterminated string literal')
                i += len(q)
                yield mt(a, 'STRING', ''.join(sb))
                continue

            # numbers (int and float)
            if s[i].isdigit():
                while i < len(s) and s[i].isdigit():
                    i += 1
                if i < len(s) and s[i] == '.':
                    i += 1
                    while i < len(s) and s[i].isdigit():
                        i += 1
                    yield mt(a, 'FLOAT', float(s[a:i]))
                else:
                    yield mt(a, 'INT', int(s[a:i]))
                continue

            # escaped names
            if s[i] == '`':
                i += 1
                while i < len(s) and s[i] != '`':
                    i += 1
                if i >= len(s):
                    raise error([a], 'Unterminated escape identifier')
                i += 1
                yield mt(a, 'NAME', s[a+1:i-1])
                continue

            # names and keywords
            if s[i].isalnum() or s[i] == '_':
                while i < len(s) and (s[i].isalnum() or s[i] == '_'):
                    i += 1
                name = s[a:i]
                type_ = name if name in KEYWORDS else 'NAME'
                yield mt(a, type_, name)
                continue

            # symbols
            for symbol in SYMBOLS:
                if s.startswith(symbol, i):
                    i += len(symbol)
                    yield mt(a, symbol, symbol)
                    break
            else:
                # unrecognized token
                raise error([i], 'Unrecognized token')


@Namespace
def IR(ns):

    @ns
    class Expression(Node):
        pass

    @ns
    class Declaration(Node):
        pass

    @ns
    class ImportDeclaration(Declaration):
        fields = (
            ('module_name', str),
        )

    @ns
    class GlobalDeclaration(Declaration):
        fields = (
            ('module_name', str),
            ('short_name', str),
        )

    @ns
    class LocalDeclaration(Declaration):
        fields = (
            ('name', str),
        )

    @ns
    class Import(Node):
        fields = (
            ('module_name', str),
            ('alias', str),
        )

    @ns
    class GlobalVariableDefinition(Node):
        fields = (
            ('module_name', str),
            ('short_name', str),
            ('expression', typeutil.Optional[Expression]),
        )

    @ns
    class Module(Node):
        fields = (
            ('name', str),
            ('imports', typeutil.List[Import]),
            ('definitions', typeutil.List[GlobalVariableDefinition]),
        )

    @ns
    class Param(Node):
        fields = (
            ('name', str),
        )

    @ns
    class Block(Expression):
        fields = (
            ('exprs', typeutil.List[Expression]),
        )

    @ns
    def methodCall(token, owner, name, args):
        return Call(token, GetAttr(token, owner, name), args)

    @ns
    class Call(Expression):
        fields = (
            ('f', Expression),
            ('args', typeutil.List[Expression]),
        )

    @ns
    class GetAttr(Expression):
        fields = (
            ('owner', Expression),
            ('name', str),
        )

    @ns
    class SetAttr(Expression):
        fields = (
            ('owner', Expression),
            ('name', str),
            ('expr', Expression),
        )

    @ns
    class SetLocal(Expression):
        fields = (
            ('name', str),
            ('expression', Expression),
        )

    @ns
    class GetLocal(Expression):
        fields = (
            ('name', str),
        )

    @ns
    class GetModule(Expression):
        fields = (
            ('module_name', str),
        )

    @ns
    class Lambda(Expression):
        fields = (
            ('name', str),
            ('params', typeutil.List[Param]),
            ('body', Expression),
            ('vars', typeutil.List[LocalDeclaration]),
        )

    @ns
    class NullLiteral(Expression):
        fields = ()

    @ns
    class BoolLiteral(Expression):
        fields = (
            ('value', bool),
        )

    @ns
    class IntLiteral(Expression):
        fields = (
            ('value', int),
        )

    @ns
    class FloatLiteral(Expression):
        fields = (
            ('value', float),
        )

    @ns
    class StringLiteral(Expression):
        fields = (
            ('value', str),
        )


@Namespace
def parser(ns):

    builtin_token = lexer.lex(Source(*(['builtin'] * 3)))[0]

    class Scope:
        def __init__(self, parent):
            self.parent = parent
            self.table = dict()
            self.stack = [] if parent is None else parent.stack

        def error(self, message):
            raise Error(self.stack, message)

        @contextlib.contextmanager
        def push(self, token):
            self.stack.append(token)
            try:
                yield
            finally:
                self.stack.pop()

        def __getitem__(self, key: str) -> IR.Declaration:
            if key in self.table:
                return self.table[key]
            elif self.parent is not None:
                return self.parent[key]
            else:
                raise self.error(f'{repr(key)} not defined')

        def __setitem__(self, key: str, value: IR.Declaration):
            assert isinstance(value, IR.Declaration), value
            if key in self.table:
                with self.push(value.token), self.push(self.table[key].token):
                    raise self.error(f'Duplicate definition of {repr(key)}')
            self.table[key] = value

        def __contains__(self, key: str) -> bool:
            return key in self.table or self.parent and key in self.parent

        def pull(self, key: str) -> IR.Declaration:
            if key not in self.table:
                if self.parent and key in self.parent:
                    self.table[key] = self.parent.pull(key)
                else:
                    raise self.error(f'{repr(key)} not defined')
            return self.table[key]

    BUILTIN_NAMES = {
        'print',
    }

    def new_global_scope():
        scope = Scope(None)

        for builtin_name in BUILTIN_NAMES:
            scope[builtin_name] = IR.GlobalDeclaration(
                builtin_token,
                'builtin',
                builtin_name,
            )

        return scope

    @ns
    def parse(source: Source) -> IR.Module:
        global_scope = new_global_scope()
        module_name = source.name
        tokens = lexer.lex(source)
        i = 0
        indent_stack = []

        def qualify(name):
            return f'{module_name}.{name}'

        def should_skip_newlines():
            return indent_stack and indent_stack[-1]

        @contextlib.contextmanager
        def skipping_newlines(skipping):
            indent_stack.append(skipping)
            yield
            indent_stack.pop()

        def peek():
            nonlocal i
            if should_skip_newlines():
                while i < len(tokens) and tokens[i].type == '\n':
                    i += 1
            return tokens[i]

        def gettok():
            nonlocal i
            token = peek()
            i += 1
            return token

        def at(t: str):
            return peek().type == t

        def consume(t: str):
            if at(t):
                return expect(t)

        def consume_all(t: str):
            while consume(t):
                pass

        def expect(t: str):
            if not at(t):
                with global_scope.push(peek()):
                    raise global_scope.error(f'Expected {t} but got {peek()}')
            return gettok()

        def expect_id():
            return expect('NAME').value

        def parse_block(scope):
            token = peek()
            expect('{')
            exprps = []
            with skipping_newlines(False):
                consume_all('\n')
                while not consume('}'):
                    exprps.append(parse_expression(scope))
                    consume_all('\n')
            return Promise(lambda:
                IR.Block(token, [p.resolve() for p in exprps]))

        def expect_params(scope):
            params = []
            expect('(')
            with skipping_newlines(True):
                while not consume(')'):
                    token = peek()
                    name = expect_id()
                    params.append(IR.Param(token, name))
                    if not consume(','):
                        expect(')')
                        break
            return params

        def parse_expression(scope):
            return parse_postfix(scope)

        def parse_args(scope):
            expect('(')
            argps = []
            with skipping_newlines(True):
                while not consume(')'):
                    argps.append(parse_expression(scope))
                    if not consume(','):
                        expect(')')
                        break
            return Promise(lambda: [argp.resolve() for argp in argps])

        def pcall(f, *mixed_args):
            """Utility for calling functions when some arguments are still
            Promises.
            Semantically, this is kind of dirty mixing Promises and
            non-Promises like this, but in practice, at least for use
            cases here, I never actually want Promise types in the IR
            so this is ok.
            """
            @Promise
            def promise():
                return f(*[
                    arg.resolve() if isinstance(arg, Promise) else arg
                    for arg in mixed_args
                ])
            return promise

        def parse_postfix(scope):
            exprp = parse_primary(scope)
            while True:
                token = peek()
                if at('('):
                    argsp = parse_args(scope)
                    exprp = pcall(IR.Call, token, exprp, argsp)
                elif consume('.'):
                    name = expect_id()
                    if consume('='):
                        vexpr = parse_expression()
                        exprp = pcall(IR.SetAttr, token, exprp, name, vexpr)
                    else:
                        exprp = pcall(IR.GetAttr, token, expr, name)
                else:
                    break
            return exprp

        def parse_primary(scope):
            token = peek()
            if consume('('):
                with skipping_newlines(True):
                    exprp = parse_expression(scope)
                    expect(')')
                    return exprp
            if consume('$'):
                return parse_block(scope)
            if consume('INT'):
                return Promise.value(IR.IntLiteral(token, token.value))
            if consume('FLOAT'):
                return Promise.value(IR.FloatLiteral(token, token.value))
            if consume('STRING'):
                return Promise.value(IR.StringLiteral(token, token.value))
            if consume('NAME'):
                name = token.value
                if consume('='):
                    exprp = parse_expression(scope)
                    if name not in scope:
                        scope[name] = IR.LocalDeclaration(token, name)
                    @Promise
                    def promise():
                        with scope.push(token):
                            defn = scope[name]
                        if isinstance(defn, IR.GlobalDeclaration):
                            return IR.SetAttr(
                                token,
                                IR.GetModule(token, defn.module_name),
                                defn.short_name,
                                exprp.resolve(),
                            )
                        elif isinstance(defn, IR.LocalDeclaration):
                            return IR.SetLocal(
                                token,
                                defn.name,
                                exprp.resolve(),
                            )
                        elif isinstance(defn, IR.ModuleDeclaration):
                            with scope.push(token):
                                raise scope.error(
                                    f'{name} ({defn.module_name}) '
                                    f'is a module!')
                        else:
                            with scope.push(token):
                                raise scope.fubar(repr(defn))
                    return promise
                else:
                    @Promise
                    def promise():
                        with scope.push(token):
                            defn = scope[name]
                        if isinstance(defn, IR.GlobalDeclaration):
                            return IR.GetAttr(
                                token,
                                IR.GetModule(token, defn.module_name),
                                defn.short_name,
                            )
                        elif isinstance(defn, IR.LocalDeclaration):
                            return IR.GetLocal(token, defn.name)
                        elif isinstance(defn, IR.ModuleDeclaration):
                            return IR.GetModule(token, defn.name)
                        else:
                            with scope.push(token):
                                raise scope.fubar(repr(defn))
                    return promise

            with scope.push(peek()):
                raise scope.error(f'Expected expression but got {peek()}')

        def parse_global_def(scope):
            token = expect('def')
            short_name = expect_id()
            params = expect_params(scope)
            scope[short_name] = IR.GlobalDeclaration(
                token,
                module_name,
                short_name,
            )
            bodyp = parse_lambda_from_body(scope, token, short_name, params)
            return Promise(lambda:
                IR.GlobalVariableDefinition(
                    token,
                    module_name,
                    short_name,
                    bodyp.resolve(),
                ))

        def parse_lambda_from_body(scope, token, name, params):
            fscope = Scope(scope)
            for param in params:
                fscope[param.name] = (
                    IR.LocalDeclaration(param.token, param.name)
                )
            bodyp = parse_block(fscope)
            fvars = [
                defn for defn in fscope.table.values()
                if isinstance(defn, IR.LocalDeclaration)
            ]
            return Promise(lambda:
                IR.Lambda(token, name, params, bodyp.resolve(), fvars)
            )

        def parse_global_var(scope):
            token = expect('var')
            short_name = expect_id()
            scope[short_name] = IR.GlobalDeclaration(
                token,
                module_name,
                short_name,
            )
            exprp = (
                parse_expression(scope) if consume('=') else
                Promise.value(NullLiteral(token))
            )
            expect('\n')
            return Promise(lambda:
                IR.GlobalVariableDefinition(
                    token,
                    module_name,
                    short_name,
                    exprp.resolve(),
                ))

        def parse_global(scope):
            if at('def'):
                return parse_global_def(scope)
            else:
                return parse_global_var(scope)

        def expect_import(scope):
            token = expect('import')
            parts = [expect_id()]
            while consume('.'):
                parts.append(expect_id())
            module_name = '.'.join(parts)
            alias = expect_id() if consume('as') else parts[-1]
            scope[alias] = IR.ImportDeclaration(token, module_name)
            return IR.Import(token, module_name, alias)


        imports = []
        defn_promises = []

        token = peek()
        consume_all('\n')
        while at('import'):
            imports.append(expect_import(global_scope))
            consume_all('\n')

        while not at('EOF'):
            defn_promises.append(parse_global(global_scope))
            consume_all('\n')

        defns = [p.resolve() for p in defn_promises]

        return IR.Module(token, module_name, imports, defns)


def main():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('filenames', nargs='+')
    aparser.add_argument('--search-dir', default='srcs')
    aparser.add_argument('--out-dir', default='out')
    args = aparser.parse_args()
    for filename in args.filenames:
        source = Source.from_name_and_path('main', filename)
        print(parser.parse(source))


if __name__ == '__main__':
    main()

