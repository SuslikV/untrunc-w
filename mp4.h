/*
    Untrunc - mp4.h
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#ifndef MP4_H
#define MP4_H

#include <vector>
#include <string>
#include <stdio.h>

#include "track.h"

extern "C" {
#include "libavformat/avformat.h" //extern to get av_register_all etc.
}

extern int verbFFLevel; //declare and define it at 'main.cpp' but also use in 'loginfo.cpp'
extern bool deleteTmps, complete; //declare and define it at 'main.cpp' but also use in 'mp4.cpp'
extern uint32_t GOPcount, GOPloop;
extern int32_t smpltrk;

class Box;
class clFile;

class Mp4 {
public:
    uint32_t timescale = 0;
    uint32_t duration = 0;
    uint64_t duration64 = 0; //for FullBoxes with version 1 only
    std::vector<uint32_t> interleavingMask; //stores chunks interlieving order (by # of the track wich they belongs to)
    std::vector<ChunkMembers> samplesPerChunkMask; //stores number of samples inside each chunk [0 - first chunk, 1 - second chunk, ...]
    uint32_t startLoopPoint = 0; //interleaving mask loop begining (in samples count)

    Box *root;
    Box *mdatBAD;

    Mp4();
    ~Mp4();

    void open(std::string filename);

    void repair(std::string filename); //searches for samples offsets and sizes inside broken video
    void saveMovie(std::string filenameBAD); //saves repaired movie
    void removeTmps(std::string filenameTmp); //removes temporary files used during box restoring

protected:
    clFile brokenFile; //to work with broken file
    clFile workingFile; //to work with OK file

    std::vector<Track> trackBoxContent; // stores all contnet (header + offset) of each 'trak' box
    AVFormatContext *contextByFFmpeg; //for opening working file by FFmpeg
    void getInterleavingMask(); //fill array with chunks interlieving order
    void getSamplesPerChunkMask(); //fill array with structure: chunk#(samples_count, description_index)
    void parseTracks(); //inside 'mdat' find all 'trak' boxes and read its data by FFmpeg, etc.

    ChunkMembers getChunkMembers(uint32_t trackNumber, uint32_t chunkPosition); //returns samples number and description index per asked track and chunk
};

#endif // MP4_H
