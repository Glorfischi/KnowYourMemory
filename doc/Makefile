PREFIX = ../..

PLOTDIR = ./plots
DATADIR = ./data

PLOT = $(PLOTDIR)/send-pingpong.png $(PLOTDIR)/send-pingpong-multi.png	$(PLOTDIR)/send-bw.png\
			 $(PLOTDIR)/write-crc-lat.png $(PLOTDIR)/write-imm-lat.png $(PLOTDIR)/write-off-lat.png \
			 $(PLOTDIR)/write-ack-lat.png  $(PLOTDIR)/write-ack-ping.png $(PLOTDIR)/write-bw-msg-size.png\
			 $(PLOTDIR)/write-bw-ack.png $(PLOTDIR)/read-pingpong.png $(PLOTDIR)/read-bw.png




plot: $(PLOT)

clean:
	@echo "##### CLEAN-UP #####"
	-rm -f $(PLOT)
	@echo "####################"
	@echo

%.png: %.p $(DATADIR)
	gnuplot $<

%.pdf: %.tex
	$(VARS) latexmk -f -pdf -M -MP -MF $*.deps -cd $<

thesis: thesis/thesis.pdf

.DELETE_ON_ERROR:
.PHONY: all clean plot


