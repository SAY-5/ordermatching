FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        clang cmake make ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -B build -S . -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j && \
    ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim AS final
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 ca-certificates && \
    rm -rf /var/lib/apt/lists/* && \
    useradd -m -u 1001 om
COPY --from=build /src/build/matchcli /usr/local/bin/matchcli
USER om
ENTRYPOINT ["matchcli"]
