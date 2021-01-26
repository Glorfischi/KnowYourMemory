PREFIX = ../..

PLOTDIR = ./plots
DATADIR = ./data

OLD_PLOT = $(PLOTDIR)/send-pingpong.png $(PLOTDIR)/send-pingpong-multi.png	$(PLOTDIR)/send-bw.png\
			 $(PLOTDIR)/write-crc-lat.png $(PLOTDIR)/write-imm-lat.png $(PLOTDIR)/write-off-lat.png \
			 $(PLOTDIR)/write-ack-lat.png  $(PLOTDIR)/write-ack-ping.png $(PLOTDIR)/write-bw-msg-size.png\
			 $(PLOTDIR)/write-bw-ack.png $(PLOTDIR)/read-pingpong.png $(PLOTDIR)/read-bw.png 

PLOT = $(PLOTDIR)/send-bw-batch.png $(PLOTDIR)/send-bw-unack.png $(PLOTDIR)/send-bw-msgsize.png $(PLOTDIR)/send-lat-msgsize.png \
			 $(PLOTDIR)/send-bw-threads.png $(PLOTDIR)/send-bw-srq-threads.png $(PLOTDIR)/send-bw-srq-n1.png $(PLOTDIR)/send-bw-n1.png \
			 $(PLOTDIR)/write-lat-msgsize.png $(PLOTDIR)/write-bw-unack.png  $(PLOTDIR)/write-bw-rev-anom.png\
			 $(PLOTDIR)/write-bw-msgsize.png $(PLOTDIR)/write-bw-read-msgsize.png  $(PLOTDIR)/write-atomic-lat-msgsize.png \
			 $(PLOTDIR)/write-bw-threads-16.png $(PLOTDIR)/write-bw-threads-512.png $(PLOTDIR)/write-bw-threads-8192.png\
			 $(PLOTDIR)/write-bw-n1-16.png $(PLOTDIR)/write-bw-n1-512.png $(PLOTDIR)/write-bw-n1-8192.png \
			 $(PLOTDIR)/write-atomic-bw-msgsize.png $(PLOTDIR)/write-atomic-bw-threads.png \
			 $(PLOTDIR)/write-direct-bw-msgsize.png $(PLOTDIR)/write-direct-lat-msgsize.png $(PLOTDIR)/write-direct-bw-threads.png \
			 $(PLOTDIR)/write-direct-bw-n1.png \
			 $(PLOTDIR)/buf-read-lat-msgsize.png $(PLOTDIR)/buf-read-bw-msgsize.png \
       $(PLOTDIR)/buf-read-bw-threads.png $(PLOTDIR)/buf-read-bw-n1.png \
			 $(PLOTDIR)/dir-read-lat-msgsize.png $(PLOTDIR)/dir-read-bw-msgsize.png \
       $(PLOTDIR)/dir-read-bw-threads.png $(PLOTDIR)/dir-read-bw-n1.png \
			 $(PLOTDIR)/lat-msgsize.png







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


