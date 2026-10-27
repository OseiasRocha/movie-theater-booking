# syntax=docker/dockerfile:1
FROM dhi.io/alpine-base:3.23-dev AS dependencies

USER root
# libuuid's configure checks also require Linux kernel headers.
RUN apk add --no-cache build-base cmake ninja python3 py3-pip perl libatomic linux-headers
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:${PATH}"
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Use Alpine's CMake instead of a glibc-based Conan tool binary.
RUN printf '[platform_tool_requires]\ncmake/%s\n' \
       "$(cmake --version | head -n 1 | cut -d ' ' -f 3)" > /tmp/system-cmake
COPY conanfile.py ./
# Never download glibc binaries into this musl cache. Keep installed packages in
# the layer so later compilation does not depend on the mutable cache mount.
ARG TARGETARCH
RUN --mount=type=cache,id=movie-theater-conan-alpine-3.23-${TARGETARCH},target=/root/.conan2,sharing=locked \
    conan profile detect --force \
    && conan remote update conancenter --recipes-only \
    && conan install . --build=missing --deployer=full_deploy \
      -pr:h default -pr:h /tmp/system-cmake \
      -pr:b default -pr:b /tmp/system-cmake \
      -s:a build_type=Release -s:a compiler.cppstd=20 \
      -o "*:shared=False"

FROM dependencies AS build
COPY CMakeLists.txt ./
COPY inc/ inc/
COPY src/ src/
COPY tests/ tests/
COPY docs/ docs/
COPY config/ config/
# Container builds favor unity compilation; native builds keep per-file clangd commands.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_UNITY_BUILD=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure --no-tests=error

FROM dhi.io/alpine-base:3.23 AS runtime
WORKDIR /app
COPY --from=build /usr/lib/libstdc++.so.6* /usr/lib/libgcc_s.so.1 /usr/lib/libatomic.so.1* /usr/lib/
COPY --from=build /app/build/movie-theater-booking /usr/local/bin/movie-theater-booking
COPY --from=build /app/config/ ./config/
USER 65532:65532
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/movie-theater-booking"]
