"""
From powershell:
$PSDefaultParameterValues['Out-File:Encoding'] = 'utf8'
python .\klc.py > main.c

From linux subsystem for windows:
gcc -Werror -Wpedantic -Wall -Wno-unused-function -Wno-unused-variable main.c && \
cp main.{c,cc} && \
g++ -Werror -Wpedantic -Wall -Wno-unused-function -Wno-unused-variable main.cc && \
./a.out
"""
from typing import NamedTuple, Tuple, List, Union, Optional, Callable, Iterable
import abc
import re
import typing
import contextlib


SYMBOLS = [
    ';',
    '.', ',', '!', '@', '^', '&', '+', '-', '/', '%', '*', '.', '=', '==', '<',
    '>', '<=', '>=', '(', ')', '{', '}', '[', ']',
]

KEYWORDS = {'class', 'trait', 'if', 'else', 'while', 'break', 'continue'}

PRIMITIVE_TYPES = {
    'void',
    'int',
    'long',
    'size_t',
    'double',
}


PRELUDE = """/* Autogenerated by the KL Compiler */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int KLC_Status;
typedef struct KLC_header KLC_header;
typedef struct KLC_typeinfo KLC_typeinfo;

struct KLC_header {
  KLC_typeinfo* type;
  size_t refcnt;
  KLC_header* next;
};

struct KLC_typeinfo {
  const char* name;
  void (*deleter)(KLC_header*, KLC_header**);
};

void KLC_retain(KLC_header *obj) {
  if (obj) {
    obj->refcnt++;
  }
}

void KLC_partial_release(KLC_header* obj, KLC_header** delete_queue) {
  if (obj) {
    if (obj->refcnt) {
      obj->refcnt--;
    } else {
      obj->next = *delete_queue;
      *delete_queue = obj;
    }
  }
}

void KLC_release(KLC_header *obj) {
  KLC_header* delete_queue = NULL;
  KLC_partial_release(obj, &delete_queue);
  while (delete_queue) {
    obj = delete_queue;
    delete_queue = delete_queue->next;
    obj->type->deleter(obj, &delete_queue);
    free(obj);
  }
}

typedef struct KLCNString KLCNString;
struct KLCNString {
  KLC_header header;
  size_t size;
  char *buffer;
};

void KLC_deleteString(KLC_header* robj, KLC_header** dq) {
  KLCNString *obj = (KLCNString*) robj;
  free(obj->buffer);
}

KLC_typeinfo KLC_typeString = {
  "String",
  KLC_deleteString
};

KLCNString* KLC_mkstr(const char *str) {
  KLCNString* obj = (KLCNString*) malloc(sizeof(KLCNString));
  size_t len = strlen(str);
  char* buffer = (char*) malloc(sizeof(char) * (len + 1));
  strcpy(buffer, str);
  obj->header.type = &KLC_typeString;
  obj->header.refcnt = 0;
  obj->header.next = NULL;
  obj->size = len;
  obj->buffer = buffer;
  return obj;
}

void KLCNmain();

int main() {
  KLCNmain();
  return 0;
}

"""


class Source(NamedTuple):
    filename: str
    data: str


class Token:
    __slots__ = ['type', 'value', 'source', 'i']

    def __init__(self,
                 type: str,
                 value: object = None,
                 source: Optional[Source] = None,
                 i: Optional[int] = None) -> None:
        self.type = type
        self.value = value
        self.source = source
        self.i = i

    def _key(self) -> Tuple[str, object]:
        return self.type, self.value

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Token) and self._key() == other._key()

    def __hash__(self) -> int:
        return hash(self._key())

    def __repr__(self) -> str:
        return f'Token({repr(self.type)}, {repr(self.value)})'

    @property
    def lineno(self) -> int:
        assert self.source is not None
        assert self.i is not None
        return self.source.data.count('\n', 0, self.i) + 1

    @property
    def colno(self) -> int:
        assert self.source is not None
        assert self.i is not None
        return self.i - self.source.data.rfind('\n', 0, self.i)

    @property
    def line(self) -> str:
        assert self.source is not None
        assert self.i is not None
        s = self.source.data
        a = s.rfind('\n', 0, self.i) + 1
        b = s.find('\n', self.i)
        if b == -1:
            b = len(s)
        return s[a:b]

    @property
    def info(self) -> str:
        line = self.line
        colno = self.colno
        lineno = self.lineno
        spaces = ' ' * (colno - 1)
        return f'on line {lineno}\n{line}\n{spaces}*\n'


