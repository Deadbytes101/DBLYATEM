include Makefile

CORE += src/gateway_roundtrip.c
HEADERS += include/dblyat/gateway_roundtrip.h

$(BUILD)/dblyatem: src/gateway_roundtrip.c include/dblyat/gateway_roundtrip.h

$(BUILD)/gateway_roundtrip_test: tests/gateway_roundtrip_test.c \
                                 src/gateway_roundtrip.c \
                                 src/gateway_enter.c \
                                 src/gateway_lookup.c \
                                 src/gateway_dispatch.c \
                                 src/gateway_frame.c \
                                 src/gateway_complete.c \
                                 src/gateway_return.c \
                                 src/gateway_return_apply.c \
                                 include/dblyat/gateway_roundtrip.h \
                                 include/dblyat/gateway_enter.h \
                                 include/dblyat/gateway_complete.h \
                                 include/dblyat/gateway_return_apply.h \
                                 include/dblyat/gateway_return.h \
                                 include/dblyat/gateway_frame.h \
                                 include/dblyat/gateway_dispatch.h \
                                 include/dblyat/gateway_lookup.h \
                                 include/dblyat/gateway_decode.h \
                                 include/dblyat/gateway_emit.h \
                                 include/dblyat/gateway_plan.h \
                                 include/dblyat/iat_plan.h \
                                 include/dblyat/bind.h \
                                 include/dblyat/import.h \
                                 include/dblyat/map.h \
                                 include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_roundtrip_test.c \
		src/gateway_roundtrip.c src/gateway_enter.c \
		src/gateway_lookup.c src/gateway_dispatch.c \
		src/gateway_frame.c src/gateway_complete.c \
		src/gateway_return.c src/gateway_return_apply.c -o $@

.PHONY: gateway_roundtrip_verify
gateway_roundtrip_verify: $(BUILD)/gateway_roundtrip_test
	./$(BUILD)/gateway_roundtrip_test

test: gateway_roundtrip_verify
