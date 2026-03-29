FROM ubuntu:24.04 AS base

ARG DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
ARG VCPKG_BINARY_SOURCES=""
ENV VCPKG_BINARY_SOURCES=${VCPKG_BINARY_SOURCES}

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        git \
        ninja-build \
        pkg-config \
        python3 \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN test -x /usr/bin/ninja \
    && ninja --version \
    && cmake --version

RUN cat <<'EOF' >/usr/local/bin/resolve-vcpkg-triplet
#!/bin/sh
set -eu

arch="$(dpkg --print-architecture)"
case "${arch}" in
    amd64)
        echo "x64-linux"
        ;;
    arm64)
        echo "arm64-linux"
        ;;
    *)
        echo "unsupported Debian architecture: ${arch}" >&2
        exit 1
        ;;
esac
EOF
RUN chmod +x /usr/local/bin/resolve-vcpkg-triplet

RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src

COPY CMakeLists.txt vcpkg.json ./
COPY include ./include
COPY src ./src
COPY config.json.example ./config.json

FROM base AS release-build

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_C_COMPILER=/usr/bin/cc \
      -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
      -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET="$(resolve-vcpkg-triplet)" \
    && cmake --build build --parallel \
    && mkdir -p /opt/backend-artifacts/lib \
    && cp build/Backend /opt/backend-artifacts/Backend \
    && cp config.json /opt/backend-artifacts/config.json \
    && cp -a "build/vcpkg_installed/$(resolve-vcpkg-triplet)/lib/." /opt/backend-artifacts/lib

FROM base AS unit-tests

COPY tests ./tests

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_C_COMPILER=/usr/bin/cc \
      -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
      -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTS=ON \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET="$(resolve-vcpkg-triplet)" \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure -R UnitTests

FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libgcc-s1 \
        libssl3 \
        libstdc++6 \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=release-build /opt/backend-artifacts/Backend /app/Backend
COPY --from=release-build /opt/backend-artifacts/config.json /app/config.json
COPY --from=release-build /opt/backend-artifacts/lib /usr/local/lib
RUN ldconfig

ENV BACKEND_CONFIG_PATH=/app/config.json

RUN groupadd --system app \
    && useradd --system --gid app --create-home --home-dir /home/app app \
    && mkdir -p /app/storage/images \
    && chown -R app:app /app

USER app

EXPOSE 8080

CMD ["./Backend"]
