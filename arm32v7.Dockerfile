##########################
FROM balenalib/raspberrypi3-ubuntu:bionic as build
RUN [ "cross-build-start" ]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get --no-install-recommends install -y \
   wget curl git unzip build-essential cmake pkg-config apt-utils \
   libjpeg-dev libtiff5-dev libpng-dev \
   libavcodec-dev libavformat-dev libswscale-dev libv4l-dev \
   libxvidcore-dev libx264-dev \
   libfontconfig1-dev libcairo2-dev \
   libgdk-pixbuf2.0-dev libpango1.0-dev \
   libgtk2.0-dev libgtk-3-dev \
   libatlas-base-dev gfortran \
   libhdf5-dev libhdf5-serial-dev libhdf5-103 \
   libqtgui4 libqtwebkit4 libqt4-test python3 python3-pyqt5 \
   python3-dev python3-pip time vim file ca-certificates

RUN CMAKE_VERSION=3.15 && \
    CMAKE_BUILD=3.15.0 && \
    curl -L https://cmake.org/files/v${CMAKE_VERSION}/cmake-${CMAKE_BUILD}.tar.gz | tar -xzf - && \
    cd /cmake-${CMAKE_BUILD} && \
    ./bootstrap --parallel=$(grep ^processor /proc/cpuinfo | wc -l) && \
    make -j"$(grep ^processor /proc/cpuinfo | wc -l)" install && \
    rm -rf /cmake-${CMAKE_BUILD}

ENV PAHO_MQTT_HOME=/paho.mqtt
ENV C_INCLUDE_PATH=${PAHO_MQTT_HOME}/include:${C_INCLUDE_PATH}
ENV CPATH=${PAHO_MQTT_HOME}/include:$CPATH
WORKDIR ${PAHO_MQTT_HOME}
RUN git clone https://github.com/eclipse/paho.mqtt.c.git && \
    cd paho.mqtt.c && git checkout v1.3.1 && \
    cmake -Bbuild -H. -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DPAHO_ENABLE_TESTING=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN git clone https://github.com/eclipse/paho.mqtt.cpp && \
    cd paho.mqtt.cpp && \
    cmake -Bbuild -H. -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} -DCMAKE_PREFIX_PATH=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN cp -rf ${PAHO_MQTT_HOME}/lib/* /usr/lib/ && \
    cp -rf ${PAHO_MQTT_HOME}/include/* /usr/include/

ARG OPENCV_VERSION="4.2.0"
ENV OPENCV_VERSION $OPENCV_VERSION
RUN mkdir /tmp/opencv && \
    cd /tmp/opencv && \
    wget -O opencv.zip https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.zip && \
    unzip opencv.zip && \
    wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.zip && \
    unzip opencv_contrib.zip && \
    mkdir /tmp/opencv/opencv-${OPENCV_VERSION}/build && cd /tmp/opencv/opencv-${OPENCV_VERSION}/build && \
    cmake \
    -D CMAKE_BUILD_TYPE=RELEASE \
    -D CMAKE_INSTALL_PREFIX=/usr/local \
    -D OPENCV_EXTRA_MODULES_PATH=/tmp/opencv/opencv_contrib-${OPENCV_VERSION}/modules \
    -D WITH_FFMPEG=YES \
    -D INSTALL_C_EXAMPLES=NO \
    -D INSTALL_PYTHON_EXAMPLES=NO \
    -D BUILD_ANDROID_EXAMPLES=NO \
    -D BUILD_DOCS=NO \
    -D BUILD_TESTS=NO \
    -D BUILD_PERF_TESTS=NO \
    -D BUILD_EXAMPLES=NO \
    -D BUILD_opencv_java=NO \
    -D BUILD_opencv_python=NO \
    -D BUILD_opencv_python2=NO \
    -D BUILD_opencv_python3=NO \
    -D OPENCV_GENERATE_PKGCONFIG=YES .. && \
    make -j$(nproc) && \
    make install && \
    cd && rm -rf /tmp/opencv

RUN ln -s /usr/local/include/opencv4/opencv2/ /usr/local/include/opencv2
WORKDIR /app
ADD ./app /app
RUN cmake -Bbuild -H. -DCMAKE_INSTALL_PREFIX=/app && \
    cmake --build build/ --target install

RUN [ "cross-build-end" ]

##########################
FROM balenalib/raspberrypi3-ubuntu:bionic as app_release

RUN [ "cross-build-start" ]
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get --no-install-recommends install -y openssl libfreetype6 libhdf5-dev libhdf5-serial-dev libharfbuzz-bin libgtk-3-0 libavcodec58 libavformat58 libswscale5
ENV PAHO_MQTT_HOME=/paho.mqtt
COPY --from=build ${PAHO_MQTT_HOME}/lib /usr/lib
COPY --from=build /usr/local/lib/* /usr/lib
COPY --from=build /app/bin /usr/bin

STOPSIGNAL SIGINT
CMD ["/usr/bin/teknoir_app"]