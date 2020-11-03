PREFIX = ../..

PLOTDIR = ./plots
DATADIR = ./data

PLOT = $(PLOTDIR)/send-pingpong.png $(PLOTDIR)/send-pingpong-multi.png	$(PLOTDIR)/send-bw.png\
			 $(PLOTDIR)/write-crc-lat.png $(PLOTDIR)/write-imm-lat.png $(PLOTDIR)/write-off-lat.png \
			 $(PLOTDIR)/write-ack-lat.png  $(PLOTDIR)/write-ack-ping.png




plot: $(PLOT)

clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(PLOT)
	@echo "####################"
	@echo

%.png: %.p $(DATADIR)
	gnuplot $<

.DELETE_ON_ERROR:
.PHONY: all clean plot


