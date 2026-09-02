#!/bin/bash

set -e

IMAGE="sankha555/azkaban-ccs:ccs"

RUN_AFTER="no"
SKIP_INSTALL="no"
case "${1:-}" in
    --run)       RUN_AFTER="yes" ;;
    --pull-only) SKIP_INSTALL="yes" ;;
    "")          ;;
    *)           echo "unknown option: $1"
                 echo "usage: $0 [--run | --pull-only]"; exit 1 ;;
esac

# Everything below needs root. If we are not root already we go through sudo;
# as a plain variable so the same command lines work in both cases.
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
elif command -v sudo > /dev/null; then
    SUDO="sudo"
else
    echo "this script needs to be run as root, or with sudo installed."
    exit 1
fi

# ---------------------------------------------------------------
# 1. install Docker
# ---------------------------------------------------------------
if [ "$SKIP_INSTALL" = "yes" ]; then
    echo "=== Skipping the Docker install (--pull-only) ==="
elif command -v docker > /dev/null && case "$(command -v docker)" in /snap/*) false ;; *) true ;; esac; then
    echo "=== Docker is already installed, skipping the install ==="
    docker --version
elif command -v docker > /dev/null; then
    # The Canonical snap runs its own runc under a strict AppArmor profile that
    # denies the sysctl write Docker performs for every container, so containers
    # fail to start with "ip_unprivileged_port_start ... permission denied".
    # The official packages are needed instead; removing the snap also removes
    # any images it holds, so that is left to the reviewer to do deliberately.
    echo "Docker is installed as a snap ($(command -v docker)), which cannot run"
    echo "this image: its confinement makes container startup fail with"
    echo "    ... ip_unprivileged_port_start ... permission denied"
    echo ""
    echo "Remove it and re-run this script to get the official packages:"
    echo ""
    echo "    sudo snap remove docker      # NB: deletes the snap's images"
    echo "    $0 ${1:-}"
    exit 1
else
    echo "=== Installing Docker ==="

    # the apt repository differs between Ubuntu and Debian
    if [ ! -r /etc/os-release ]; then
        echo "cannot read /etc/os-release, so the distribution is unknown."
        echo "install Docker by hand: https://docs.docker.com/engine/install/"
        exit 1
    fi
    . /etc/os-release

    case "$ID" in
        ubuntu) DISTRO="ubuntu"; CODENAME="$VERSION_CODENAME" ;;
        debian) DISTRO="debian"; CODENAME="$VERSION_CODENAME" ;;
        # derivatives (Linux Mint, Pop!_OS, ...) name their upstream release here
        *)      if [ "${ID_LIKE#*ubuntu}" != "$ID_LIKE" ]; then
                    DISTRO="ubuntu"; CODENAME="${UBUNTU_CODENAME:-$VERSION_CODENAME}"
                elif [ "${ID_LIKE#*debian}" != "$ID_LIKE" ]; then
                    DISTRO="debian"; CODENAME="$VERSION_CODENAME"
                else
                    echo "this script only handles Ubuntu and Debian, but found: $ID"
                    echo "install Docker by hand: https://docs.docker.com/engine/install/"
                    exit 1
                fi ;;
    esac
    echo "detected $DISTRO ($CODENAME)"

    $SUDO apt-get update
    $SUDO apt-get install -y ca-certificates curl

    # Docker's signing key
    $SUDO install -m 0755 -d /etc/apt/keyrings
    $SUDO curl -fsSL "https://download.docker.com/linux/$DISTRO/gpg" \
          -o /etc/apt/keyrings/docker.asc
    $SUDO chmod a+r /etc/apt/keyrings/docker.asc

    # ... and the repository itself
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/$DISTRO $CODENAME stable" \
        | $SUDO tee /etc/apt/sources.list.d/docker.list > /dev/null

    $SUDO apt-get update
    $SUDO apt-get install -y docker-ce docker-ce-cli containerd.io \
          docker-buildx-plugin docker-compose-plugin

    echo "install done."
    docker --version

    # on a systemd machine the daemon is started by the package, but not in a
    # container or WSL, so nudge it if it is not running
    if ! $SUDO docker info > /dev/null 2>&1; then
        echo "the daemon is not up yet, starting it."
        if command -v systemctl > /dev/null; then
            $SUDO systemctl enable --now docker
        else
            $SUDO service docker start
        fi
    fi

    echo ""
    echo "=== Checking the install with hello-world ==="
    $SUDO docker run --rm hello-world > /dev/null && echo "docker works."

    # let the current user run docker without sudo from the next login onwards
    if [ -n "$SUDO" ] && ! id -nG "$USER" | grep -qw docker; then
        echo ""
        echo "adding $USER to the docker group (takes effect at the next login)."
        $SUDO usermod -aG docker "$USER"
    fi
fi

# ---------------------------------------------------------------
# 2. pull the artifact image
# ---------------------------------------------------------------
echo ""
echo "=== Pulling $IMAGE ==="
echo "(a few GB, so this takes a while)"
$SUDO docker pull "$IMAGE"

echo ""
echo "image pulled:"
$SUDO docker images "${IMAGE%:*}"

# ---------------------------------------------------------------
# 3. run the experiments
# ---------------------------------------------------------------
mkdir -p results

if [ "$RUN_AFTER" = "yes" ]; then
    echo ""
    echo "=== Running the quick experiments ==="
    $SUDO docker run --rm --cap-add=NET_ADMIN \
        -v "$PWD/results:/artifact/results" "$IMAGE" --quick
    echo ""
    echo "results are in $PWD/results"
else
    echo ""
    echo "All set. To run the experiments:"
    echo ""
    echo "    ${SUDO:+sudo }docker run --rm --cap-add=NET_ADMIN \\"
    echo "        -v \"\$PWD/results:/artifact/results\" \\"
    echo "        $IMAGE --quick"
    echo ""
    echo "use --all instead of --quick for the full sweep (hours)."
    echo "NET_ADMIN is required: the proof-cost experiment shapes the"
    echo "container's loopback to 1 Gbit with tc."
fi
