ALPINE_TAG=3.19.1
VERSION=0.0.1

TAG = jgodoy/minipool\:$(VERSION)-alpine-$(ALPINE_TAG)

install:
	docker build . -t $(TAG) --build-arg ALPINE_TAG=$(ALPINE_TAG)
