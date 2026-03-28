#!/bin/bash
# Generate or update debian/changelog for libnemo-normalize
#
# Usage:
#   scripts/generate-changelog.sh [VERSION] [URGENCY]
#
# Examples:
#   scripts/generate-changelog.sh                  # uses version from debian/changelog
#   scripts/generate-changelog.sh 0.2.0 medium     # bump to 0.2.0
#   scripts/generate-changelog.sh 1.0.0 low        # release 1.0.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CHANGELOG="$PROJECT_DIR/debian/changelog"

PACKAGE="libnemo-normalize"
MAINTAINER="Brian West <brian@bkw.org>"
DISTRIBUTION="stable"

VERSION="${1:-}"
URGENCY="${2:-medium}"

if [ -z "$VERSION" ]; then
    if [ -f "$CHANGELOG" ]; then
        VERSION=$(head -1 "$CHANGELOG" | sed -n 's/.*(\([^)]*\)).*/\1/p')
        echo "Using existing version: $VERSION"
    else
        VERSION="0.1.0"
        echo "No changelog found, defaulting to version: $VERSION"
    fi
fi

DATE=$(date -R)

# Collect git log entries since last tag (or all if no tags)
LAST_TAG=$(git -C "$PROJECT_DIR" describe --tags --abbrev=0 2>/dev/null || echo "")
if [ -n "$LAST_TAG" ]; then
    CHANGES=$(git -C "$PROJECT_DIR" log --oneline "$LAST_TAG"..HEAD -- . 2>/dev/null || echo "  * No changes since $LAST_TAG")
else
    CHANGES=$(git -C "$PROJECT_DIR" log --oneline -20 -- . 2>/dev/null || echo "  * Initial release")
fi

# Format changes as debian changelog entries
FORMATTED_CHANGES=""
if [ -n "$CHANGES" ]; then
    while IFS= read -r line; do
        # Strip the commit hash prefix
        MSG=$(echo "$line" | sed 's/^[0-9a-f]* //')
        FORMATTED_CHANGES="$FORMATTED_CHANGES  * $MSG\n"
    done <<< "$CHANGES"
else
    FORMATTED_CHANGES="  * Initial release.\n"
fi

cat > "$CHANGELOG" << EOF
$PACKAGE ($VERSION) $DISTRIBUTION; urgency=$URGENCY

$(echo -e "$FORMATTED_CHANGES")
 -- $MAINTAINER  $DATE
EOF

echo "Generated $CHANGELOG for $PACKAGE $VERSION"
