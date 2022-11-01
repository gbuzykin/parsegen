# LALR-grammar Based Parser Generator

This tool generates syntax [LALR](https://en.wikipedia.org/wiki/LALR_parser) (Look-Ahead LR parser)
analyzer based on grammar specified using
[BNF](https://en.wikipedia.org/wiki/Backus%E2%80%93Naur_form) (Backus–Naur or context-free Form).
Its input file is very similar to the input of standard [yacc](https://en.wikipedia.org/wiki/Yacc)
tool by structure and syntax.  But contrary to *yacc* instead of generating full analyzer with
inlined action handlers `parsegen` generates only tables and parser engine C-compliant function as
files, which can be included into the rest analyzer's implementation.

This README file briefly describes this tool and can be not up-do-date, it also gives guidelines how
to use this stuff.  For more detailed information see
[wiki](https://github.com/gbuzykin/parsegen/wiki) pages.

## A Simple Usage Example

Assume that we already have built this tool as `parsegen` executable, and we want to create a simple
arithmetical calculator, which supports '+', '-' (unary and binary), '*', and '/' operators.
Brackets '(' and ')' can also be used to override precedence.

Here is a source input file `test.gr` for the desired parser:

```gr
# This is an arithmetical expression syntax analyzer.

# This section contains token and action definitions as well as token priority and
# associativity specifications.
# Start conditions are also can be defined in this section using the form: %start <sc-name>.
# By default only one `initial` start condition is implicitly defined.

# Tokens
%token num     # number token
%token eof     # end of file token

# Operator precedence
# Tokens in upper lines has less priority than lower ones
%left '+' '-'
%left '*' '/'
%right $unary  # Names preceded with symbol '$' are used for internal tokens, which are declared
               # in-place and are not exposed to final analyzer. They are only used as priority
               # references. Names $default, $empty, and $error are reserved.

# Actions
%action add    # add action ('+')
%action sub    # subtract action ('-')
%action mul    # multiply action ('*')
%action div    # divide action ('/')
%action uminus # unary minus

%% # section separator

# This section describes grammar using Backus–Naur form.
# By default its first production is a starting production with default `initial` start condition.
# If one of productions is needed to be marked as a starting production, then start condition
# name should be specified in triangle braces just after the left part of the production.
# Note, that starting production must be ended with a token.

result : expr [eof] ;     # or result<initial> : expr [eof]

expr : '(' expr ')'       # character ':' separates left and right production parts
  | expr '+' expr {add}   # character '|' separates right parts of productions with the same left parts
  | expr '-' expr {sub}   # actions to be performed can be specified in curly braces in any point of a production
  | expr '*' expr {mul}   # single quotation marks can be used for single-character tokens
  | expr '/' expr {div}
  | '-' expr {uminus} %prec $unary    # explicit precedence specification is used for this production
  | [num]                 # tokens are specified in square brackets
  ;                       # character ';' finishes production

%% # mandatory end of section
```

Then, in order to generate our syntax analyzer let's issue the following:

```bash
./parsegen test.gr
test.gr: info: building analyzer...
test.gr: info:  - action table row size: max 6, avg 2
test.gr: info:  - goto table row size: max 7, avg 4
test.gr: info: done: 0 shift/reduce, 0 reduce/reduce conflict(s) found
```

As the result two files with default names `parser_defs.h` and `parser_analyzer.inl` are generated.
If it is needed to specify the names explicitly the following should be issued:

```bash
./parsegen test.gr -o <new-analyzer-file-name> --header-file=<new-defs-file-name>
```

File `parser_defs.h` contains numerical identifiers for tokens, actions, and start conditions (or
start analyzer states).  Only one `sc_initial` start condition is defined for our example.

File `parser_analyzer.inl` contains necessary tables and `parse()` function implementation, defined
as `static`.  This function has the following prototype:

```c
static int parse(int tt, int* sptr0, int** p_sptr, int rise_error);
```

where:

- `tt` - look-ahead token identifier
- `sptr0` - pointer to the beginning of user-provided state stack
- `p_sptr` - pointer to current user-provided state stack pointer
- `rise_error` - if is `<0` then an error is risen with code `rise_error`, error recovering
  procedure is initiated as the analyzer faced a syntax error

returns: action identifier to perform on reduction or

- `predef_act_shift` to shift look-ahead token
- `predef_act_reduce` to reduce without specified action

## How It Works

The analyzer returns the decision result whether to shift next look-ahead token or to reduce some
production based on current state stack and current look-ahead token identifier.

The starting state must be on the top of user-provided state stack before the first call of
`parse()`.  Current stack pointer `*p_sptr` must point to the position *after* the last state (the
first free stack cell).

Before each `parse()` call one free stack cell must be reserved.  The caller is responsible for it.

In case of reduction as well as in case of erroneous reduction (involving $error token) the length
of this reduction can be calculated as a difference between stack top pointers before and after the
call plus 1.

The calculators's code can be something like this:

```cpp
...
namespace parser_detail {
#include "parser_defs.h"
#include "parser_analyzer.inl"
}
...
int main() {
    ...
    auto state_stack = std::make_unique<int[]>(kInitialStackSize);
    int* sfirst = state_stack.data();
    int* slast = sfirst + kInitialStackSize;
    int* sptr = sfirst;
    *sptr++ = parser_detail::sc_initial;
    ...
    std::vector<double> eval_stack;
    ...
    int tt = <... obtain the first token identifier, e.g., from lexical analyzer ...>;
    ...
    while (true) {
        <... ensure that state stack contains at least one free cell ...>
        int act = parser_detail::parse(tt, sfirst, &sptr, 0);
        if (act < 0) {
            <... log syntax error ...>
            return -1;
        } else if (act != parser_detail::predef_act_shift) {
            switch (act) {
                case parser_detail::act_add: {
                    *(eval_stack.end() - 2) += *(eval_stack.end() - 1);
                    eval_stack.pop_back();
                } break;
                case parser_detail::act_sub: {
                    *(eval_stack.end() - 2) -= *(eval_stack.end() - 1);
                    eval_stack.pop_back();
                } break;
                case parser_detail::act_mul: {
                    *(eval_stack.end() - 2) *= *(eval_stack.end() - 1);
                    eval_stack.pop_back();
                } break;
                case parser_detail::act_div: {
                    <... log error if denominator is zero ...>
                    *(eval_stack.end() - 2) /= *(eval_stack.end() - 1);
                    eval_stack.pop_back();
                } break;
                case parser_detail::act_uminus: {
                    *(eval_stack.end() - 1) = -*(eval_stack.end() - 1);
                } break;
                default: break;
            }
        } else if (tt != parser_detail::tt_eof) {
            eval_stack.push_back(<... current look-ahead token value ...>);
            tt = <... obtain the next token identifier, e.g., from lexical analyzer ...>;
        } else {
            // we can accept the whole expression on shifting the ending terminal
            break;
        }
    }
    <... print the single element in `eval_stack` ...>
    return 0;
}
```

## Command Line Options

```bash
$ ./parsegen --help
OVERVIEW: A tool for LALR-grammar based parser generation
USAGE: ./parsegen.exe file [-o <file>] [--header-file=<file>] [-h] [-V]
OPTIONS:
    -o, --outfile=<file>  Place the output analyzer into <file>.
    --header-file=<file>  Place the output definitions into <file>.
    -h, --help            Display this information.
    -V, --version         Display version.
```

## How to Build `parsegen`

Perform these steps to build the project (in linux, for other platforms the steps are similar):

1. Clone `parsegen` repository and enter it

    ```bash
    git clone https://github.com/gbuzykin/parsegen
    cd parsegen
    ```

2. Initialize and update `uxs` submodule

    ```bash
    git submodule update --init
    ```

3. Then, compilation script should be created using `cmake` tool.  To use the default C++ compiler
   just issue (for new enough version of `cmake`)

    ```bash
    cmake -S . -B build
    ```

    or to make building scripts for debug or optimized configurations issue the following

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Debug"
    ```

    or

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Release"
    ```

4. Enter created folder `build` and run `make`

    ```bash
    cd build
    make
    ```

    to use several parallel processes (e.g. 8) for building run `make` with `-j` key

    ```bash
    make -j 8
    ```
