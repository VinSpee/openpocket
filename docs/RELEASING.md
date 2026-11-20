# Releasing

Release notes are generated with [changesets](https://github.com/changesets/changesets). Each pull request adds a
changeset ([CONTRIBUTING.md](../CONTRIBUTING.md)); a workflow collects them into a version, one changelog per release
unit, a tag and a draft GitHub release. Nothing is published to npm, and nothing is published without a person.

## The parts

| Part | What it is |
| --- | --- |
| `package.json`, `package-lock.json` | The two pinned npm packages of the release tooling. The build and the tests do not use them. The lock file also records the units' version as it was when the lock was written; npm does not compare it, so a release does not have to touch the lock |
| `changelog/<unit>/package.json` | One private stub per release unit: its name, the release version, and the title of its section in the notes. `changeset version` writes `CHANGELOG.md` beside it |
| `.changeset/config.json` | The settings: the base branch `main`, the changelog generator that links pull requests and authors, and the `fixed` group that gives every unit the same version |
| `.changeset/pre.json` | Pre-release mode with the tag `beta` |
| `.github/workflows/changeset.yml` | The `changeset` check on pull requests. Advisory, not required |
| `.github/workflows/release.yml` | The release flow below |
| `.github/scripts/release.mjs` | The step that creates the tag and the draft release. Without `--create` it prints them and changes nothing |

The stubs live in `changelog/` and not beside the sources for three reasons. OpenPocket's sources have no folder of
their own. `tools/` has its own `package.json` and lock file, installed with `npm ci --prefix tools`, which an npm
workspace would change. And a `package.json` in a firmware folder would sit among files the build reads.

## The numbers

Three numbers exist. Changesets own the first one and never touch the other two.

| Number | Example | Where | Who changes it |
| --- | --- | --- | --- |
| The release | `v1.0.0-beta.1` | `changelog/*/package.json`, the tag, the GitHub release | Changesets, in the Version Packages pull request |
| A firmware's version | `v42` for OpenPocket, `v01` for PALETTE | `OP_VERSION_NUM` in `firmware/openpocket.c` and `firmware/apps/palette/palette.c`; on the boot screen | A person, in the pull request that changes that firmware |
| The package identity | `BlackBox_942`, `PocketX_901` | Derived: 900 plus the firmware's version (`tools/pxpkg.py`). The installer tells builds apart by it | It follows the firmware's version |

One release carries several firmwares, and each keeps its own version and identity inside it. The release therefore
needs a number of its own. It is a semantic version, so `v1.0.0-beta.1` cannot be mistaken for a firmware version
such as `v42`, and it cannot collide with upstream's tags `v24` to `v42`.

The first release is a 1.0 beta. The units start at `1.0.0-beta.0`, which is never released. In pre-release mode the
first Version Packages pull request moves them to `1.0.0-beta.1`, the next one to `1.0.0-beta.2`, and so on, whatever
the categories of the changesets are. Leaving pre-release mode gives `1.0.0`. After that the highest category among
the waiting changesets decides the next number, as semantic versioning says.

A changeset does not change a firmware. A pull request that changes a firmware's bytes still moves that firmware's
version and its release pin by hand, as before (`tests/release_pin.sh`).

## Cutting a release

1. Pull requests with changesets merge into `main`. On each push the `release` workflow opens or updates the pull
   request **Version Packages (beta)** from the branch `changeset-release/main`. It holds the new version in every
   `changelog/*/package.json`, the new entries in every `CHANGELOG.md`, and the used changesets moved to
   `.changeset/pre/`. Its description shows the notes as they will be.
2. When the release is wanted, start the checks on that pull request: close it and reopen it. GitHub does not start
   workflows for a pull request that a workflow opened with its own token, so the required `build-and-test` check
   does not appear by itself. A later update of the pull request by the workflow needs the same again.
3. Merge it. The `release` workflow then creates the tag `v<version>` at the merge commit and a **draft** GitHub
   release, marked as a pre-release while the version has a suffix. Its text is the entry of each unit for that
   version, under the unit's title. A unit without changes is left out.
4. Read the draft, add what only a person can say (what was checked on a pedal, the warnings), and publish it. The
   workflow never publishes a release.

The release step runs on every push to `main` that has no changeset waiting. It creates the tag and the draft only
when they are missing, so running it again changes nothing. To see what it would create:
`node .github/scripts/release.mjs`.

To leave the beta: run `npx changeset pre exit` on a branch, merge it, and the next Version Packages pull request
carries `1.0.0`. To use another tag such as `rc`, change `"tag"` in `.changeset/pre.json`.

## Where the firmware assets plug in

The draft release has no files yet. Attaching them is a second job in `.github/workflows/release.yml`:

- `needs: release` and `if: needs.release.outputs.tag != ''`. The `release` job sets that output only in the run that
  created the draft.
- It checks out that tag, builds and tests as the `build` workflow does, compares the outputs with the pins, and
  uploads them to the draft with `gh release upload "$TAG" <files>`. It needs `contents: write` and no secret.

It has to be a job of this workflow. A tag created with the workflow's token does not start a workflow that waits for
`push: tags`.

## Repository settings

- **Settings, Actions, General: "Allow GitHub Actions to create and approve pull requests"** must be on. Without it
  the workflow cannot open the Version Packages pull request. It was off when this was written.
- The workflows use only the token GitHub gives each run. There is no secret to create.
- The `changeset` check is not a required check. To make it one, add it to the branch protection of `main`.
- `.changeset/config.json` names the repository (`VinSpee/undertone`) for the links in the changelogs. Change it
  when the repository is renamed again.

## Adding a unit

Create `changelog/<name>/package.json` like the others, with the current version, and add `<name>` to the `fixed`
group in `.changeset/config.json`. The order of that group is the order of the sections in the notes.

## What was tested

Tested locally, in a copy of the repository: `changeset version` gives `1.0.0-beta.1`, then `1.0.0-beta.2`, then
`1.0.0` after `pre exit`; the changelog generator returns the pull request and the author for a real commit of this
repository; the release step prints the right notes, and with a stand-in for `gh` it creates the tag and the draft
only when they are missing. The `changeset` check ran on its own pull request.

Not tested: the `release` workflow has not run on `main`. No Version Packages pull request, tag or release has been
created.
