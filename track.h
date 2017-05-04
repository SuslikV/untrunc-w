/*
    Untrunc - track.h
    GPLv2
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#ifndef TRACK_H
#define TRACK_H

#include <vector>
#include <string>
#include "file.h"

extern "C" {
#include "libavcodec/avcodec.h" //extern to get enum AVCodecID
}

class Box;

//to store samples to chunk values from the 'stsc' box
struct ChunkMembers {
    uint32_t firstChunk;    //first_chunk from 'stsc' box
    uint32_t samplesCount;  //samples_per_chunk from 'stsc' box
    uint32_t descriptionIndex;  //max posible value is the entry count from 'stsd' box
                                //(but actually restricted by reference index from this box to uint 16-bit).
                                //Not used yet, maybe later...
};

struct recoveredSample {
    int64_t chunkInBoxOffset; //stores sample offset in bytes, relative to 'mdat' box start [0, 1, 2, 3, 4...]
    int32_t smplSize; //recovered size of the sample; max sample/chunk size is 2^31 bytes
    uint32_t smplNumInChunk; //if bigger than 1 then this sample belongs to previous chunk
    int32_t smplSend; //ffmpeg send packet result
    int32_t smplSync; //decoded frame, ffmpeg result (keyframe or not). Should mark chunks of random access point (RAP) only.
                      //0 - not sync sample
};

class Codec {
public:
    //return sturcture of sample
    recoveredSample getSampleSize(uint8_t *inbuf, int32_t blockLength, uint32_t minLength, AVCodecParameters *streamCodec, uint32_t samplesCount);

    AVCodecParameters *codecParamByFFmpeg; //to get codec parameters from working file and use it on broken
    AVCodec *codecByFFmeg;

};

class Track {
public:
    uint32_t timescale = 0;
    uint32_t duration = 0;
    uint64_t duration64 = 0; //for FullBoxes with version 1 only
    double_t trkTime; //track's length in seconds
    uint32_t trkID; //track ID
    bool useOffsets64; //true if 'co64' box present
    Codec codec;

    //correspondents of terms (ISO 14496-15):
    //         AVC | ISO
    //----------------------
    //      Stream | Track
    // Access Unit | Sample

    //Sync sample is Keyframe (IDR frame for video)

    //The Chunk is group of the sequential Samples of the same Track.
    //Chunks inside 'mdat' box are interleaved.
    //
    //Interleaving example, 3 tracks ISO (Sv - video sample, Sa - audio sample):
    //
    //           chunk chunk  chunk  chunk chunk  chunk  chunk chunk  chunk
    // mdat data:  Sv  SaSaSa SaSaSa   Sv  SaSaSa SaSaSa   Sv  SaSaSa SaSaSa ...
    //   track #:  1   2 2 2  3 3 3    1   2 2 2  3 3 3    1   2 2 2  3 3 3
    //--------------------------------------------------------------------------
    //Note:
    // chunk
    // SaSaSa
    // 2 3 2   - not possible! (inside chunk - samples should belong to single track)

    std::vector<uint32_t> interleavingMask; //stores chunks interleaving order

    std::vector<uint32_t> times; //stores samples deltas only
    std::vector<uint32_t> offsets; //stores chunk offsets
    std::vector<uint64_t> longOffsets64; //stores chunk offsets 64bit
    std::vector<uint32_t> sizes; //stores samples sizes (AU sizes)
    std::vector<uint32_t> sizesMinMax; //stores samples minimum and maximum sizes (AU sizes)
    std::vector<uint32_t> keyframes; //0 based! stores sync samples numbers, if box 'stss' not present then all samples are sync ones
    std::vector<ChunkMembers> samplesFPD; //stores samples to chunk box content Firstchunk_samplesPerchunk_sampleDescriptionindex (FPD)

    uint8_t nalSizeField; //stores nalSizeField per track; lengthSizeMinusOne +1 from avcC ISO 14496-15 2010
    std::vector<recoveredSample> smplsRParams; //stores all samples sturcture of the broken file

    void parse(Track *track, Box *trak, clFile &workingfile); //get sample times, keyframes, sample sizes etc.

    std::vector<uint32_t> getSampleDeltas(Box *t, clFile &workingfile);  //get array of samples deltas only, source: 'stts' box
    std::vector<uint32_t> getKeyframes(Box *t, clFile &workingfile);     //get array of sync samples numbers, source: 'stss' box

    std::vector<uint32_t> getMinMaxSTSZ(Box *t, clFile &workingfile);   //get array of sample minimum and maximum size (AU sizes), source 'stsz' or 'stz2' box

    std::vector<uint32_t> getChunkOffsets(Box *t, clFile &workingfile); //get array of chunk offsets, source 'stco' or 'co64' box
    std::vector<uint64_t> getChunkOffsets64(Box *t, clFile &workingfile); //get array of chunk offsets, source 'co64' box

    std::vector<ChunkMembers> getSamplesNum(Box *t, clFile &workingfile);   //get array of first chunk number(in group/run of chunks), samples count per each chunk and index description,
                                                                            //source 'stsc' box
    uint8_t getNalSizeField(Track *track, Box *t, clFile &workingfile); //get length of the NAL-size field from the given box ('stsd')

    uint32_t optMinSampleSize(Track &track, clFile &brokenfile, uint64_t sampleOffset, AVCodecParameters *streamCodec); //returns minimal sample size close to expected size of the current sample
};

#endif // TRACK_H
