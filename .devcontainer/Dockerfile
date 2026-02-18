FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

# Install build-essential, C++17 compiler (g++), and other dependencies
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive 
    && apt-get -y install --no-install-recommends 
    build-essential 
    curl 
    git 
    python3 
    python3-pip 
    ca-certificates 
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Bazelisk
RUN curl -Lo /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 
    && chmod +x /usr/local/bin/bazel
