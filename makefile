CC ?= gcc

# -Wstrict-prototypes is also supported in clang 4.0 and newer,
# but since clang, unlike gcc, ignores unknown -W options, we
# don't need to do anything special to support older versions.
CFLAGS += -fPIC -std=c99 -Wall -Wextra -pedantic -Wstrict-prototypes
LDFLAGS += -shared

RELEASE ?= 0
ifeq ($(RELEASE),0)
CFLAGS += -g
else
CFLAGS += -O2 -DNDEBUG
endif

# List of additional include paths to search for DeaDBeeF API headers
DEADBEEF_INC :=

SOURCES := main.c

# We use conditional assignment for GTK2_CFLAGS and GTK2_LIBS
# to support DeaDBeeF Plugin Builder.
GTK2_TARGET := playlist_manager_gtk2.so
GTK2_CFLAGS ?= $(shell pkg-config --cflags gtk+-2.0)
GTK2_LIBS ?= $(shell pkg-config --libs gtk+-2.0)
GTK2_OBJECTS := $(SOURCES:.c=_gtk2.o)

GTK3_TARGET := playlist_manager_gtk3.so
GTK3_CFLAGS ?= $(shell pkg-config --cflags gtk+-3.0)
GTK3_LIBS ?= $(shell pkg-config --libs gtk+-3.0)
GTK3_OBJECTS := $(SOURCES:.c=_gtk3.o)


.PHONY: all
all: gtk2 gtk3

.PHONY: gtk2
gtk2: $(GTK2_TARGET)

.PHONY: gtk3
gtk3: $(GTK3_TARGET)

$(GTK2_TARGET): $(GTK2_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(GTK2_LIBS)

%_gtk2.o: %.c
	$(CC) $(CFLAGS) $(GTK2_CFLAGS) $(DEADBEEF_INC) -c -o $@ $<

$(GTK3_TARGET): $(GTK3_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(GTK3_LIBS)

%_gtk3.o: %.c
	$(CC) $(CFLAGS) $(GTK3_CFLAGS) $(DEADBEEF_INC) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(GTK2_TARGET) $(GTK2_OBJECTS) $(GTK3_TARGET) $(GTK3_OBJECTS)
