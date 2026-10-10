CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Icore -Ihost -Iroundkit/include

CORE := core/paint.cpp core/text.cpp core/reply.cpp core/app.cpp core/look.cpp
HDRS := $(wildcard core/*.hpp core/*.inc host/*.hpp roundkit/include/rk/*.hpp)
TESTS := host/build/test_audio host/build/test_paint host/build/test_app host/build/test_look
UV_TEST := uv run -q --python 3.12 --with pillow --with fonttools

.PHONY: test relay-test sim fake shots media relay build flash clean

test: $(TESTS) relay-test
	@for t in $(TESTS); do ./$$t || exit 1; done

relay-test:
	@$(UV_TEST) python -m unittest discover -s relay 2>&1 | tail -1

host/build/test_%: host/test_%.cpp $(CORE) $(HDRS)
	@mkdir -p host/build
	$(CXX) $(CXXFLAGS) $< $(CORE) -o $@

host/build/sim: host/sim.cpp $(CORE) $(HDRS)
	@mkdir -p host/build
	$(CXX) $(CXXFLAGS) $$(sdl2-config --cflags) host/sim.cpp $(CORE) $$(sdl2-config --libs) -o $@

# Listens through the laptop's microphone; needs the relay running (make relay).
sim: host/build/sim
	./host/build/sim

fake: host/build/sim
	./host/build/sim --fake

shots: host/build/sim
	@mkdir -p host/out
	./host/build/sim --shots host/out

# Rebuilds the GIFs and screenshots in docs/. Needs ffmpeg.
media: host/build/sim
	uv run tools/make_media.py

relay:
	uv run relay/shaza_relay.py

build:
	./roundkit/scripts/idf set-target esp32s3
	./roundkit/scripts/idf build

flash:
	PORT=$(PORT) ./roundkit/scripts/idf -p $(PORT) flash

clean:
	rm -rf build host/build host/out
