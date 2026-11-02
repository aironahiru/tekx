# Packages

A package carries the things a group of documents shares. It can hold
notation, theorem-like environments, macros, and programs that extend tekx
itself. A document takes one in with a single line:

```
use house-notation 1.2
```

Packages don't have to be public. A registry is just a directory or a git
repository of packages, such as a share on the office network or a private
repository. An in-house tool that solves one group's problem can live there
and stay out of public view.

Every package a project uses is pinned in `tekx.lock`, by version, by source
(a git commit or a directory) and by a SHA-256 hash of its content. Every build
checks the hash. A paper can't change because a package changed underneath it.

## Using packages

```bash
tekx pkg registry add house https://git.example.com/house/tekx-packages.git
```

```bash
tekx pkg add house-notation
```

```
added house-base 1.0.2 (sha256:7a1e9e047409…) from https://git.example.com/… at 751e77a497cf
added house-notation 1.3.0 (sha256:5b8e77ceaffe…) from https://git.example.com/… at 751e77a497cf
```

`add` fetches the package and the packages it depends on, installs them in
`.tekx/packages/`, and records them in `tekx.lock` beside the document. Commit
`tekx.lock` with the document. On another machine:

```bash
tekx pkg install
```

That fetches exactly what the lock pins: the same commits, the same content.
It refuses any source whose content no longer matches the lock.

| command | does |
|---|---|
| `tekx pkg add SOURCE` | add a package: SOURCE is a directory, a git URL (`--rev REV`, `--path DIR` for one in a subdirectory), or a name looked up in the registries |
| `tekx pkg install` | install what `tekx.lock` pins, checking every hash |
| `tekx pkg update [NAME]` | fetch the newest content of each source (the default branch of a git source) and lock it |
| `tekx pkg verify` | check the installed packages against `tekx.lock` |
| `tekx pkg lock` | accept changes you made to an installed package; it becomes `local`, so commit `.tekx/packages/NAME` |
| `tekx pkg list` | the locked packages and registries |
| `tekx pkg remove NAME` | remove a package that no other package needs |
| `tekx pkg search [WORD]` | what the registries offer |
| `tekx pkg registry add NAME LOCATION` | a registry for this project, kept in `tekx.lock` |
| `tekx pkg new NAME` | a skeleton for a new package |

Registries are searched in order:
1. the project's, in `tekx.lock`;
2. those in the `TEKX_REGISTRY` environment variable, separated by blanks;
3. those in `~/.config/tekx/registries`, one `NAME LOCATION` per line.

### What a build checks

When a document says `use NAME [VERSION]`, tekx finds the project's
`tekx.lock`, in the document's directory or above it, and then:

- the package must be in the lock;
- it must be installed in `.tekx/packages/NAME`;
- its content must hash to the locked value;
- its version must satisfy the one the document asks for, as a caret range:
  `1.2` accepts 1.2.0 and later 1.x, but not 2.0, and before 1.0 the minor
  version must match;
- it must accept the running tekx (`requires`).

Any failure is one clear error at the `use` line, for example:

```
paper.tekx:2:5: error: package house-notation has changed since it was locked:
`tekx pkg install` restores the locked version, `tekx pkg lock` accepts the change
```

### Packages under development

With no `tekx.lock` above the document, tekx looks for packages in the
directories listed in `TEKX_PATH`, separated by colons. This is for working on
a package before anything is locked.

```bash
TEKX_PATH=~/work/packages tekx paper.tekx
```

## Writing a package

```bash
tekx pkg new house-notation
```

That creates a directory with a manifest and a first file. The manifest,
`package.tekx`:

```
package "house-notation"
version "1.3.0"
summary "House style: notation, environments, names"
requires "tekx 0.2"
depends "house-base 1.0"
provides "main.tekx"
```

| entry | meaning |
|---|---|
| `package` | the name: lowercase letters, digits and `-` |
| `version` | MAJOR.MINOR.PATCH |
| `summary` | one line, shown by `list` and `search` |
| `requires` | the lowest tekx version it works with |
| `depends` | another package and the version needed; may repeat |
| `provides` | a file of definitions taken in by `use` (default `main.tekx`); may repeat |
| `filter` | a program that rewrites the document tree; may repeat |
| `block` | a block name the package's filter implements; may repeat |

A provided file holds ordinary tekx definitions, exactly as a document would
write them:

```
define operator Var, Cov
define E(x) as EE[x] read as "the expected value of {x}"
define postulate like axiom titled "Postulate"
define lab as "the Analytical Engine Laboratory"
```

Errors in these files are reported against the package's own file and line.

## Filters and custom blocks: software inside the pipeline

A filter is any program, in any language. It reads the document tree on its
standard input and writes a document tree to its standard output (Format 2
in [machinery.md](machinery.md)). tekx runs a package's filters after `parse`,
before the mathematics is laid out, so a filter can add or rewrite anything:
sections, paragraphs, formulas (as source, which `eqn` then lays out), figures
or tables.

A package can also declare blocks that only its filter understands:

```
block "callout"
filter "bin/callout"
```

A document can then write:

```
callout "Remember"
  Packages are pinned by hash in tekx.lock.
```

`parse` passes the block to the filter as a raw node: its header arguments,
and the body lines verbatim.

```
custom@10 callout callouts
  args "Remember"
  verbatim@11 Packages are pinned by hash in tekx.lock.
```

The filter replaces it with ordinary content. A custom block left in the tree
is an error, so a missing or broken filter can't silently drop part of a
document. A filter that exits with a failure stops the build. Its standard
error reaches the terminal. `TEKX_PACKAGE_DIR` tells it where its package is
installed.

Filters are programs, and running a document runs them. tekx runs a filter
only from a package whose content matches its locked hash (or from
`TEKX_PATH`, when you develop it), so what runs is exactly what the project
locked.

## The lock file

```
# tekx.lock -- the exact packages this document builds with.
registry house git https://git.example.com/house/tekx-packages.git
package callouts 0.2.0 sha256:cad5… git https://git.example.com/… 751e77a4… callouts
package house-base 1.0.2 sha256:7a1e… git https://git.example.com/… 751e77a4… house-base
package style 0.3.1 sha256:9d1c… dir /srv/tekx/style
```

It is plain text, one line per package, sorted, so it diffs and merges well.

The hash covers every file's path, its executable bit and its content, in
path order. `.git` directories are left out, and symbolic links are refused,
so a package can't point outside itself.
