# Convenience wrapper around the Kore app in ats-c/.
.PHONY: all prod test release clean

all:
	$(MAKE) -C ats-c

prod:
	$(MAKE) -C ats-c prod

test: all
	@echo "Start the service, then run: ATS_ADMIN_PASSWORD=... tools/smoke.sh <base-url>"

release:
	tools/release.sh

clean:
	$(MAKE) -C ats-c clean
