ARG UBUNTU_VERSION=22.04

FROM ubuntu:$UBUNTU_VERSION AS build

RUN apt-get update && \
    apt-get install -y build-essential git cmake libcurl4-openssl-dev

WORKDIR /app

COPY . .

RUN cmake -S . -B build -DGGML_BACKEND_DL=ON -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=ON -DLLAMA_CURL=ON -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j $(nproc) && \
    mkdir -p /app/lib && \
    find build -name "*.so" -exec cp {} /app/lib/ \;

FROM ubuntu:$UBUNTU_VERSION AS runtime

WORKDIR /app

RUN apt-get update && \
    apt-get install -y libcurl4-openssl-dev libgomp1 curl

COPY --from=build /app/build/bin/llama-cli /app/
COPY --from=build /app/lib/ /app/

ENV LC_ALL=C.utf8

ENTRYPOINT [ "/app/llama-cli" ]
