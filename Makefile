TARGET = ffmpeg_seeker

CXX = g++
#CXXFLAGS = -Wall -std=c++17 -DSPDLOG_FMT_EXTERNAL
CXXFLAGS = -Wall -DSPDLOG_FMT_EXTERNAL -Wall -I/usr/include/ffmpeg -I.

LIBS = -lavformat -lavcodec -lavutil -lpthread -lfmt
INCLUDES = -I/usr/include/ffmpeg

SRCS = main.cpp FFmpegDemuxSeeker.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
