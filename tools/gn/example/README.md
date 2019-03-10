# [GN : Generate Ninja](https://gn.googlesource.com/gn)

## Folders and Files

```bash
sun@sun:~/gn_example$ tree -L 3
.
├── build                                                                       // ???
│   ├── BUILDCONFIG.gn                                                          # set_default...
│   ├── BUILD.gn                                                                # config() {}
│   └── toolchain                                                               // ???
│       └── BUILD.gn                                                            # gcc
├── BUILD.gn                                                                    # targets
├── .gn                                                                         # dotfile
├── hello.cc
├── hello_shared.cc
├── hello_shared.h
├── hello_static.cc
├── hello_static.h
└── README.md

2 directories, 10 files
sun@sun:~/gn_example$
```

```

                                      - //build/BUILD.gn                        # config("compiler_defaults") {} config("executable_ldconfig") {}
                                     /
                                    /
  //.gn <- //build/BUILDCONFIG.gn -<--- //build/toolchain/BUILD.gn              # toolchain("gcc") {tool("cc") {} ...}
                                    \
                                     \
                                      - //BUILD.gn                              # executable() {} shared_library() {} static_library() {}

```

## [GN](https://github.com/Passw/gn_standalone_GFW) <=> [GN docs](https://github.com/Passw/gn_standalone_GFW/tree/master/docs)

- **root**
  + `//.gn`                                                                     (dotfile)
  + `//build/BUILDCONFIG.gn`                                                    (build configuration file which combine below *Three* **BUILD.gn** files)
  + `//build/BUILD.gn`                                                          (parameter options for compiler tools)
  + `//build/toolchain/BUILD.gn`                                                (compiler tools)
  + `//BUILD.gn`                                                                (build or target file)

- **toolchain**
  + os
  + cpu

- three types of toolchain
  1. host
  2. target
  3. current

## Build and Run

1.  `gn gen out/Default`
    ```
    gn_example] -> gn gen out/Default
    Done. Made 3 targets from 4 files in 11ms
    ```

2.  `ninja -C out/Default`
    ```
    gn_example] -> ninja -C out/Default
    ninja: Entering directory `out/Default'
    [6/6] LINK hello
    ```

3.  `out/Default/hello`
    ```
    gn_example] -> out/Default/hello
    Hello, world
    ```

## NOTEs

- Built-in **target types**
  - `executable() {}`
  - `shared_library() {}`
  - `static_library() {}`
  - `loadable_module() {}`
    - like a `shared_library` but loaded at runtime ???
  - `source_set() {}`
    - compiles source files with no intermediate library
  - `group() {}`
    - a named group of targets(deps but no sources)
  - `copy() {}`
  - ...

- `config() {}` is different to `configs`
  - `config() {}` is a **target type**
  - `configs` is a **list**

- `configs` group compiler or linker related **flags** ...
  - Compiler configuration
    - `cflags`
    - `ldflags`
    - `defines`
    - `include_dirs`
      - include directories
  - *No* sources or dependencies
    - ~~sources~~
    - ~~deps~~

- **public_configs**
  - Apply settings to targets that depend on you.
- **public_deps**
  - Forward public configs up the dependency chain.

- Some things the code loads dynamically.
  - **data**
  - **data_deps**

- Get the information about a given target.
  - `gn desc out/Default //:hello deps --tree`
      ```
      gn_example] -> gn desc out/Default //:hello
      Target //:hello
      Type: executable
      Toolchain: //build/toolchain:gcc

      visibility
        *

      testonly
        false

      check_includes
        true

      allow_circular_includes_from

      sources
        //hello.cc

      public
        [All headers listed in the sources are public.]

      configs (in order applying, try also --tree)
        //build:compiler_defaults
        //build:executable_ldconfig

      outputs
        //out/Default/hello

      cflags
        -fPIC
        -pthread

      ldflags
        -Wl,-rpath=$ORIGIN/
        -Wl,-rpath-link=

      Direct dependencies (try also "--all", "--tree", or even "--all --tree")
        //:hello_shared
        //:hello_static
      ```

- Get more information about **flags**, **defines**, **include_dirs**, etc that apply to a target
  - `gn desc out/Default //:hello cflags --blame`

- List all existed targets
  - `gn ls out/Default "//*"`

- How do I depend on that?
  - `gn path out/Default //:hello //:hello_static`

- What references something?
  - `gn refs out/Default //:hello`

