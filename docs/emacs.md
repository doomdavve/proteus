# Emacs development

This setup mirrors the one used by Maldives. It uses Emacs 30's built-in
`c-ts-mode`, Eglot, Flymake, Xref, Project.el, Compilation mode, and LLDB
integration. The external language tools are `clangd` and `clang-format`.

## Install and check the tools

Apple's Command Line Tools include `clangd`. This Mac already has it at
`/usr/bin/clangd`. Install the standalone formatter with Homebrew:

```sh
brew install clang-format
```

`executable-find` is an Emacs Lisp function rather than an interactive command.
Press `M-:` and evaluate this expression to check all four tools:

```elisp
(mapcar (lambda (name) (cons name (executable-find name)))
        '("clangd" "clang-format" "cmake" "lldb"))
```

Each name should be paired with an absolute path; a `nil` value means Emacs
cannot find that executable.

If a Homebrew executable is missing when Emacs was launched from Finder, add
Homebrew to both `PATH` and Emacs's executable search path in your init file:

```elisp
(setenv "PATH" (concat "/opt/homebrew/bin:" (getenv "PATH")))
(add-to-list 'exec-path "/opt/homebrew/bin")
```

Restart Emacs after changing `PATH`, or evaluate both forms with `C-x C-e`.

## Install the C and C++ tree-sitter grammars

Emacs implements `c-ts-mode` and `c++-ts-mode` in the same library. In Emacs 30,
loading that library probes both grammars even for a `.c` file. Install both to
avoid a long warning about the unavailable `cpp` grammar.

Evaluate this expression in Emacs:

```elisp
(progn
  (require 'treesit)
  (add-to-list 'treesit-language-source-alist
               '(c "https://github.com/tree-sitter/tree-sitter-c"))
  (add-to-list 'treesit-language-source-alist
               '(cpp "https://github.com/tree-sitter/tree-sitter-cpp")))
```

Then run both installation commands:

```text
M-x treesit-install-language-grammar RET c RET
M-x treesit-install-language-grammar RET cpp RET
```

Verify the installation by evaluating:

```elisp
(mapcar #'treesit-language-available-p '(c cpp))
```

The result should be `(t t)`.

## Configure C and Eglot

Add this to `~/.emacs.d/init`:

```elisp
(use-package treesit
  :ensure nil
  :config
  (add-to-list 'treesit-language-source-alist
               '(c "https://github.com/tree-sitter/tree-sitter-c"))
  (add-to-list 'treesit-language-source-alist
               '(cpp "https://github.com/tree-sitter/tree-sitter-cpp")))

(use-package c-ts-mode
  :ensure nil
  :mode (("\\.c\\'" . c-ts-mode)
         ("\\.h\\'" . c-ts-mode)))

(use-package eglot
  :ensure nil
  :hook (c-ts-mode . eglot-ensure))

;; Recognize CMake projects even before they have Git metadata.
(with-eval-after-load 'project
  (add-to-list 'project-vc-extra-root-markers "CMakeLists.txt"))
```

Restart Emacs or evaluate the forms. Open `src/main.c`; `M-x describe-mode`
should report `c-ts-mode`, and `M-x eglot` should say the buffer is already
managed. If the file was already open, run `M-x normal-mode` in its buffer.

Eglot already knows that `clangd` serves `c-ts-mode`; no server mapping is
needed in the configuration.

If Emacs reports that the grammar for `cpp` is unavailable even though you only
opened C source, install the `cpp` grammar as described above. This comes from
loading Emacs's shared C/C++ tree-sitter implementation; it does not mean the
file was classified as C++.

## Give clangd the real build flags

C needs the same include paths and defines in the editor that it receives from
the compiler. In particular, `raylib.h` lives inside CMake's fetched dependency.
Proteus enables `CMAKE_EXPORT_COMPILE_COMMANDS`, so configure the project once:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

This creates `build/compile_commands.json`. Clangd automatically searches a
project's `build` directory for that file. After configuring, use
`M-x eglot-reconnect` if an existing Eglot session still reports that it cannot
find `raylib.h`.

Do not commit `compile_commands.json`; it contains machine-specific paths and is
already below the ignored `build` directory.

## Build and run from Emacs

Save the current buffer with `C-x C-s`, then use:

```text
C-x p c
```

This runs `project-compile` (`M-x project-compile`). It finds the project root
through Git and starts compilation there, so it works when the current buffer
is `src/main.c` and its working directory is `src/`. Accept the command with
`RET`:

```sh
cmake --build build -j 8 && ctest --test-dir build --output-on-failure
```

That default comes from the repository's `.dir-locals.el`. Avoid plain
`M-x compile` from a subdirectory: it uses that buffer's `default-directory`
and would look for `src/build` rather than the project's top-level `build`
directory.

