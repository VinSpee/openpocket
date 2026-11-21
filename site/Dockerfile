# The site of undertone.vinspee.me: the installer, the editor and the documents of docs/ at this commit, the rescue kit
# built from it, and the files of the one release that site/RELEASE names. Coolify builds it from the public
# repository; it needs no secret.
# site/README.md says how the release is pinned and how the container is checked.
#   docker build -f site/Dockerfile -t openpocket-site .      (from the top of the repository)
# Each base image is pinned by the digest of its multi-platform index: the server that runs the site is arm64, the
# check runs on x86-64.
FROM caddy:2.11.9-alpine@sha256:003a97808e781aa9f7e75cc2f43d34d461099031faf5e20faefb9f61126836f0 AS caddy

# The rescue kit of the installer's Rescue card, built from this commit's sources (rescue/build_kit.py) and checked
# against its pin, rescue/kit.sha256: the build stops when the kit is not the pinned bytes.
FROM python:3.13.18-alpine@sha256:9ef1a599f863231c3d91e941389bfdc4a2d6070557243d54df1a4e57015b960e AS kit
COPY rescue/ /src/rescue/
COPY tools/uboot_rescue.py tools/pocketx_fw.py /src/tools/
COPY RESCUE.bat LICENSE /src/
RUN python3 /src/rescue/build_kit.py /kit && cd /kit && sha256sum -c /src/rescue/kit.sha256

# The pages and the rescue kit, then the release's files beside them, each checked against its SHA-256 in
# site/release.sha256.
# site/Dockerfile.dockerignore keeps docs/firmware/ and docs/rescue/ out: the site serves a release's packages and the
# kit built above, never what a local build left in the checkout.
FROM caddy AS site
COPY docs/ /site/
COPY --from=kit /kit/ /site/rescue/
COPY site/RELEASE site/release.sha256 site/release.sh /pin/
ARG RELEASE_URL=https://github.com/VinSpee/undertone/releases/download
RUN sh /pin/release.sh /pin /site "$RELEASE_URL" && cp /pin/RELEASE /pin/release.sha256 /site/

FROM caddy
COPY site/Caddyfile /etc/caddy/Caddyfile
COPY --from=site /site/ /srv/
