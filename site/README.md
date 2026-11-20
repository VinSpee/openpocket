# The site container

`site/Dockerfile` builds the site of `undertone.vinspee.me`: a small container with a static file server. It serves
two things.

- **The pages of this commit.** Everything in `docs/`: the installer (`index.html`), the editor (`editor.html`) and
  the documents. A correction to a page reaches the site through an ordinary pull request, without a release.
- **The files of one pinned release.** The packages and whatever else that release publishes for the site. The build
  downloads them from the GitHub release and checks each one against a SHA-256 written in this folder.

The packages must come from the site's own address. The installer reads them with `fetch`, and GitHub does not allow
a page of another site to read its release downloads.

The firmware is never built here. The toolchain runs on x86-64 only, and the server that runs the site is arm64.

## The parts

| File | What it is |
| --- | --- |
| `Dockerfile` | The build: the pages, then the release's files, then the file server. The build context is the top of the repository |
| `Dockerfile.dockerignore` | Keeps everything but `docs/` and `site/` out of the build, and `docs/firmware/` too, where a local build leaves its packages |
| `Caddyfile` | The file server, on port 80 without HTTPS. The proxy in front of the container provides HTTPS |
| `RELEASE` | One line: the tag of the pinned release, or `none` |
| `release.sha256` | One line per file of that release: its SHA-256, its path on the site, the name of its release asset |
| `release.sh` | The step that downloads the files and checks them |

The container also serves `RELEASE` and `release.sha256` themselves, at `/RELEASE` and `/release.sha256`, so that
anyone can see which release a running site holds.

## Pinning a release

No release is pinned yet: `RELEASE` says `none`, and the site serves the pages without any package. The installer
then has nothing to install.

To put a published release on the site, open a pull request that changes two files:

1. `RELEASE`: the release's tag, for example `v1.0.0-beta.1`.
2. `release.sha256`: one line for each file the site should serve.

```
<SHA-256>  <path on the site>  <name of the release asset>
```

The path says where the page will ask for the file, for example `firmware/manifest.json`. The asset name is the
file's name in the GitHub release. The two are separate because a release's assets have no folders. Take each SHA-256
from the build that made the release, not from a download of the release.

The build stops, and nothing is deployed, when a download fails, when a file is not the pinned bytes, when a path is
already a page of the site, and when the two files disagree about whether a release is pinned. Going back to the
previous release is reverting the pull request.

## Building and checking

```
docker build -f site/Dockerfile -t openpocket-site .          # from the top of the repository
docker run --rm -p 8080:80 openpocket-site                    # http://localhost:8080/
sh .claude/skills/verify/scripts/pages.sh site                # the check
```

The check is `tests/test_site.mjs`, and the `site` job of `.github/workflows/build.yml` runs it on every pull request.
It needs Docker and Playwright. It does three things:

1. It builds the image as it will be deployed and reads the pages and the pinned files back from a running container.
2. It builds the image again with a stand-in release, which is the test's fixture packages on a local server, given to
   the build with `--build-arg RELEASE_URL=...`. Each file must arrive as pinned, and six wrong pins must stop the build.
3. It runs the installer and editor page tests against the container. The installer test needs packages that fit its
   simulated pedal, so the container's `firmware/` folder is the test's own folder for that run.

## Deploying

Coolify builds the container from the public repository. It needs these settings and no secret:

| Setting | Value |
| --- | --- |
| Repository | `https://github.com/VinSpee/undertone`, branch `main` |
| Build pack | Dockerfile |
| Base directory | `/` |
| Dockerfile location | `/site/Dockerfile` |
| Port | 80 |

The build needs to reach Docker Hub for the base image and `github.com` for the release's files.

## What was tested

Tested on an arm64 Mac: the check above passes; and a build with upstream's `v42` release pinned downloaded
`BlackBox_942.fwsc` from GitHub and served the same bytes.

Not tested: the build on Coolify, and the site behind its proxy and Cloudflare. The download from a release of this
repository is not tested either, because the repository has no release yet.
