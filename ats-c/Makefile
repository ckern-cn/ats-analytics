# ats-capi build entry.
#
#	make		-> bin/ats-capi (single binary, conf embedded)
#	make run	-> foreground on 127.0.0.1:3001
#	make clean
#
# The directory must be named ats-c because kodev derives the app name (and
# therefore conf/<name>.conf) from basename(pwd).

ROOT		:= $(shell pwd)
KORE_SOURCE	:= $(ROOT)/vendor/kore-4.2.3
KODEV		:= $(KORE_SOURCE)/kodev/kodev
BUILD		:= $(ROOT)/build
BIN		:= $(ROOT)/bin
APP		:= $(BIN)/ats-capi
KODEV_APP	:= ats-c
SCHEMA_H	:= $(ROOT)/src/schema_sql.h
SCHEMA_SQL	:= $(ROOT)/../db/schema.sql

FLAVOR		?= prod
KODEV_CC	:= gcc -Wno-deprecated-declarations -Wno-discarded-qualifiers
KORE_CFLAGS	:= -Wno-discarded-qualifiers
KODEV_ENV	:= KORE_SOURCE=$(KORE_SOURCE) KORE_FLAVOR='TLS_BACKEND=none NOTLS=1' \
		   KORE_BUILD_FLAVOR=$(FLAVOR) \
		   KORE_OBJDIR=$(BUILD)/obj KODEV_OUTPUT=$(BUILD) \
		   CC=gcc CFLAGS='$(KORE_CFLAGS)'

.PHONY: all prod run clean schema

all: $(APP)

schema: $(SCHEMA_H)

$(SCHEMA_H): $(SCHEMA_SQL) ../tools/gen-schema.sh
	sh ../tools/gen-schema.sh $(SCHEMA_SQL) $(SCHEMA_H)

$(APP): $(KODEV) $(wildcard src/*.c src/*/*.c) $(SCHEMA_H) conf/ats-c.conf conf/build.conf | $(BUILD)/obj $(BUILD)/kodev-obj $(BIN)
	env $(KODEV_ENV) $(KODEV) build
	cp -f $(BUILD)/$(KODEV_APP) $@

$(BUILD) $(BUILD)/obj $(BUILD)/kodev-obj $(BIN):
	mkdir -p $@

$(KODEV): $(KORE_SOURCE)/src/cli.c
	$(MAKE) -C $(KORE_SOURCE)/kodev OBJDIR=$(BUILD)/kodev-obj CC='$(KODEV_CC)'

prod:
	$(MAKE) clean
	$(MAKE) FLAVOR=prod

run: all
	$(APP) -fnr

clean:
	rm -rf $(BUILD) $(BIN) $(KODEV) $(SCHEMA_H)
