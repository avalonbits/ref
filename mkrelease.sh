#!/bin/bash
# Builds a release, locally: nothing is published.
#
#   ref-<version>.zip           unzip at the root of the SD card:
#       bin/ref.bin                 the viewer
#       bin/md2ref.bin              Markdown into documents
#       config/ref/LICENSE          ref's and md2ref's: GPL version 3 only
#       config/ref/docs/mos.dat     agon-docs at the commit in SOURCE
#       config/ref/docs/ref.dat     ref's own book: `ref ref`
#       config/ref/docs/VERSION     ref's version, the commit it was built
#                                   from, and the agon-docs commit
#   ref-builder-<version>.zip   the builder, to make mos.dat on a PC or Mac:
#       SOURCE, build/, LICENSE
#
# mos.dat is the wiki's own text, and agon-docs has no license yet: do not
# publish the first zip until its maintainers have settled one. The script
# refuses unless REF_LICENSE_SETTLED=1 says that has happened.
#
# Usage: ./mkrelease.sh [output directory]   (default: .)
set -euo pipefail
cd "$(dirname "$0")"

if [ "${REF_LICENSE_SETTLED:-}" != 1 ]; then
    echo "mkrelease: agon-docs has no license yet; mos.dat cannot be released." >&2
    echo "mkrelease: set REF_LICENSE_SETTLED=1 once its maintainers have settled one." >&2

    exit 1
fi
VERSION=$(sed -n 's/^#define REF_VERSION "\(.*\)"/\1/p' src/viewer/version.h)
DEST=$(cd "${1:-.}" && pwd)
command -v zip >/dev/null || { echo "mkrelease: zip is not installed" >&2; exit 1; }

# Built fresh, and tested: a release from a stale binary is found by a user.
make -s dat book agon agon-md2ref test >/dev/null

COMMIT=$(git rev-parse HEAD 2>/dev/null || echo unknown)
if [ "$COMMIT" != unknown ] && ! git diff --quiet HEAD 2>/dev/null; then
    COMMIT="$COMMIT-dirty"
fi
DOCS=$(sed -n 's/^commit //p' SOURCE)

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/card/bin" "$STAGE/card/config/ref/docs" "$STAGE/builder"
cp bin/ref.bin bin/md2ref/md2ref.bin "$STAGE/card/bin/"
cp LICENSE "$STAGE/card/config/ref/"
cp out/mos.dat out/ref.dat "$STAGE/card/config/ref/docs/"
printf 'ref %s\ncommit %s\nagon-docs %s\n' "$VERSION" "$COMMIT" "$DOCS" > "$STAGE/card/config/ref/docs/VERSION"
cp -r SOURCE build LICENSE "$STAGE/builder/"
rm -rf "$STAGE/builder/build/cache"
find "$STAGE/builder" -name __pycache__ -prune -exec rm -rf {} +

(cd "$STAGE/card" && zip -qr "$DEST/ref-$VERSION.zip" .)
(cd "$STAGE/builder" && zip -qr "$DEST/ref-builder-$VERSION.zip" .)
echo "$DEST/ref-$VERSION.zip"
echo "$DEST/ref-builder-$VERSION.zip"
