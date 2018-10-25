FROM ubuntu:16.04
MAINTAINER Ketoth Xupack "ketoth.xupack@gmail.com"

ENV TERM xterm
ARG DEBIAN_FRONTEND=noninteractive

RUN set -ex && \
    apt-get update && \
    apt-get install -y \
        dialog \
        apt-utils \
        software-properties-common \
        sudo \
        bison \
        build-essential \
        chrpath \
        cmake \
        devscripts \
        dpatch \
        equivs \
        fakeroot \
        git \
        gnome-common \
        gobject-introspection \
        gtk-doc-tools \
        libappindicator-dev \
        libasound2-dev \
        libdbusmenu-glib-dev \
        liblzma-dev \
        libopus-dev \
        libpulse-dev \
        libssl-dev \
        libunity-dev \
        libva-dev \
        libdrm-dev \
        libvdpau-dev \
        libx11-xcb-dev \
        libxcb-xkb-dev \
        libxkbcommon-dev \
        libicu-dev \
        lintian \
        quilt \
        valac \
        xutils-dev \
        yasm && \
    add-apt-repository ppa:ubuntu-toolchain-r/test && \
    apt-get update && \
    apt-get install -y g++-7 gcc-7 && \
    update-alternatives \
        --install /usr/bin/gcc gcc /usr/bin/gcc-7 60 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-7 \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-7 \
        --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-7 \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-7 && \
    update-alternatives --config gcc && \
    rm -rf /var/lib/apt/lists/* && \
    gcc -v

VOLUME ["/srv/telegram"]

CMD ["/sbin/init"]
