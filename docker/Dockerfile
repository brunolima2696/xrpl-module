# syntax=docker/dockerfile:1.19

ARG XRPLD_TOOLCHAIN_IMAGE=ghcr.io/xrplf/xrpld/nix-ubuntu:sha-060957e

FROM ${XRPLD_TOOLCHAIN_IMAGE} AS builder

ARG BUILD_JOBS=4
ARG TEST_SUITES=LoanSet,Vault,Feature,ServerDefinitions
ARG BUILD_TYPE=Debug
ENV CONAN_HOME=/root/.conan2
WORKDIR /workspace/xrpld

COPY . ./

RUN --mount=type=cache,id=xrpld-conan,target=/root/.conan2 \
    --mount=type=cache,id=xrpld-debug-build,target=/workspace/xrpld/.build \
    find . -path './.build' -prune -o -type f \
        \( -name '*.sh' -o -name '*.bash' \) -exec sed -i 's/\r$//' {} + && \
    bash docker/build-pistis.sh image && \
    cp .build/xrpld /tmp/xrpld

FROM ${XRPLD_TOOLCHAIN_IMAGE} AS runtime

ARG VCS_REF=unknown
LABEL org.opencontainers.image.source="https://github.com/brunolima2696/xrpl-module" \
      org.opencontainers.image.revision="${VCS_REF}" \
      org.opencontainers.image.title="PistisLending xrpld"

COPY --from=builder /tmp/xrpld /usr/local/bin/xrpld
COPY docker/xrpld.cfg /etc/xrpld/xrpld.cfg
COPY docker/entrypoint.sh /usr/local/bin/xrpld-entrypoint

RUN sed -i 's/\r$//' /usr/local/bin/xrpld-entrypoint && \
    chmod +x /usr/local/bin/xrpld /usr/local/bin/xrpld-entrypoint && \
    mkdir -p /var/lib/xrpld/nudb /var/lib/xrpld/db /var/log/xrpld

EXPOSE 5005
VOLUME ["/var/lib/xrpld", "/var/log/xrpld"]

ENTRYPOINT ["/usr/local/bin/xrpld-entrypoint"]
