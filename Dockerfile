# Dockerfile for building and testing libnemo_normalize
#
# Installs OpenFst + Thrax via conda, compiles the C library and test program,
# then runs the tests against exported FAR grammar files.
#
# Usage:
#   # 1. First export FAR files (one-time, requires nemo-tn-test image):
#   bash export_far.sh
#
#   # 2. Build this image:
#   docker build -t nemo-c-test .
#
#   # 3. Run tests:
#   docker run --rm nemo-c-test

FROM --platform=linux/amd64 continuumio/miniconda3:25.3.1-1

WORKDIR /workspace

# Install build tools and conda packages
RUN echo "deb http://archive.debian.org/debian stretch main contrib non-free" > /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y --reinstall build-essential pkg-config && \
    apt-get clean

# Install thrax (brings OpenFst + headers)
RUN conda install -c conda-forge thrax=1.3.4 -y && \
    conda clean -afy

# Make conda libs visible to the linker
RUN printf "# Conda lib path\n/opt/conda/lib" > /etc/ld.so.conf.d/conda.so.conf && \
    ldconfig

# Set compiler flags for OpenFst/Thrax headers and libs
ENV CONDA_PREFIX=/opt/conda
ENV CPPFLAGS="-I/opt/conda/include"
ENV LDFLAGS="-L/opt/conda/lib"

# Copy library source
COPY nemo_normalize.h nemo_normalize.cpp test_normalize.c Makefile ./

# Copy exported FAR files (must exist from export_far.sh)
COPY far_export/ far_export/

# Build the library and test
RUN make all

# Default: run tests
CMD ["make", "test"]
