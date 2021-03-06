#!/bin/bash
basedir=$(dirname "$0")
cd "$basedir/.." || exit 1
source "ci/ninja.sh" || exit 1

function clang_tidy_works {
    if ! hash clang-tidy 2> /dev/null; then
        return 1
    fi

    return 0
}

if [[ ! -z ${MOLCH_CI_DISABLE_CLANG_TIDY+x} ]]; then
    echo "Clang Tidy is disabled!"
    exit 0
fi

if ! clang_tidy_works; then
    echo "Clang Tidy isn't available!"
    exit 0
fi

export CC=clang
export CXX=clang++

output_dir=clang-tidy
if [[ "$1" != "tidy-only" ]]; then
    [[ -e "$output_dir" ]] && rm -r "$output_dir"
    mkdir "$output_dir"
    cd "$output_dir" || exit 1
    if meson .. --buildtype plain -Dlua_bindings=false; then
        # This has to be done with else because with '!' it won't work on Mac OS X
        true
    else
        exit $? #abort on failure
    fi

    ninja

    sed -i 's/-pipe//g' compile_commands.json
else
    cd "$output_dir" || exit 1
fi

clang-tidy -p='.' '../lib/'*.cpp '../test/'*.cpp
