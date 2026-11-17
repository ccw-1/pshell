# Build the user's guide PDF (needs pandoc and XeLaTeX with DejaVu fonts)
GUIDE_PDF = GUIDE.pdf
VERSION := $(shell cat ../VERSION)

all: $(GUIDE_PDF)

$(GUIDE_PDF): GUIDE.md guide-header.tex ../VERSION
	pandoc GUIDE.md -o $@ --pdf-engine=xelatex \
	  --toc --toc-depth=2 --number-sections -V documentclass=report \
	  -V geometry:margin=2.2cm -V colorlinks=true -V linkcolor=blue \
	  -V mainfont="DejaVu Serif" -V monofont="DejaVu Sans Mono" \
	  -V monofontoptions="Scale=0.85" -V fontsize=10pt \
	  -M date="Version $(VERSION)" \
	  -H guide-header.tex --highlight-style=tango

clean:
	rm -f $(GUIDE_PDF)

.PHONY: all clean
