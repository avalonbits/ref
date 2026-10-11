#!/bin/bash
# Builds a release, locally: nothing is published.
#
#   ref-<version>.zip           unzip at the root of the SD card:
#       bin/ref.bin                 the viewer
#       lib/ref/ref.dat             agon-docs at the commit in SOURCE
#       lib/ref/VERSION             ref's version, the commit it was built
#                                   from, and the agon-docs commit
#   ref-builder-<version>.zip   the builder, to make ref.dat on a PC or Mac:
#       SOURCE, build/
#
# ref.dat is the wiki's own text, and agon-docs has no license yet: do not
# publish the first zip until its maintainers have settled one. The script
# refuses unless REF_LICENSE_SETTLED=1 says that has happened.
#
# Usage: ./mkrelease.sh [output directory]   (default: .)
set -euo pipefail
cd "$(dirname "$0")"

if [ "${REF_LICENSE_SETTLED:-}" != 1 ]; then
    echo "mkrelease: agon-docs has no license yet; ref.dat cannot be released." >&2
    echo "mkrelease: set REF_LICENSE_SETTLED=1 once its maintainers have settled one." >&2

    exit 1
fi
VERSION=$(sed -n 's/^#define REF_VERSION "\(.*\)"/\1/p' src/viewer/version.h)
DEST=$(cd "${1:-.}" && pwd)
command -v zip >/dev/null || { echo "mkrelease: zip is not installed" >&2; exit 1; }

# Built fresh, and tested: a release from a stale binary is found by a user.
make -s dat agon test >/dev/null

COMMIT=$(git rev-parse HEAD 2>/dev/null || echo unknown)
if [ "$COMMIT" != unknown ] && ! git diff --quiet HEAD 2>/dev/null; then
    COMMIT="$COMMIT-dirty"
fi
DOCS=$(sed -n 's/^commit //p' SOURCE)

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/card/bin" "$STAGE/card/lib/ref" "$STAGE/builder"
cp bin/ref.bin "$STAGE/card/bin/"
cp out/ref.dat "$STAGE/card/lib/ref/"
printf 'ref %s\ncommit %s\nagon-docs %s\n' "$VERSION" "$COMMIT" "$DOCS" > "$STAGE/card/lib/ref/VERSION"
cp -r SOURCE build "$STAGE/builder/"
rm -rf "$STAGE/builder/build/cache"
find "$STAGE/builder" -name __pycache__ -prune -exec rm -rf {} +

(cd "$STAGE/card" && zip -qr "$DEST/ref-$VERSION.zip" .)
(cd "$STAGE/builder" && zip -qr "$DEST/ref-builder-$VERSION.zip" .)
echo "$DEST/ref-$VERSION.zip"
echo "$DEST/ref-builder-$VERSION.zip"
