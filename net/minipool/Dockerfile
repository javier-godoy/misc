ARG ALPINE_TAG
FROM alpine:${ALPINE_TAG}

RUN apk add g++ libstdc++ make

COPY src /src
RUN cd /src && make && mv minipool-* /
CMD sleep infinity
