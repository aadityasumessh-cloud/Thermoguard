# Build stage
FROM ubuntu:24.04 AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build -j

# Runtime stage: only the two binaries
FROM ubuntu:24.04
COPY --from=build /src/build/plant_controller /usr/local/bin/plant_controller
COPY --from=build /src/build/plantctl /usr/local/bin/plantctl
EXPOSE 5000/tcp
# Bind to all interfaces inside the container so the published port is reachable.
CMD ["plant_controller", "--host", "0.0.0.0", "--tcp-port", "5000", "--udp-host", "127.0.0.1"]
