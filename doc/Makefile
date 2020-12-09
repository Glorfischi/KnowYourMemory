PREFIX = ../..

PLOTDIR = ./plots
DATADIR = ./data

OLD_PLOT = $(PLOTDIR)/send-pingpong.png $(PLOTDIR)/send-pingpong-multi.png	$(PLOTDIR)/send-bw.png\
			 $(PLOTDIR)/write-crc-lat.png $(PLOTDIR)/write-imm-lat.png $(PLOTDIR)/write-off-lat.png \
			 $(PLOTDIR)/write-ack-lat.png  $(PLOTDIR)/write-ack-ping.png $(PLOTDIR)/write-bw-msg-size.png\
			 $(PLOTDIR)/write-bw-ack.png $(PLOTDIR)/read-pingpong.png $(PLOTDIR)/read-bw.png 

PLOT = $(PLOTDIR)/send-bw-batch.png $(PLOTDIR)/send-bw-unack.png $(PLOTDIR)/send-bw-msgsize.png $(PLOTDIR)/send-lat-msgsize.png \
			 $(PLOTDIR)/send-bw-threads.png $(PLOTDIR)/send-bw-threads-zoomed.png



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