class Error(Exception):
    def __init__(self, tokens: Iterable[Token], message: str) -> None:
        super().__init__(''.join(token.info for token in tokens) + message)


class Pattern(abc.ABC):
    @abc.abstractmethod
    def match(self, source: Source, i: int) -> Optional[Tuple[Token, int]]:
        pass


class RegexPattern(Pattern):
    def __init__(
            self,
            regex: Union[typing.Pattern[str], str],
            *,
            type: Optional[str] = None,
            type_callback: Callable[[str], str] = lambda value: value,
            value_callback: Callable[[str], object] = lambda x: x) -> None:
        if isinstance(regex, str):
            regex = re.compile(regex)

        if type is not None:
            type_ = str(type)  # for mypy
            type_callback = lambda _: type_

        self.regex = regex
        self.type_callback = type_callback
        self.value_callback = value_callback

    def match(self, source: Source, i: int) -> Optional[Tuple[Token, int]]:
        m = self.regex.match(source.data, i)
        if m is None:
            return None

        raw_value = m.group()
        type_ = self.type_callback(raw_value)
        value = self.value_callback(raw_value)
        return Token(type_, value, source, i), m.end()


def _make_lexer():
    whitespace_pattern = RegexPattern(r'\s+')

    class Lexer:
        def __init__(
                self,
                patterns: List[Pattern],
                *,
                ignore_pattern: Pattern,
                filter: Callable[[Token], Optional[Token]] = lambda token: token
        ) -> None:
            self.patterns = patterns
            self.ignore_pattern = ignore_pattern
            self.filter = filter

        def lex(self, source: Union[Source, str]) -> List[Token]:
            if isinstance(source, str):
                source = Source('<string>', source)

            ignore_pattern = self.ignore_pattern
            i = 0
            s = source.data
            tokens = []

            while True:
                while True:
                    match = ignore_pattern.match(source, i)
                    if match is None:
                        break
                    _, i = match

                if i >= len(s):
                    break

                for pattern in self.patterns:
                    match = pattern.match(source, i)
                    if match is not None:
                        unfiltered_token, i = match
                        token = self.filter(unfiltered_token)
                        if token is not None:
                            tokens.append(token)
                        break
                else:
                    token = Token('ERR', None, source, i)
                    raise Error([token], 'Unrecognized token')

            tokens.append(Token('EOF', None, source, i))
            return tokens

    class MatchingBracesSkipSpacesFilter:
        """A lexer filter for ignoring newlines that appear inside parentheses
        or square brackets.
        """

        def __init__(self) -> None:
            self.stack: List[Token] = []

        def should_skip_newlines(self) -> bool:
            return bool(self.stack and self.stack[-1].type != '{')

        def __call__(self, token: Token) -> Optional[Token]:
            if token.type in ('{', '[', '('):
                self.stack.append(token)

            if token.type in ('}', ']', ')'):
                self.stack.pop()

            if token.type == '\n' and self.should_skip_newlines():
                return None

            return token

    def make_symbols_pattern(symbols: Iterable[str]) -> Pattern:
        return RegexPattern('|'.join(map(re.escape, reversed(sorted(symbols)))))

    def make_keywords_pattern(keywords: Iterable[str]) -> Pattern:
        return RegexPattern('|'.join(r'\b' + kw + r'\b' for kw in keywords))

    name_pattern = RegexPattern(
        '\w+', type='NAME', value_callback=lambda value: value)

    def string_pattern_value_callback(value: str) -> str:
        return str(eval(value))  # type: ignore

    string_pattern_regex = '|'.join([
        r'(?:r)?"""(?:(?:\\.)|(?!""").)*"""',
        r"(?:r)?'''(?:(?:\\.)|(?!''').)*'''",
        r'(?:r)?"(?:(?:\\.)|(?!").)*"',
        r"(?:r)?'(?:(?:\\.)|(?!').)*'",
    ])

    string_pattern = RegexPattern(
        string_pattern_regex,
        type='STRING',
        value_callback=string_pattern_value_callback)

    float_pattern = RegexPattern(
        r'\d+\.\d*|\.\d+', type='FLOAT', value_callback=eval)  # type: ignore
    int_pattern = RegexPattern(
        r'\d+', type='INT', value_callback=eval)  # type: ignore

    def make_simple_lexer(*, keywords: Iterable[str], symbols: Iterable[str]):
        keywords_pattern = make_keywords_pattern(keywords)
        symbols_pattern = make_symbols_pattern(symbols)
        return Lexer(
            [
                string_pattern,
                keywords_pattern,
                float_pattern,
                int_pattern,
                name_pattern,
                symbols_pattern,
            ],
            ignore_pattern=whitespace_pattern)

    return make_simple_lexer(keywords=KEYWORDS, symbols=SYMBOLS)


