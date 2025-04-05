# Stage 1: Build Stage
FROM gcc:latest AS builder

# Set working directory
WORKDIR /cpp

# Copy source files
COPY cpp /cpp

RUN apt-get update && apt-get install -y cmake

# Create a build directory
RUN cmake -B build && cmake --build build --config Release

# Stage 2: Minimal Runtime
FROM ghcr.io/maxotta/kiv-dsa-vagrant-base-docker:latest

# Set working directory
WORKDIR /cpp

# Copy the built binary from the builder stage
COPY --from=builder /cpp/build/helloworld .

#COPY --from=builder /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib64/
#RUN yum install -y libstdc++ 
#RUN yum update -y libstdc++ 

# Set the binary as the entrypoint
CMD ["/cpp/helloworld"]
