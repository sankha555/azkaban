# ---------------------------------------------------------------------------
# Azkaban artifact image.
#
# Build (on an x86-64 host; ~20 min, mostly ELINA and the python wheels):
#     docker build --platform linux/amd64 -t azkaban:ccs .
#
# Run (NET_ADMIN is required: the proof-cost experiment shapes the container's
# own loopback to 1 Gbit with tc):
#     docker run --rm --cap-add=NET_ADMIN -v "$PWD/results:/artifact/results" \
#         azkaban:ccs --quick
#
# Everything is compiled for a portable x86-64 baseline (see AZKABAN_ARCH_FLAGS
# below), not -march=native, so the image runs on any x86-64 CPU with AES-NI,
# AVX2 and RDSEED -- i.e. Intel Broadwell / AMD Excavator (2015) or newer.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04

# Portable CPU baseline. The x86-64-v2 level does not include AES-NI, PCLMUL,
# AVX2 or RDSEED, all of which emp-toolkit uses (emp-tool's f2k GF(2^128)
# multiply needs PCLMUL), so they are requested explicitly on top of it.
ARG AZKABAN_ARCH_FLAGS="-march=x86-64-v2 -maes -mpclmul -mavx2 -mrdseed"

ENV DEBIAN_FRONTEND=noninteractive \
    ARTIFACT=/artifact \
    LD_LIBRARY_PATH=/usr/local/lib

# ---------------------------------------------------------------------------
# 1. System packages
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git wget curl ca-certificates m4 \
        libssl-dev libgmp-dev libmpfr-dev libcdd-dev \
        python3 python3-pip python3-venv python3-dev \
        iperf3 iproute2 \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# 2. emp-toolkit, from the copies vendored in this repository
#    (src/emp-tool and src/emp-ot; nothing is fetched from GitHub, so the build
#    is unaffected by upstream changes). They are copied to a scratch directory
#    first, on their own, so editing the project's own sources does not
#    invalidate this layer, and the build tree is thrown away afterwards.
#
#    emp-base.cmake hard-codes -march=native, which would make the compiled
#    binaries crash with SIGILL on an older CPU, so it is patched to
#    the portable baseline before emp-tool is built. Only the library target is
#    built (emp-tool's own test binaries are not needed); emp-ot is header-only,
#    so it is just installed.
# ---------------------------------------------------------------------------
COPY src/emp-tool /tmp/emp/emp-tool
COPY src/emp-ot   /tmp/emp/emp-ot

RUN cd /tmp/emp/emp-tool \
    && sed -i "s|-march=native -maes -mrdseed|${AZKABAN_ARCH_FLAGS}|" cmake/emp-base.cmake \
    && grep -q -- "-march=x86-64-v2" cmake/emp-base.cmake \
    && cmake . -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null \
    && make -j"$(nproc)" emp-tool > /dev/null \
    && cmake --install . > /dev/null \
    && cd /tmp/emp/emp-ot \
    && cmake . -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null \
    && cmake --install . > /dev/null \
    && ldconfig \
    && grep -q -- "-march=x86-64-v2" /usr/local/cmake/emp-base.cmake \
    && rm -rf /tmp/emp

# ---------------------------------------------------------------------------
# 3. The rest of the artifact (see .dockerignore for what is left out)
# ---------------------------------------------------------------------------
WORKDIR ${ARTIFACT}
COPY . ${ARTIFACT}

# ---------------------------------------------------------------------------
# 4. ELINA (built in place: eran/python_helpers loads the .so files from the
#    source tree, relative to eran/ELINA/python_interface)
# ---------------------------------------------------------------------------
# test_deepzono.py's import chain reaches zonoml (DeepZono), fppoly (via
# deeppoly_nodes.py) and fconv (via krelu.py), so the whole C half of ELINA has
# to be built -- hence 'make c' and '-use-deeppoly -use-fconv'. Gurobi is not
# enabled: it only guards the spatial-constraint path in compute_bounds.c,
# which L_infinity certification never reaches.
#
# Two places hard-code -march=native and would make the image crash with SIGILL
# on an older CPU: HAS_NATIVE, which configure writes into Makefile.config, and
# fconv's own CXXFLAGS. Both are rewritten to the portable baseline.
RUN cd ${ARTIFACT}/eran/ELINA \
    && ./configure -prefix /usr/local -use-deeppoly -use-fconv \
                   -cdd-prefix /usr/include/cddlib \
    && sed -i "s|^HAS_NATIVE = .*|HAS_NATIVE = ${AZKABAN_ARCH_FLAGS}|" Makefile.config \
    && sed -i "s|-DNDEBUG -O3 -march=native|-DNDEBUG -O3 ${AZKABAN_ARCH_FLAGS}|" fconv/Makefile \
    && grep -q -- "-march=x86-64-v2" Makefile.config \
    && grep -q -- "-march=x86-64-v2" fconv/Makefile \
    && make -j"$(nproc)" c \
    && make install \
    && ldconfig \
    && test -f zonoml/libzonoml.so \
    && test -f fppoly/libfppoly.so \
    && test -f fconv/libfconv.so

# ---------------------------------------------------------------------------
# 5. Python environment (ERAN)
# ---------------------------------------------------------------------------
RUN python3 -m venv ${ARTIFACT}/venv \
    && ${ARTIFACT}/venv/bin/pip install --no-cache-dir --upgrade pip \
    && ${ARTIFACT}/venv/bin/pip install --no-cache-dir -r ${ARTIFACT}/eran/requirements.txt

# ---------------------------------------------------------------------------
# 6. The C++ executables, built for the portable baseline
# ---------------------------------------------------------------------------
RUN cmake -S ${ARTIFACT} -B ${ARTIFACT}/build \
        -DAZKABAN_ARCH_FLAGS="${AZKABAN_ARCH_FLAGS}" \
    && cmake --build ${ARTIFACT}/build -j"$(nproc)" \
    && ls ${ARTIFACT}/build/bin/robustness ${ARTIFACT}/build/bin/fairness \
          ${ARTIFACT}/build/bin/cleartext_accuracy

# Bind-mount target for the host directory.
RUN mkdir -p ${ARTIFACT}/results

# The experiment scripts source eran/env.sh (scripts/setup.sh writes it on a
# bare host); write the in-image one.
RUN printf '%s\n' \
        '# generated at image build time' \
        'export LD_LIBRARY_PATH="/usr/local/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"' \
        'export PYTHONPATH="/artifact/eran/ELINA/python_interface:/artifact/eran/python_helpers${PYTHONPATH:+:${PYTHONPATH}}"' \
        > ${ARTIFACT}/eran/env.sh

ENTRYPOINT ["/artifact/scripts/run_all.sh"]
CMD ["--quick"]
