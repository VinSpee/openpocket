# The site of undertone.vinspee.me: the installer, the editor and the documents of docs/ at this commit, with the files
# of the one release that site/RELEASE names. Coolify builds it from the public repository; it needs no secret.
# site/README.md says how the release is pinned and how the container is checked.
#   docker build -f site/Dockerfile -t openpocket-site .      (from the top of the repository)
# The base image is pinned by the digest of its multi-platform index: the server that runs the site is arm64, the check
# runs on x86-64.
FROM caddy:2.11.9-alpine@sha256:003a97808e781aa9f7e75cc2f43d34d461099031faf5e20faefb9f61126836f0 AS caddy

# The pages, then the release's files beside them, each checked against its SHA-256 in site/release.sha256.
# site/Dockerfile.dockerignore keeps docs/firmware/ out: the site serves a release's packages, never a checkout's.
FROM caddy AS site
COPY docs/ /site/
COPY site/RELEASE site/release.sha256 site/release.sh /pin/
ARG RELEASE_URL=https://github.com/VinSpee/undertone/releases/download
RUN sh /pin/release.sh /pin /site "$RELEASE_URL" && cp /pin/RELEASE /pin/release.sha256 /site/

FROM caddy
COPY site/Caddyfile /etc/caddy/Caddyfile
COPY --from=site /site/ /srv/
