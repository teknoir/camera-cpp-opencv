##########################
FROM balenalib/generic-aarch64-ubuntu:focal-build as build
RUN [ "cross-build-start" ]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get --no-install-recommends install -y \
   wget curl git unzip build-essential git gcc make cmake cmake-gui cmake-curses-gui libssl-dev \
   libopencv-dev

ENV PAHO_MQTT_HOME=/paho.mqtt
ENV C_INCLUDE_PATH=${PAHO_MQTT_HOME}/include:${C_INCLUDE_PATH}
ENV CPATH=${PAHO_MQTT_HOME}/include:$CPATH
WORKDIR ${PAHO_MQTT_HOME}
RUN git clone -b v1.3.11 https://github.com/eclipse/paho.mqtt.c.git && \
    cd paho.mqtt.c && \
    cmake -Bbuild -H. -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DPAHO_ENABLE_TESTING=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN git clone -b v1.2.0 https://github.com/eclipse/paho.mqtt.cpp && \
    cd paho.mqtt.cpp && \
    cmake -Bbuild -H. -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE -DCMAKE_INSTALL_PREFIX=${PAHO_MQTT_HOME} -DCMAKE_PREFIX_PATH=${PAHO_MQTT_HOME} && \
    cmake --build build/ --target install

RUN cp -rf ${PAHO_MQTT_HOME}/lib/* /usr/lib/ && \
    cp -rf ${PAHO_MQTT_HOME}/include/* /usr/include/

WORKDIR /app
ADD ./app /app
RUN cmake -Bbuild -H. -DCMAKE_INSTALL_PREFIX=/app && \
    cmake --build build/ --target install

RUN [ "cross-build-end" ]

##########################
FROM balenalib/generic-aarch64-ubuntu:focal as app_release

RUN [ "cross-build-start" ]
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get --no-install-recommends install -y openssl libopencv-videoio4.2 libopencv-core4.2 libopencv-contrib4.2 libopencv-imgcodecs4.2 libopencv-imgproc4.2
ENV PAHO_MQTT_HOME=/paho.mqtt
COPY --from=build ${PAHO_MQTT_HOME}/lib /usr/lib
COPY --from=build /usr/local/lib/* /usr/lib
COPY --from=build /app/bin /usr/bin

STOPSIGNAL SIGINT
CMD ["/usr/bin/teknoir_app"]
