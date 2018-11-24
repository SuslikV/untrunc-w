/*
    Untrunc - main.cpp
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio (https://github.com/ponchio/untrunc)
*/

//edited 2017

// The program ("untrunc-w") designed to restore playability
// of the aborted (selfcontained not fragmented) .mp4 recordings.

#include "mp4.h"
#include "atom.h"
#include "loginfo.h"

#include <iostream>
#include <string>
using namespace std;

//int verbLevel = VERB_FULL; //display all messages
int verbLevel = VERB_NORMAL; //display only info messages

int verbFFLevel = AV_LOG_FATAL; //display level of FFmpeg messages
//int verbFFLevel = AV_LOG_INFO;

bool deleteTmps = true; //remove .tmp file when task complete
bool complete = false; //save movie task is complete
uint32_t GOPcount = 1; //GOP count used to build the interleaving mask (minimum = 1)
uint32_t GOPloop = 1; //GOP count used to loop the interleaving mask (maximum = GOPcount)
int32_t smpltrk = 0; //0 = multi-track mode; sample per track count used to loop the chunk members in two-tracks mode only
int32_t h264alg = 12; //h264 nal recognition algorithm version
int32_t aacGG = 80; //AAC global_gain low threshold (for mono tracks). Only last 8-bits used.
                    //!!!Keep it as high as possible, current reading buffer allows 0x007A1200h (8000000) bytes samples,
                    //!!!so values lower than 0x3Dh (61) not recommended (risk of fasle positive recognition increases).
                    //!!!Assumption based on own experience, subject to change.

void usage() {
    logMe(LOG_ERR, "Usage: untrunc-w [options] <working.mp4> [<broken.mp4>]");
    logMe(LOG_INFO, "Help: untrunc-w --help");
    logMe(LOG_INFO, "");
}

void helpme() {
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "untrunc-w.Date.2018.version.1.4");
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "Usage: untrunc-w [options] <working.mp4> [<broken.mp4>]");
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "Options:");
    logMe(LOG_INFO, "--uverbose        : Display all untrunc-w messages.");
    logMe(LOG_INFO, "--ffverbose       : Display AV_LOG_INFO level of FFmpeg messages.");
    logMe(LOG_INFO, "--keeptmp         : Do not delete .tmp files after task complete.");
    logMe(LOG_INFO, "--gopm [n]        : IM size, in number [n] of GOPs. Default setting is 1.");
    logMe(LOG_INFO, "--gopl [n]        : IM loop start, in number [n] of GOPs. Default setting is 1.");
    logMe(LOG_INFO, "--smpltrk [0..100]: Number of samples per chunk instead of using IM values.");
    logMe(LOG_INFO, "                    Default setting is 0 (multi-track mode).");
    logMe(LOG_INFO, "--h264alg [10..12]: Algorithm of h264 recognition [version number].");
    logMe(LOG_INFO, "--aacgg [0..127]  : AAC (1 channel) low global_gain threshold. Default is 80.");
    logMe(LOG_INFO, "--help            : Display this help info.");
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "Recommendations.");
    logMe(LOG_INFO, "For multi-track movie [--gopm 12 --gopl 10] if you have working file that has at least 25 keyframes entries.");
    logMe(LOG_INFO, "For single-audio-track movie [--smpltrk 30] or so.");
    logMe(LOG_INFO, "");
}

int main(int argc, char *argv[]) {

    //test
    //argc = 8;
    //argv[0] = (char *)"untrunc-w.exe";
    //argv[1] = (char *)"--uverbose";
    //argv[2] = (char *)"--ffverbose";
    //argv[1] = (char *)"--keeptmp";
    //argv[1] = (char *)"C:/Temp/working_3tracks.mp4";
    //argv[2] = (char *)"C:/Temp/broken_3tracks.mp4";
    //argv[2] = (char *)"C:/Temp/2017-04-30 19-24-49.mp4";
    //argv[3] = (char *)"C:/Temp/2017-04-30 19-21-02.mp4";
    //argv[2] = (char *)"--gopm";
    //argv[3] = (char *)"12";
    //argv[4] = (char *)"--gopl";
    //argv[5] = (char *)"10";
    //argv[6] = (char *)"C:/Temp/2017-04-30 23-38-20working.mp4";
    //argv[7] = (char *)"C:/Temp/2017-04-30 21-09-09broken.mp4"; //thanks to bowlingotter for real samples
    //end_test
    string GOPcountStr, GOPloopStr, smpltrkStr, h264algStr, aacGGStr;
    int i = 1;
    for(; i < argc; i++) {
        string arg(argv[i]);
        if (arg == "--uverbose") {
            verbLevel = VERB_FULL;
        } else if (arg == "--ffverbose") {
            verbFFLevel = AV_LOG_INFO;
        } else if (arg == "--keeptmp") {
            deleteTmps = false;
        } else if (arg == "--gopm") {
            if (++i < argc) GOPcountStr = argv[i];
        } else if (arg == "--gopl") {
            if (++i < argc) GOPloopStr = argv[i];
        } else if (arg == "--smpltrk") {
            if (++i < argc) smpltrkStr = argv[i];
        } else if (arg == "--h264alg") {
            if (++i < argc) h264algStr = argv[i];
        } else if (arg == "--aacgg") {
            if (++i < argc) aacGGStr = argv[i];
        } else if (arg == "--help") {
            helpme();
            return 0;
        } else
            break; //for
    }
    if (argc == i) {
        usage();
        return -1;
    }
    if (GOPcountStr != "")
        GOPcount = atoi(GOPcountStr.c_str());
    if (GOPcount < 1)
        GOPcount = 1; //minimum 1
    logMe(LOG_INFO, "GOP count to build the interleaving mask: " + to_string(GOPcount));

    if (GOPloopStr != "")
        GOPloop = atoi(GOPloopStr.c_str());
    if (GOPloop > GOPcount)
        GOPloop = GOPcount -1; //minimum 0
    logMe(LOG_INFO, "GOP count to loop the interleaving mask: " + to_string(GOPloop));

    if (smpltrkStr != "")
        smpltrk = atoi(smpltrkStr.c_str());
    if (smpltrk > 100)
        smpltrk = 100; //!assumption based on own experience, subject to change
    logMe(LOG_INFO, "Samples per chunk (0 - get it from the working file): " + to_string(smpltrk));
    
    if (h264algStr != "") {
        h264alg = atoi(h264algStr.c_str());
        logMe(LOG_INFO, "h264 Algorithm version: " + to_string(h264alg));
    }

    if (aacGGStr != "") {
        aacGG = atoi(aacGGStr.c_str()) & 0x7F; //truncate to last 7-bits
        logMe(LOG_INFO, "Search for AAC (1 channel) with global_gain only in range: [" + to_string(aacGG) + "..127]");
    }
    aacGG = aacGG << 1; //prepare it for AAC mono tracks comparison

    string working = argv[i];
    string broken;
    i++;
    if (i < argc) // always, argv[argc] == NULL
        broken = argv[i];

    logMe(LOG_INFO, "Reading working file: " + working);
    Mp4 mp4;

    try {
        mp4.open(working);
        if (broken.size()) {
            mp4.repair(broken);
            mp4.saveMovie(broken + "_untrunc.mp4");
            mp4.removeTmps(broken + "_untrunc.mp4" + ".tmp");

            logMe(LOG_INFO, "========= The End. =========");
            logMe(LOG_INFO, "");
            if (complete)
                logMe(LOG_INFO, "Look for untruncated file: " + broken +"_untrunc.mp4");
        }
    } catch(string e) {
        logMe(LOG_ERR, e);
        return -1;
    }

    return 0;
}
