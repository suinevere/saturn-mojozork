# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:223-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
#
# For NETBIN=1 builds, flatten the ELF to the raw image the PlanetWeb loader
# expects and refuse to ship one that exceeds the loader's 400 KB ceiling.
NETBIN_MAX_BYTES = 409600

post_build:
ifeq ($(strip $(NETBIN)),1)
	$(info ****** Packaging zaturn.netbin ******)
	@$(OBJCOPY) -O binary "$(BUILD_ELF)" "$(BUILD_DROP)/zaturn.netbin"
	@sz=$$(stat -c%s "$(BUILD_DROP)/zaturn.netbin"); \
	 echo "zaturn.netbin: $$sz bytes (limit $(NETBIN_MAX_BYTES))"; \
	 if [ "$$sz" -gt "$(NETBIN_MAX_BYTES)" ]; then \
	     echo "ERROR: zaturn.netbin exceeds the $(NETBIN_MAX_BYTES)-byte loader limit" >&2; \
	     rm -f "$(BUILD_DROP)/zaturn.netbin"; \
	     exit 1; \
	 fi
else
	$(info ****** No post build steps ******)
endif
