# Docker Troubleshooting

## Check Requirements First

|---|---|
| OS | Linux (Ubuntu/Debian tested). The install script handles both. |
| CPU | x86-64 with **AES-NI, PCLMUL, AVX2, RDSEED** — Intel Broadwell / AMD Excavator (2015) or newer. |
| Disk | ~8 GB free (image is 2.8 GB). |
| Privileges | `sudo` access, once, to install Docker and to let the experiment shape the loopback link. |
| Network | To pull the image (~2.8 GB). |

The image is `linux/amd64` only. On ARM see [Troubleshooting §F](#f-non-x86-64-host).

---

## A. `permission denied while trying to connect to the docker API at unix:///var/run/docker.sock`

Your shell is not in the `docker` group. Check with `id`:

```bash
getent group docker   # should list your username
id                    # your *live* shell's groups — may lag behind
```

If `getent` lists you but `id` does not, the group has not reached this session.
Fix, in order of preference:

```bash
sg docker -c '<your docker command>'   # works immediately, per-shell
newgrp docker                          # per-shell
sudo docker ...                        # always works
```

A full logout/login fixes it permanently. Quitting an editor's terminal tab does
not — the terminal inherits groups from the editor process, so quit the editor
entirely.

## B. `error during container init: open sysctl net.ipv4.ip_unprivileged_port_start file: reopen fd N: permission denied`

Your host cannot give containers a private network namespace. This happens
inside LXC/LXD guests, some VPS products, and some CI runners — check with
`systemd-detect-virt`.

Docker writes that sysctl into every private network namespace; in a nested
environment `/proc/sys` belongs to a user namespace the container does not own,
so the write is refused. **Every** container fails, not just this artifact
(`docker run --rm hello-world` fails too).

Add `--network host`:

```bash
docker run --rm --network host --cap-add=NET_ADMIN \
    -v "$PWD/results:/artifact/results" \
    sankha555/azkaban-ccs:ccs --quick
```

There is no host-side fix. Setting the sysctl on the host does not help: it is
not writable there either, and `runc` performs the write regardless of value.
`--security-opt apparmor=unconfined` does not help — the failure is in `runc`
init, before any container LSM profile applies.

> **With `--network host`, the experiment shapes *your machine's* loopback**, not
> a throwaway container one. Anything using localhost is affected during the run,
> and the qdisc is left behind. Clean up afterwards:
> ```bash
> sudo tc qdisc del dev lo root
> ```

## C. `ERROR: could not shape lo to 1gbit` / `RTNETLINK answers: Operation not permitted`

`--cap-add=NET_ADMIN` is missing from your `docker run`. Add it.

The run aborts deliberately — without shaping, proof costs would not be
comparable to the reported numbers.

## D. You are `root`, but still get "permission denied" on the socket

You are in a nested user namespace — "fake root", not real root. Check:

```bash
cat /proc/self/uid_map
```

A map like `0 1001 1` means uid 0 here is really uid 1001 outside, with only one
uid mapped. The socket then shows as `nobody nogroup` and is unreachable; no
Docker configuration can fix this from inside. `exit` that shell and run as your
normal user ([§A](#a-permission-denied-while-trying-to-connect-to-the-docker-api-at-unixvarrundockersock)).

## E. Docker installed as a snap

The Canonical snap runs `runc` under a strict AppArmor profile that blocks
container startup. Remove it and re-run the install script:

```bash
sudo snap remove docker      # NB: deletes the snap's images
./install-docker-and-pull.sh
```

## F. Non-x86-64 host

The image is built for x86-64. Enable emulation, then pin the platform:

```bash
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
docker run --rm --platform linux/amd64 --cap-add=NET_ADMIN \
    -v "$PWD/results:/artifact/results" \
    sankha555/azkaban-ccs:ccs --quick
```

Emulated runs are far slower and timings are **not** comparable to the paper.

## G. `SIGILL` / "Illegal instruction"

Your CPU lacks one of AES-NI, PCLMUL, AVX2 or RDSEED. Verify with
`grep -o -E 'aes|pclmulqdq|avx2|rdseed' /proc/cpuinfo | sort -u`. The artifact
cannot run on such a host.

---

