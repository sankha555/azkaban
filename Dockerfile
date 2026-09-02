# ---------------------------------------------------------------------------
# Azkaban artifact image.
#
# Build (on an x86-64 host; ~40 min, mostly ELINA and the python wheels):
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
ARG GUROBI_VERSION=9.1.2
ARG CDDLIB_VERSION=0.94m

ENV DEBIAN_FRONTEND=noninteractive \
    ARTIFACT=/artifact \
    GUROBI_HOME=/opt/gurobi912/linux64 \
    LD_LIBRARY_PATH=/opt/gurobi912/linux64/lib:/usr/local/lib

# ---------------------------------------------------------------------------
# 1. System packages
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git wget curl ca-certificates m4 \
        libssl-dev libgmp-dev libmpfr-dev \
        python3 python3-pip python3-venv python3-dev \
        iperf3 iproute2 \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# 2. Gurobi runtime (ELINA's fppoly links libgurobi91.so)
#    No licence is needed for the experiments in this artifact: the DeepZono
#    domain never constructs a Gurobi model.
# ---------------------------------------------------------------------------
RUN mkdir -p /opt \
    && curl -fL "https://packages.gurobi.com/9.1/gurobi${GUROBI_VERSION}_linux64.tar.gz" \
         -o /tmp/gurobi.tar.gz \
    && tar -xzf /tmp/gurobi.tar.gz -C /opt \
    && rm /tmp/gurobi.tar.gz \
    && cp "${GUROBI_HOME}/lib/libgurobi91.so" /usr/local/lib/ \
    && ldconfig

# ---------------------------------------------------------------------------
# 3. cddlib (needed by ELINA's fconv)
# ---------------------------------------------------------------------------
RUN cd /tmp \
    && wget -q "https://github.com/cddlib/cddlib/releases/download/${CDDLIB_VERSION}/cddlib-${CDDLIB_VERSION}.tar.gz" \
    && tar -xzf "cddlib-${CDDLIB_VERSION}.tar.gz" \
    && cd "cddlib-${CDDLIB_VERSION}" \
    && ./configure --prefix=/usr/local > /dev/null \
    && make -j"$(nproc)" > /dev/null && make install > /dev/null \
    && ldconfig \
    && rm -rf /tmp/cddlib*

# ---------------------------------------------------------------------------
# 4. emp-toolkit, from the copies vendored in this repository
#    (src/emp-tool and src/emp-ot; nothing is fetched from GitHub, so the build
#    is unaffected by upstream changes). They are copied to a scratch directory
#    first, on their own, so editing the project's own sources does not
#    invalidate this layer, and the build tree is thrown away afterwards.
#
#    emp-base.cmake hard-codes -march=native, which would make the compiled
#    binaries crash with SIGILL on a reviewer's older CPU, so it is patched to
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
# 5. The rest of the artifact (see .dockerignore for what is left out)
# ---------------------------------------------------------------------------
WORKDIR ${ARTIFACT}
COPY . ${ARTIFACT}

# ---------------------------------------------------------------------------
# 6. ELINA (built in place: eran/python_helpers loads the .so files from the
#    source tree, relative to eran/ELINA/python_interface)
# ---------------------------------------------------------------------------
# configure probes the build CPU and writes "HAS_NATIVE = -march=native" into
# Makefile.config; replace it with the portable baseline. fppoly picks Gurobi up
# from $(GUROBI_HOME), which is passed explicitly to make.
RUN cd ${ARTIFACT}/eran/ELINA \
    && ./configure -use-deeppoly -use-fconv -use-gurobi \
    && sed -i "s|^HAS_NATIVE = .*|HAS_NATIVE = ${AZKABAN_ARCH_FLAGS}|" Makefile.config \
    && grep -q "^HAS_NATIVE = -march=x86-64-v2" Makefile.config \
    && make -j"$(nproc)" GUROBI_HOME="${GUROBI_HOME}" > /dev/null \
    && make install GUROBI_HOME="${GUROBI_HOME}" > /dev/null \
    && ldconfig \
    && test -f ${ARTIFACT}/eran/ELINA/fppoly/libfppoly.so

# ---------------------------------------------------------------------------
# 7. Python environment (ERAN)
# ---------------------------------------------------------------------------
RUN python3 -m venv ${ARTIFACT}/venv \
    && ${ARTIFACT}/venv/bin/pip install --no-cache-dir --upgrade pip \
    && ${ARTIFACT}/venv/bin/pip install --no-cache-dir -r ${ARTIFACT}/eran/requirements.txt

# ---------------------------------------------------------------------------
# 8. The C++ executables, built for the portable baseline
# ---------------------------------------------------------------------------
RUN cmake -S ${ARTIFACT} -B ${ARTIFACT}/build \
        -DAZKABAN_ARCH_FLAGS="${AZKABAN_ARCH_FLAGS}" \
    && cmake --build ${ARTIFACT}/build -j"$(nproc)" \
    && ls ${ARTIFACT}/build/bin/robustness ${ARTIFACT}/build/bin/fairness \
          ${ARTIFACT}/build/bin/cleartext_accuracy

# Bind-mount target for the reviewer's host directory.
RUN mkdir -p ${ARTIFACT}/results

# The experiment scripts look for eran/gurobi_env.sh; write the in-image one.
RUN printf '%s\n' \
        '# generated at image build time' \
        'export GUROBI_HOME="/opt/gurobi912/linux64"' \
        'export LD_LIBRARY_PATH="/opt/gurobi912/linux64/lib:/usr/local/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"' \
        > ${ARTIFACT}/eran/gurobi_env.sh

ENTRYPOINT ["/artifact/scripts/run_all.sh"]
CMD ["--quick"]
