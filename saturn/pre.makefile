# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-221). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
pre_build:
	$(info ****** Converting PNG backgrounds to TGA ******)
	@sh ../tools/convert-backgrounds.sh
ifeq ($(strip $(NETBIN)),1)
	$(info ****** Regenerating embedded netbin payloads ******)
	@python3 ../tools/gen_blob.py src/netbin_blobs.c \
	    --pack story story=cd/data/Z3/ZORK1.Z3
endif
