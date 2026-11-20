# Contributing

Building and testing are in [docs/BUILDING.md](docs/BUILDING.md). This page covers the one thing every pull request
carries besides its change: a changeset, which becomes its line in the release notes.

## Add a changeset

A changeset is a small Markdown file in `.changeset/`. It names the release units the change touches, a category for
each, and one sentence for the release notes. Commit it with the change, in the same pull request.

```
npm ci            # once, at the top of the repository (Node 22.11 or newer)
npx changeset     # asks for the units, the category and the sentence, then writes .changeset/<some-name>.md
```

You can also write the file by hand. Any file name ending in `.md` works:

```md
---
"palette": patch
---

The tap tempo light follows the tempo again after a preset change.
```

Write the sentence for someone who plays the pedal, not for someone who reads the code. The pull request's number and
its author are added to the line automatically.

## Which unit

The release units are the folders of `changelog/`. Each has its own changelog and its own section in the release
notes. A change that touches two units names both in one changeset and appears under both.

| Unit | Section in the notes | Its files |
| --- | --- | --- |
| `openpocket` | OpenPocket firmware | `firmware/openpocket.c` and its headers at the top of `firmware/`, `firmware/ble/`, `engine/`, the web editor (`docs/editor.html` and its scripts) |
| `palette` | PALETTE firmware | `firmware/apps/palette/`, `docs/PALETTE*.md` |
| `base` | Base firmware | `firmware/base/`, `firmware/bsp/`, `firmware/sdk/`, `firmware/apps/starter/`, `firmware/build.sh`, `loader/`, the packager (`tools/pxpkg.py`) |
| `tools` | Installer, rescue and tools | the web installer (`docs/index.html` and its scripts), `tools/flash.mjs`, the rescue tool, `tools/log.mjs`, the simulator, `tools/op.mjs` |

When a change fits none of them cleanly, pick the unit whose users will notice it.

## Which category

| Category | Heading in the notes | Use it for |
| --- | --- | --- |
| `major` | Major Changes | A change the user must act on or know before installing: saved presets are reset, a pedal or an official firmware generation is no longer accepted, the way to install or to rescue changes |
| `minor` | Minor Changes | Something new: an effect, a control, a screen, a command, support for another pedal |
| `patch` | Patch Changes | A fix, or a change the user does not have to learn |

During the beta the category only decides the heading. The release number counts up as `1.0.0-beta.N` whatever the
categories are ([docs/RELEASING.md](docs/RELEASING.md)).

## A pull request that needs no release note

Tests, the build, the checks, refactors that keep the bytes, and documents for contributors change nothing a user
installs. Say so with an empty changeset:

```
npx changeset --empty
```

By hand, it is a file in `.changeset/` that holds only the two `---` lines.

## The check

The `changeset` check on a pull request fails when the pull request adds no changeset, and when a changeset names a
unit that does not exist. It is not a required check: a pull request can merge without it. Run the same check locally
with `npx changeset status --since=origin/main`.
