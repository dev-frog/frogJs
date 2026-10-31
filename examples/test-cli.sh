#!/bin/bash
# CLI and REPL self-test: exits with code 1 if any check fails.
# Usage: examples/test-cli.sh [path/to/frogjs]   (default: build/frogjs)

FROGJS="${1:-build/frogjs}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
export FROGJS_REPL_HISTORY=""

passed=0
failed=0

# check <name> <expected> <actual>
check() {
    if [ "$2" == "$3" ]; then
        passed=$((passed + 1))
        echo "  ✓ $1"
    else
        failed=$((failed + 1))
        echo "  ✗ $1" >&2
        echo "      expected: $(printf '%q' "$2")" >&2
        echo "      got:      $(printf '%q' "$3")" >&2
    fi
}

# check_contains <name> <needle> <haystack>
check_contains() {
    if [[ "$3" == *"$2"* ]]; then
        passed=$((passed + 1))
        echo "  ✓ $1"
    else
        failed=$((failed + 1))
        echo "  ✗ $1" >&2
        echo "      expected to contain: $(printf '%q' "$2")" >&2
        echo "      got:                 $(printf '%q' "$3")" >&2
    fi
}

echo "Flags"
check "--version" "$("$FROGJS" -v)" "$("$FROGJS" --version)"
check_contains "--version format" "v0." "$("$FROGJS" --version)"
check_contains "--help" "Usage: frogjs" "$("$FROGJS" --help)"
check "--eval" "3" "$("$FROGJS" --eval 'console.log(1 + 2)')"
check "-e passes arguments" "a,b" "$("$FROGJS" -e 'console.log(process.argv.slice(1).join())' a b)"
check "--print" "{ a: [ 1, 2 ] }" "$("$FROGJS" --print '({ a: [1, 2] })')"
check "--print=code" "6" "$("$FROGJS" --print='2 * 3')"
check "-pe prints strings unquoted" "hi" "$("$FROGJS" -pe '"hi"')"
check "-e sets __filename" "[eval]" "$("$FROGJS" -p '__filename')"
check "process.execArgv" "[ '-p', 'process.execArgv' ]" "$("$FROGJS" -p 'process.execArgv')"
check "script from stdin" "piped" "$(echo 'console.log("piped")' | "$FROGJS")"
check "script from stdin with -" "x" "$(echo 'console.log(process.argv[2])' | "$FROGJS" - x)"

echo "Errors"
"$FROGJS" --no-such-flag 2>/dev/null
check "bad option exits with 9" "9" "$?"
check_contains "bad option message" "bad option: --no-such-flag" "$("$FROGJS" --no-such-flag 2>&1)"
"$FROGJS" -e 2>/dev/null
check "-e without code exits with 9" "9" "$?"
check_contains "missing file message" "cannot open 'missing.js'" "$("$FROGJS" missing.js 2>&1)"
"$FROGJS" missing.js 2>/dev/null
check "missing file exits with 1" "1" "$?"

printf 'const x = 1;\nnull.y;\n' > "$TMP_DIR/throws.js"
output="$("$FROGJS" "$TMP_DIR/throws.js" 2>&1)"
check "uncaught error exits with 1" "1" "$("$FROGJS" "$TMP_DIR/throws.js" >/dev/null 2>&1; echo $?)"
check_contains "error shows file and line" "throws.js:2" "$output"
check_contains "error shows the source line and caret" $'null.y;\n     ^' "$output"
check_contains "error shows the stack" "TypeError: Cannot read properties of null (reading 'y')" "$output"

printf 'x = 1;\nrequire("./throws");\n' > "$TMP_DIR/requires.js"
check_contains "errors in required modules propagate" "throws.js:2" "$("$FROGJS" "$TMP_DIR/requires.js" 2>&1)"

printf 'let a = 1 +* 2;\n' > "$TMP_DIR/syntax.js"
"$FROGJS" --check "$TMP_DIR/syntax.js" 2>/dev/null
check "--check fails on a syntax error" "1" "$?"
check_contains "--check reports the error" "SyntaxError: Unexpected token '*'" "$("$FROGJS" -c "$TMP_DIR/syntax.js" 2>&1)"
check "--check passes valid code without running it" "" "$("$FROGJS" -c examples/hello.js)"

echo "REPL (piped with -i)"
repl() {
    printf "$1" | "$FROGJS" -i 2>&1
}
check "expression" $'> 2\n> ' "$(repl '1 + 1\n')"
check "statement prints undefined" $'> undefined\n> 1\n> ' "$(repl 'let a = 1\na\n')"
check "multi-line input" $'> ... ... [ 1, 2, 3 ]\n> ' "$(repl '[1,\n2,\n3]\n')"
check "method chain continued on a new line" $'> ... \'1\'\n> ' "$(repl '(1\n.toString())\n')"
check "multi-line function" $'> ... undefined\n> 7\n> ' "$(repl 'function g() {\nreturn 7 }\ng()\n')"
check "object literal" $'> { a: 1 }\n> ' "$(repl '{ a: 1 }\n')"
check "_ holds the last result" $'> 42\n> 43\n> ' "$(repl '40 + 2\n_ + 1\n')"
check "uncaught error" $'> Uncaught ReferenceError: nope is not defined\n> ' "$(repl 'nope\n')"
check "_error holds the last error" $'> Uncaught 1\n> 1\n> ' "$(repl 'throw 1\n_error\n')"
check "syntax error recovery" $'> Uncaught SyntaxError: Unexpected token \'*\'\n> 3\n> ' "$(repl '1 +* 2\n1 + 2\n')"
check ".break abandons multi-line input" $'> ... > 1\n> ' "$(repl '[1,\n.break\n1\n')"
check_contains ".help" ".exit     Exit the REPL" "$(repl '.help\n')"
check ".exit" "> " "$(repl '.exit\n1 + 1\n')"
check "invalid keyword" $'> Invalid REPL keyword\n> ' "$(repl '.nope\n')"
check "leading-dot number" $'> 0.5\n> ' "$(repl '.5\n')"
check ".clear resets the context" $'> undefined\n> Clearing context...\n> \'undefined\'\n> ' "$(repl 'var gone = 1\n.clear\ntypeof gone\n')"
check "results are pretty-printed" $'> Map(1) { \'k\' => [ 1, 2 ] }\n> ' "$(repl 'new Map([["k", [1, 2]]])\n')"
echo 'var loaded = 99' > "$TMP_DIR/load.js"
check ".load" $'> undefined\n> 99\n> ' "$(repl ".load $TMP_DIR/load.js\nloaded\n")"
repl "1 + 1\n.save $TMP_DIR/session.js\n" > /dev/null
check ".save" "1 + 1" "$(cat "$TMP_DIR/session.js")"
check "-e then REPL" $'> 14\n> ' "$(printf 'x * 2\n' | "$FROGJS" -e 'var x = 7' -i)"

echo ""
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
