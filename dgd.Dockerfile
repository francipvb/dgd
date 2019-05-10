FROM alpine:3.9 AS base

LABEL author.name="Francisco R. Del Roio"
LABEL author.email="francipvb@hotmail.com"

# Basic configuration
RUN apk add libstdc++ busybox-extras

FROM base AS build

# We need some development tools
RUN apk add bison g++ make

WORKDIR /build
COPY src/ .

# These variables controls build type of DGD
ENV DEFINES="-DLINUX"
ENV DEBUG=""
RUN make -e

FROM base AS final

# Remove apk cache
RUN rm -rf /var/cache/apk
COPY --from=build /build/a.out /bin/dgd

ENTRYPOINT ["/bin/sh"]