- [LLVM project built with GN](https://github.com/llvm/llvm-project/tree/master/llvm/utils/gn)
  1. `cd llvm-project`
  2. ~~`llvm/utils/gn/gn.py gen out/gn`~~
      - ~~`out/gn` is the build directory~~
      - ~~The `gn.py` script adds `--dotfile=llvm/utils/gn/.gn --root=.`~~
      - `gn --dotfile=llvm/utils/gn/.gn --root=. gen out/gn`
  3. `ninja -C out/gn check-lld`

- [Compilation Database](https://gn.googlesource.com/gn/+/master/docs/reference.md#compilation-database)
  - `gn --dotfile=llvm/utils/gn/.gn --root=. --export-compile-commands gen out/gn`
  - `--export-compile-commands`
  - Produces a `compile_commands.json` file in the root of the **build** directory

- GN has an autoformatter
  - `git ls-files '*.gn' '*.gni' | xargs -n 1 gn format`


-----------------------------------------------------------------

This is an example directory structure that compiles some simple targets using
gcc. It is intended to show how to set up a simple GN build.

Don't miss the ".gn" file in this directory!

------------------------------------------------------------------


# [GN's DSL](https://gn.googlesource.com/gn/+/refs/heads/master/docs/reference.md) is ***ugly*** and ***confusing***, better to choose [Bazel](https://github.com/bazelbuild/bazel)

> DSL (Domain Specific Language)

> GN is light and fast, Bazel is heavy and slow. Better to choose **GN** for C/C++ (2019-03-08)

## Key concepts in Bazel

- **workspace**
  - `WORKSPACE`
  - may also be called **project**
  - [Workspace Rules](https://docs.bazel.build/versions/master/be/workspace.html)

- **root**
  - directory contains file `WORKSPACE`

- **package**
  - directory contains file `BUILD`

- **target**
  - rule target
    - `//main:hello`

  - file target
    - Source files
      - `//lib:hello_time.cc`
    - Generated files (sometimes called dervied files)

  - [package groups](https://docs.bazel.build/versions/master/be/functions.html#package_group) is another kind of target
    - two properties
      - list of packages
      - their names
      - *visibility*

- **rule**
  - built-in rule
    - `cc_binary()`
    - `cc_library()`

- **attribute**
  - `srcs`
  - `hdrs`
  - `deps`
  - `visibility`

- [**macro**](https://docs.bazel.build/versions/master/skylark/macros.html)
  - A *function* called from the *BUILD* file that can instantiate.

- [**label**](https://docs.bazel.build/versions/master/build-ref.html#labels)
  - `//path/to/package:target-name`
  - `//main:hello`
  - `//:hello`

  - All targets belong to exactly *one* package.
    - `//my/app/main:app_binary`
      - package name : `my/app/main`
      - target name : `app_binary`

- [**external dependencies**](https://docs.bazel.build/versions/master/external.html)
  - **targets** from other **project / workspace**
  - **dependencies** from other **projects** are called **external dependencies**
  - **external dependencies** are defined in **WORKSPACE** file within the **main project**

  - Depending on other Bazel projects
    1. local filesystem
        - `local_repository()`

            ```
            local_repository(
              name = "coworkers_project",
              path = "path/to/coworkers-project",
            )
            ```

        - reference coworker's target `//foo:bar`
          - `@coworkers_project//foo:bar`
        - **_** (underscore is valid)
        - **-** (dash sign is invalid)

    2. reference a git repository
        - `git_repository()`

    3. download a archived file
        - `http_archive()`

  - Depending on non-Bazel projects
    - `new_local_repository()`
    - `new_git_repository()`
    - `new_http_archive()`
    - *may need to write* **BUILD** *file by yourself*

  - Depending on external packages
    - `maven_jar()`
    - `maven_server()`


## Principles

- [Bazel Design Documents](https://bazel.build/designs/)
- [Best practices for Bazel](https://github.com/bazelbuild/bazel/blob/master/site/docs/best-practices.md)

- separate *big project* into many *packages* and *targets* to ensure ***accurate incremental build***

- two types of file
  - **BUILD**
  - extension file with postfix **.bzl**
    - used to write **rule**s by ourselves
    - `load()` statement

## [Evalution model](https://docs.bazel.build/versions/master/skylark/concepts.html)

- Loading phase
- Analysis phase
- Execution phase

## Relative tools

- [Graphviz](http://www.graphviz.org/)
  - [Graphviz source code](https://gitlab.com/graphviz/graphviz/)
- [WebGraphviz](http://www.webgraphviz.com/)

- `bazel query --nohost_deps --noimplicit_deps 'deps(//main:hello-world)' --output graph`
    ```
    digraph mygraph {
      node [shape=box];
    "//main:hello-world"
    "//main:hello-world" -> "//main:hello-world.cc"
    "//main:hello-world.cc"
    }
    ```

### [query how-to](https://docs.bazel.build/versions/master/query-how-to.html)

- What **packages** exist beneath **src** folder ?
  - `cd $HOME/INSTALL/bazel/latest`
  - `bazel query 'src/...' --output package`

- Bazel's repo structure
  - `cd $HOME/INSTALL/bazel/latest/`
  - `bazel query --nohost_deps --noimplicit_deps 'deps(//src:bazel)' --output graph > ~/graph.dot`
  - `cd ~`
  - `dot -v -Kdot -Tsvg graph.dot -o bazel_graph.svg`
  - `dot -v -Kdot -Tps graph.dot -o bazel_graph.ps`
