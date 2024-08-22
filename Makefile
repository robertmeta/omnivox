run: all
	./omnivox

all: omnivox

omnivox:
	gcc omnivox.c -o omnivox \
		-I/opt/dectalk/include \
		-L/opt/dectalk/lib \
		-ltts \
		-I/opt/homebrew/include \
		-L/opt/homebrew/lib \
		-luv \
		-lportaudio \
		-lsndfile \
		-Wl,-rpath,/opt/dectalk/lib

