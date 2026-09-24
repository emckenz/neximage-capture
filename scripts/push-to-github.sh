#!/usr/bin/env bash
# Push main + tags to GitHub and trigger the Release workflow (tag v0.1.0).
set -euo pipefail

REPO="${GITHUB_REPO:-emckenz/neximage-capture}"
REMOTE="${GIT_REMOTE:-github}"

if ! git remote get-url "$REMOTE" &>/dev/null; then
  git remote add "$REMOTE" "https://github.com/${REPO}.git"
fi

echo "→ Pushing main to ${REMOTE} (${REPO})"
git push -u "$REMOTE" main

if git rev-parse v0.1.0 &>/dev/null; then
  echo "→ Pushing tag v0.1.0 (triggers android-release.yml)"
  git push "$REMOTE" v0.1.0
fi

echo ""
echo "Done. Open Releases:"
echo "  https://github.com/${REPO}/releases/latest"
echo "  https://github.com/${REPO}/releases/tag/v0.1.0"