_lexer = _make_lexer()


def lex(source):
    return _lexer.lex(source)


class FractalStringBuilder(object):
    def __init__(self):
        self.parts = []

    def __str__(self):
        parts = []
        self._dump(parts)
        return ''.join(parts)

    def _dump(self, parts):
        for part in self.parts:
            if isinstance(part, FractalStringBuilder):
                part._dump(parts)
            else:
                parts.append(str(part))

    def __iadd__(self, s):
        self.parts.append(s)
        return self

    def __call__(self, s):
        self += s
        return self

    def spawn(self):
        child = FractalStringBuilder()
        self.parts.append(child)
        return child


def translate(*sources):
    i = tokens = None

    global_token_map = dict()

    todo = []  # list of callbacks to invoke after scope is populated

    out = FractalStringBuilder()
    out += PRELUDE
    fwd = out.spawn()  # forward decls
    hdr = out.spawn()  # header
    dlt = out.spawn()  # deleter functions
    tpi = out.spawn()  # typeinfo definitions
    cst = out.spawn()  # constructor functions
    src = out.spawn()  # function definitions

    def _cdecltype(name):
        if name in PRIMITIVE_TYPES:
            return name
        else:
            return f'KLCN{name}*'

    def set_token_for_global_name(name, token):
        if name in global_token_map:
            throw(f'Conflicting definitions for {name}',
                  [global_token_map[name], token])
        global_token_map[name] = token

    def throw(message, tokens=None):
        raise Error([peek()] if tokens is None else tokens, message)

    def peek(lookahead=0):
        return tokens[min(i + lookahead, len(tokens) - 1)]

    def gettok():
        nonlocal i
        token = tokens[i]
        i += 1
        return token

    def at(token_type, lookahead=0):
        return peek(lookahead).type == token_type

    def consume(token_type):
        if at(token_type):
            return gettok()

    def expect(token_type):
        if not at(token_type):
            throw(f'Expected {token_type} but got {peek()}')
        return gettok()

    hdr('\n/* header */\n')
    dlt('\n/* deleters */\n')
    tpi('\n/* typeinfo definitions */\n')
    cst('\n/* constructors */\n')
    src('\n/* source */\n')

    def translate_class():
        token = expect('class')
        name = expect('NAME').value
        set_token_for_global_name(name, token)

        fwd(f'typedef struct KLCN{name} KLCN{name};\n')

        if consume(';'):
            return

        dlt(f'void KLC_delete{name}(KLC_header* robj, KLC_header** dq) ')
        dlt('{\n')
        dlt(f'  KLCN{name}* obj = (KLCN{name}*) robj;\n')

        tpi(f'KLC_typeinfo KLC_type{name} = ' '{\n')
        tpi(f'  "{name}",\n')
        tpi(f'  KLC_delete{name}\n')
        tpi('};\n')

        cst(f'KLCN{name}* KLC_malloc{name}() ')
        cst('{\n')
        cst(f'  KLCN{name}* obj = (KLCN{name}*) malloc(sizeof(KLCN{name}));\n')
        cst(f'  obj->header.type = &KLC_type{name};\n')
        cst(f'  obj->header.refcnt = 0;\n')
        cst(f'  obj->header.next = NULL;\n')

        hdr(f'struct KLCN{name} ')
        hdr('{\n')
        hdr('  KLC_header header;\n')

        field_token_map = dict()
        expect('{')
        while not consume('}'):
            if at('NAME') and at('NAME', 1) and at(';', 2):
                field_token = peek()
                field_type = expect('NAME').value
                field_name = expect('NAME').value
                expect(';')

                hdr(f'  {_cdecltype(field_type)} KLCN{field_name};\n')

                if field_type in PRIMITIVE_TYPES:
                    cst(f'  obj->KLCN{field_name} = 0;\n')
                else:
                    dlt(f'  KLC_partial_release((KLC_header*) obj->KLCN{field_name}, dq);\n')
                    cst(f'  obj->KLCN{field_name} = NULL;\n')

                if field_name in field_token_map:
                    throw(f'Duplicate field definition for {name}.{field_name}',
                          tokens=[field_token, field_token_map[field_name]])
            else:
                throw(f'Expected field or method definition but got {peek()}')

        cst('  return obj;\n')

        hdr('};\n')
        dlt('}\n')
        cst('}\n')

    def translate_function():
        token = peek()
        return_type = expect('NAME').value
        name = expect('NAME').value
        set_token_for_global_name(name, token)
        params = []
        expect('(')
        while at('NAME'):
            param_type = expect('NAME').value
            param_name = expect('NAME').value
            params.append((param_type, param_name))
            if not consume(','):
                break
        expect(')')
        argsstr = ', '.join(f'{_cdecltype(t)} KLCN{n}' for t, n in params)
        proto = f'{_cdecltype(return_type)} KLCN{name}({argsstr})'

        hdr(f'{proto};\n')

        if not consume(';'):
            src(f'{proto} ' "{\n")
            translate_block(1)
            src('}\n')

    def translate_statement(depth):
        if at('{'):
            translate_block(depth)
        else:
            throw('Expected statement')

    class ExpressionContext:
        def __init__(self, depth):
            self.tempvars = []
            self.depth = depth

    @contextlib.contextmanager
    def expression_context(depth):
        ec = ExpressionContext(depth)
        yield ec

        for tempvar in reversed(ec.tempvars):
            indent(depth)
            src(f'KLC_release((KLC_header*) {tempvar})')

    def translate_expression(ec, depth):
        pass

    def indent(depth):
        src('  ' * depth)

    def translate_block(depth):
        indent(depth)
        src('{\n')

        expect('{')
        while not consume('}'):
            translate_statement(depth + 1)

        indent(depth)
        src('}\n')

    def translate_source(source):
        nonlocal tokens, i
        tokens = lex(source)
        i = 0
        while not at('EOF'):
            if at('class'):
                translate_class()
            elif at('NAME') and at('NAME', 1) and at('(', 2):
                translate_function()
            else:
                throw(f'Expected global definition but got {peek()}')

    for source in sources:
        translate_source(source)

    for callback in todo:
        callback()

    return str(out)


# print(lex(Source('<string>', 'abc + def')))
print(translate(Source('<string>', """
class String;
class List;

class Foo {
  size_t i;
  int x;
}

class Bar {
  Foo foo;
}

void puts(String s);
String sprintf(String fmt, List args);

void main() {}

""")))
