#!/usr/bin/env bash
# Build the CowEngine backend images locally and ship them to a remote host
# (e.g. an OVHcloud VPS). Nothing is compiled on the VPS — it only needs Docker.
#
#   deploy/deploy.sh ubuntu@game.cowengine.com
#   deploy/deploy.sh --registry ghcr.io/bunny ubuntu@vps          # via a registry
#   deploy/deploy.sh --no-build --tag v3 ubuntu@vps               # reship a tag
#   deploy/deploy.sh --control-only ubuntu@vps                   # partial update
#
# Registry-less mode (the default) streams `docker save` over the SSH connection
# you already have; no registry account, no credentials on the VPS.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE_DIR="${REMOTE_DIR:-/opt/cowengine}"
TAG="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo latest)"
REGISTRY=""
BUILD=1
WANT_CONTROL=1
WANT_SIDECAR=1
TARGET=""

die() { echo "deploy: $*" >&2; exit 1; }
say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --registry)     REGISTRY="${2:?--registry needs a prefix, e.g. ghcr.io/you}"; shift 2 ;;
        --tag)          TAG="${2:?--tag needs a value}"; shift 2 ;;
        --remote-dir)   REMOTE_DIR="${2:?--remote-dir needs a path}"; shift 2 ;;
        --no-build)     BUILD=0; shift ;;
        --control-only|--server-only) WANT_SIDECAR=0; shift ;;
        --sidecar-only|--sidecar) WANT_CONTROL=0; shift ;;
        -h|--help)      sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*)             die "unknown flag $1" ;;
        *)              TARGET="$1"; shift ;;
    esac
done
[[ -n "$TARGET" ]] || die "usage: deploy/deploy.sh [flags] user@host"

PREFIX="${REGISTRY:+$REGISTRY/}"
CONTROL_IMAGE="${PREFIX}cowengine-control:${TAG}"
SIDECAR_IMAGE="${PREFIX}cowengine-sidecar:${TAG}"

IMAGES=()
(( WANT_CONTROL ))  && IMAGES+=("$CONTROL_IMAGE")
(( WANT_SIDECAR )) && IMAGES+=("$SIDECAR_IMAGE")
[[ ${#IMAGES[@]} -gt 0 ]] || die "nothing selected to deploy"

# ---- 1. build -------------------------------------------------------------
if (( BUILD )); then
    # linux/amd64 explicitly: OVH VPS instances are x86_64, and this keeps the
    # script honest if it's ever run from an arm64 laptop.
    (( WANT_CONTROL )) && {
        say "building $CONTROL_IMAGE"
        docker build --platform linux/amd64 \
            -f "$REPO_ROOT/deploy/Dockerfile.control" -t "$CONTROL_IMAGE" "$REPO_ROOT"
    }
    (( WANT_SIDECAR )) && {
        say "building $SIDECAR_IMAGE"
        docker build --platform linux/amd64 \
            -f "$REPO_ROOT/deploy/Dockerfile.sidecar" -t "$SIDECAR_IMAGE" "$REPO_ROOT"
    }
fi

# ---- 2. transfer ----------------------------------------------------------
if [[ -n "$REGISTRY" ]]; then
    say "pushing to $REGISTRY"
    for img in "${IMAGES[@]}"; do docker push "$img"; done
else
    # `docker load` auto-detects gzip; -1 keeps the CPU cost low since the
    # bottleneck is usually the uplink, not compression.
    say "streaming $(printf '%s ' "${IMAGES[@]}")to $TARGET"
    docker save "${IMAGES[@]}" | gzip -1 | ssh "$TARGET" 'docker load'
fi

# ---- 3. install compose files + restart -----------------------------------
say "updating $TARGET:$REMOTE_DIR"
ssh "$TARGET" "mkdir -p '$REMOTE_DIR'"
scp "$REPO_ROOT/deploy/docker-compose.prod.yml" \
    "$REPO_ROOT/deploy/Caddyfile" \
    "$REPO_ROOT/deploy/.env.example" \
    "$REPO_ROOT/deploy/install-certs.sh" \
    "$TARGET:$REMOTE_DIR/"
ssh "$TARGET" "chmod +x '$REMOTE_DIR/install-certs.sh'"

# .env holds host-specific secrets (TLS_DIR, join key) — seed it once, then only
# rewrite the image pins so a redeploy never clobbers the operator's settings.
rc=0
# ssh joins multiple command-line args with plain spaces before the remote
# shell re-splits them, so an empty arg (REGISTRY, by default) just vanishes
# instead of arriving as ''. Pre-quote with %q and pass one string instead.
remote_args="$(printf '%q ' "$REMOTE_DIR" "$CONTROL_IMAGE" "$SIDECAR_IMAGE" \
                           "$WANT_CONTROL" "$WANT_SIDECAR" "$REGISTRY")"
ssh "$TARGET" "bash -s -- $remote_args" <<'REMOTE' || rc=$?
set -euo pipefail
dir="$1"; control_image="$2"; sidecar_image="$3"
want_control="$4"; want_sidecar="$5"; registry="$6"
cd "$dir"

if [[ ! -f .env ]]; then
    cp .env.example .env
    echo "deploy: seeded $dir/.env from the example — review TLS_DIR + COW_* and rerun" >&2
    echo "deploy: NOT starting the stack until .env is reviewed" >&2
    exit 3
fi

pin() {  # pin KEY VALUE — replace or append in .env
    if grep -q "^$1=" .env; then
        sed -i "s|^$1=.*|$1=$2|" .env
    else
        printf '%s=%s\n' "$1" "$2" >> .env
    fi
}
[[ "$want_control" == 1 ]] && pin COW_CONTROL_IMAGE "$control_image"
[[ "$want_sidecar" == 1 ]] && pin COW_SIDECAR_IMAGE "$sidecar_image"

[[ -n "$registry" ]] && docker compose -f docker-compose.prod.yml pull
docker compose -f docker-compose.prod.yml up -d

# Control owns room processes now; if it changes, bounce the sidecar so its
# control-plane connection is rebuilt against the fresh API.
if [[ "$want_control" == 1 ]]; then
    docker compose -f docker-compose.prod.yml restart sidecar
fi

docker compose -f docker-compose.prod.yml ps
REMOTE

if [[ $rc -eq 3 ]]; then
    say "images are on the host; finish setup then rerun"
    echo "  ssh $TARGET 'nano $REMOTE_DIR/.env'   # check TLS_DIR + COW_* first"
    exit 3
elif [[ $rc -ne 0 ]]; then
    die "remote step failed (exit $rc)"
fi

say "deployed ${IMAGES[*]}"
echo "logs:  ssh $TARGET 'cd $REMOTE_DIR && docker compose -f docker-compose.prod.yml logs -f sidecar'"
