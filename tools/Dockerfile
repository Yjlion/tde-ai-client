# Build/run environment for tde-ai-client (used for local builds and screenshots)
ARG CODENAME=trixie
FROM debian:${CODENAME}
ARG CODENAME=trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
 && curl -fsSL -o /tmp/trinity-keyring.deb http://mirror.ppa.trinitydesktop.org/trinity/deb/trinity-keyring.deb \
 && dpkg -i /tmp/trinity-keyring.deb && rm /tmp/trinity-keyring.deb \
 && echo "deb http://mirror.ppa.trinitydesktop.org/trinity/deb/trinity-r14.1.x ${CODENAME} main deps" > /etc/apt/sources.list.d/trinity.list \
 && apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config debhelper devscripts fakeroot lintian \
      tde-cmake tdelibs14-trinity-dev libtqt3-mt-dev tqt3-dev-tools libtqtinterface-dev \
 && rm -rf /var/lib/apt/lists/*
