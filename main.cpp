#include "FFmpegDemuxSeeker.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>

struct termios originalTermSettings;
// Function to save the original terminal settings
void saveTerminalSettings() {
    tcgetattr(STDIN_FILENO, &originalTermSettings); // Save the current terminal settings
}

// Function to restore terminal settings to their original state
void restoreTerminalSettings() {
    tcsetattr(STDIN_FILENO, TCSANOW, &originalTermSettings); // Restore original settings
}

void cleanupKeyboard() {
    restoreTerminalSettings(); // Restore terminal settings on cleanup
}

int main(int argc, char* argv[]) {
    saveTerminalSettings();
    if (argc < 3) {
        std::cerr << "Usage: ./ffmpeg_seeker <input_file> <Decoder Type:HW/SW>\n";
        return 1;
    }
    DecoderType decoder;
    if (strncmp(argv[2],"SW", 2) == 0)
       decoder = SOFTWARE;
    else if ( strncmp(argv[2], "HW", 2) == 0)
       decoder = HARDWARE;
    else 
       decoder = SOFTWARE; //default 
                                       //
    try {
        FFmpegDemuxSeeker demux_seeker(argv[1], decoder);
        demux_seeker.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    cleanupKeyboard();
    return 0;
}