After the first build, `M-x recompile` reruns the same command from the same
project directory. You can also switch to the `*compilation*` buffer and press
`g`. Compilation mode recognizes Clang errors and warnings, so `M-g n` and
`M-g p` move between source locations.

The current test validates that the generated XM can be parsed and rendered by
the same XM replay library raylib uses. It is deliberately quick enough to run
after every build.

To build and immediately run the demo, use `C-x p c`, edit the command in the
minibuffer to the following, and press `RET`:

```sh
cmake --build build -j 8 && ./build/boat_study
```

Because the demo opens a native window, stop it with `Esc`, not by killing its
compilation buffer. The next `M-x recompile` repeats the build-and-run command;
use `C-x p c` again when you want to switch back to the build-and-test command.

## Formatting

The checked-in `.clang-format` uses a compact WebKit-derived C style: four-space
indentation, function braces on the following line, attached control-statement
braces, no hard tabs, and a 100-column limit. The checked-in `.dir-locals.el`
also sets `c-ts-mode` to its BSD-style indentation rules with a four-space
offset. Classic `c-mode` gets the same four-space offset as a fallback.

After changing branches or pulling changes to `.dir-locals.el`, reopen the file
or run `M-x normal-mode` so Emacs reloads the project settings. In a C buffer,
`M-: c-ts-mode-indent-offset RET` should then display `4` (or use
`c-basic-offset` if the buffer uses classic `c-mode`). Emacs indentation handles
new lines while clang-format remains the final authority for complete-file
layout.

Format the current buffer through clangd with:

```text
M-x eglot-format-buffer
```

To format C buffers automatically before saving, add:

```elisp
(add-hook 'c-ts-mode-hook
          (lambda ()
            (add-hook 'before-save-hook #'eglot-format-buffer nil t)))
```

To check all hand-written C files without modifying them:

```sh
clang-format --dry-run --Werror src/*.c tools/*.c
```

To apply formatting to all of them:

```sh
clang-format -i src/*.c tools/*.c
```

## Navigation and documentation

Eglot exposes clangd's index through the standard Emacs interfaces:

| Key or command | Action |
| --- | --- |
| `M-.` | Jump to a definition |
| `M-,` | Return to the previous location |
| `M-?` | Find references |
| `C-M-.` | Find an identifier using completion |
| `M-x xref-find-apropos` | Search project symbols |
| `M-x eldoc-doc-buffer` | Show type and documentation information |
| `M-x imenu` | Jump to a function in the current file |

Useful structural movement remains the same as in Maldives:

| Key | Action |
| --- | --- |
| `C-M-a` | Beginning of the current function |
| `C-M-e` | End of the current function |
| `C-M-f` | Forward across a balanced expression |
| `C-M-b` | Backward across a balanced expression |
| `C-M-u` | Outward to the enclosing expression |
| `C-M-d` | Into the following expression |

## Project and buffer navigation

| Key | Action |
| --- | --- |
| `C-x p f` | Find a project file |
| `C-x p g` | Search project files with grep |
| `C-x p b` | Switch project buffers |
| `C-x p p` | Switch projects |
| `C-x b` | Switch buffers |
| `C-x C-b` | List buffers |

The `project-vc-extra-root-markers` setting makes `CMakeLists.txt` identify the
project. Once the directory is a Git repository, the normal VC detection also
works.

## Diagnostics and refactoring

Eglot publishes clangd diagnostics through Flymake:

| Key or command | Action |
| --- | --- |
| `M-g n` | Next diagnostic or compilation error |
| `M-g p` | Previous diagnostic or compilation error |
| `M-x flymake-show-buffer-diagnostics` | List diagnostics for this buffer |
| `M-x flymake-show-project-diagnostics` | List project diagnostics |
| `M-x eglot-code-actions` | Show available fixes and refactorings |
| `M-x eglot-rename` | Rename a symbol across the project |
| `M-x eglot-inlay-hints-mode` | Toggle parameter hints |

Generated raylib sources live under `build/_deps`. They may emit diagnostics
while compiling, but they are not Proteus source and should normally be left
alone.

## Debug with LLDB

Configure a debug build and compile it:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
```

Then run:

```text
M-x lldb RET build/boat_study RET
```

Useful GUD keys are:

| Key | Action |
| --- | --- |
| `C-x C-a C-b` | Set a breakpoint at the current source line |
| `C-x C-a C-r` | Continue after stopping |
| `C-x C-a C-n` | Step over |
| `C-x C-a C-s` | Step into |
| `C-x C-a C-f` | Finish the current function |
| `C-x C-a C-p` | Print the expression at point |

You can also type LLDB commands such as `breakpoint set --name draw_demo`, `run`,
`frame variable`, and `bt` directly in the GUD buffer. CMake copies `assets`
beside the executable, so starting it from `build` still finds the XM file.
