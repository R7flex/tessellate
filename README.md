# dll-ollvm

LLVM 18 New Pass Manager plugin for Windows. Designed for manually mapped DLLs.

## Passes

- `tess-sub` — instruction substitution (MBA-style rewrites)
- `tess-bcf` — bogus control flow (opaque predicates)
- `tess-fla` — control-flow flattening (state-machine dispatcher)
- `tess-trim` — removes `llvm.global_ctors`/`dtors`, drops dead internals

Run all at once with preset `tess-obf`.

## Usage

```
clang -c -emit-llvm -O1 --target=x86_64-pc-windows-msvc -o in.bc file.cpp
opt -passes="default<O2>" in.bc -o mid.bc
opt -load-pass-plugin=LLVMObfuscationx.dll -passes=tess-obf mid.bc -o out.bc
llc -filetype=obj -mtriple=x86_64-pc-windows-msvc -O2 out.bc -o out.obj
```

## Build

```
cmake -S plugin -B build -G "Visual Studio 17 2022" -A x64 -DLT_LLVM_INSTALL_DIR=C:\path\to\LLVM-18.1.5-dev
cmake --build build --config Release
```

Requires LLVM 18.1.5 with plugin support. Plugin and `opt.exe` must be from the same LLVM build.

The passes operate on LLVM IR so they may work on executables as well, but this has never been tested.