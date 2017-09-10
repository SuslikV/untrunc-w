/*
	Untrunc - track.h
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#include <string>
#include <iostream>

#include "mp4.h"
#include "atom.h"
#include "file.h"
#include "loginfo.h"

using namespace std;

Mp4::Mp4(): root(NULL) { }

Mp4::~Mp4() {
    delete root;
}

void Mp4::open(string filename) {
    if (!workingFile.open(filename))
        throw string("Could not open file: ") + filename;

    root = new Box;
    while(1) {
        Box *box = new Box;
        box->parseMP4ForBoxes(workingFile);
        root->children.push_back(box);
        logMe(LOG_DBG, "root->children.size(): " + to_string(root->children.size()));
        if (workingFile.atEnd()) break;
    }

    if (root->getBoxByType((uint32_t)'ctts'))
        logMe(LOG_DBG, "Composition time offset box found. Out of order samples possible.");

    if (root->getBoxByType((uint32_t)'sdtp'))
        logMe(LOG_DBG, "Sample dependency flag box found. I and P frames might need to recover that info.");

    Box *mvhd = root->getBoxByType((uint32_t)'mvhd');
    if (!mvhd)
        throw string("Missing movie header box (mvhd)");

    //test
    //movie lasts as longest track
    //timescale = 1000;
    //duration = 19920;
    //time ~19.92

    if (((mvhd->vpFlags) >> 24) == 1) {
        //for FullBox version 1
        timescale = workingFile.readUintP((mvhd->contentOffset) +8 +8);
        duration64 = workingFile.readUint64(); //always lies next to timescale
    } else {
        timescale = workingFile.readUintP((mvhd->contentOffset) +4 +4);
        duration = workingFile.readUint(); //always lies next to timescale
    }

    logMe(LOG_DBG, "movie timescale = " + to_string(timescale));
    logMe(LOG_DBG, "movie duration = " + to_string(duration));
    logMe(LOG_DBG, "movie duration64 = " + to_string(duration64));

    av_register_all();

    //AV_LOG_QUIET   -8
    //Print no output.
    //AV_LOG_PANIC   0
    //Something went really wrong and we will crash now.
    //AV_LOG_FATAL   8
    //Something went wrong and recovery is not possible.
    //AV_LOG_ERROR   16
    //Something went wrong and cannot losslessly be recovered.
    //AV_LOG_WARNING   24
    //Something somehow does not look correct.
    //AV_LOG_INFO   32
    //Standard information.
    //AV_LOG_VERBOSE   40
    //Detailed information.
    //AV_LOG_DEBUG   48
    //Stuff which is only useful for libav* developers.

    av_log_set_level(verbFFLevel);
    logMe(LOG_INFO, "AV log level: " + to_string(av_log_get_level()));
    contextByFFmpeg = avformat_alloc_context();

    //open video file
    int error = avformat_open_input(&contextByFFmpeg, filename.c_str(), NULL, NULL);

    if (error != 0)
        throw "Could not parse AV file: " + filename;

    if (avformat_find_stream_info(contextByFFmpeg, NULL) < 0)
        throw string("Could not find stream info");

    av_dump_format(contextByFFmpeg, 0, filename.c_str(), 0);

    parseTracks();
}

void Mp4::parseTracks() {
    vector<Box *> trakBoxes = root->getBoxesCollByType((uint32_t)'trak');
    for (unsigned int i = 0; i < trakBoxes.size(); i++) {
        Track track;
        track.codec.codecParamByFFmpeg = contextByFFmpeg->streams[i]->codecpar;
        track.parse(&track, trakBoxes[i], workingFile); //parse all 'trak' boxes (find 'mdhd' inside and get timescale, duration, times, keyframes, sizes)
        trackBoxContent.push_back(track);
        //86018 = AV_CODEC_ID_ACC
        //28 = AV_CODEC_ID_H264
        AVCodecID codecID = trackBoxContent[i].codec.codecParamByFFmpeg->codec_id;
        logMe(LOG_INFO, "Track " + to_string(i) + ", codecID= " + to_string(codecID));
	}
    getInterleavingMask();
    //test
    logMe(LOG_INFO, "Chunks interleaving Mask (track's #): ");
    for (unsigned int i = 0; i < interleavingMask.size() -1; i++) //-1 because mask has additional element at the end
        logM(LOG_INFO, to_string(interleavingMask[i]) + " ");
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "Chunks interleaving Mask second loop (track's #): ");
    for (unsigned int i = startLoopPoint; i < interleavingMask.size() -1; i++) //-1 because mask has additional element at the end
        logM(LOG_INFO, to_string(interleavingMask[i]) + " ");
    logMe(LOG_INFO, "");

    getSamplesPerChunkMask();
    //test
    logMe(LOG_INFO, "Samples per each chunk Mask (samples count): ");
    for (unsigned int i = 0; i < samplesPerChunkMask.size() -1; i++)
        logM(LOG_INFO, to_string(samplesPerChunkMask[i].samplesCount) + " ");
    logMe(LOG_INFO, "");
    logMe(LOG_INFO, "Samples per each chunk Mask second loop (samples count): ");
    for (unsigned int i = startLoopPoint; i < samplesPerChunkMask.size() -1; i++)
        logM(LOG_INFO, to_string(samplesPerChunkMask[i].samplesCount) + " ");
    logMe(LOG_INFO, "");
}

/******************
 Example of the interleavingMask.

 track0 chunk offsets ('stco'): [1, 10, 20, 30]
 track1 chunk offsets ('stco'): [2, 4, 12, 15, 21, 24, 32, 33]
 track2 chunk offsets ('stco'): [6, 7, 18, 19, 25, 28, 38, 39]

 interleavingMask: [0, 1, 1, 2, 2, 0, 1, 1, 2, 2, 0, 1, 1, 2, 2, 0]

 GOPcount = 1
 interleavingMask: [0, 1, 1, 2, 2, 0]
*******************/

void Mp4::getInterleavingMask() {
    vector<uint32_t> j; // stores index counters for each track
    uint32_t i; // element index
    uint32_t minVal; //track number that has minimal element

    logMe(LOG_DBG, "Number of tracks = " + to_string(trackBoxContent.size()));
    for (unsigned int i = 0; i < trackBoxContent.size(); i++)
        j.push_back((uint32_t) 0); //start each track's element index from 0

    uint32_t baseN = 0; //number for the base track

    vector<Box *> trakBoxesTmp = root->getBoxesCollByType((uint32_t)'trak');
    if (!(trackBoxContent.size() == trakBoxesTmp.size()))
        throw ("Error. The root box of tracks was modified after last parsing. Cannot proceed further.");

    //use first track that has sync samples box ('stss') as base track
    for (uint32_t i = 0; i < trackBoxContent.size(); i++) {
        Box *stss = trakBoxesTmp[i]->getBoxByType((uint32_t)'stss');
        if (stss) {
            baseN = i; //if possible, set the base track number
            break;
        }
    }
    logMe(LOG_INFO, "Base track: " + to_string(baseN));
    Track &baseTrack = trackBoxContent[baseN]; // first track is our base, assuming that this is a video track

    //maximum length of the mask in number of GOPs;
    //use 'stss' info if present, otherwise use 120 chunks or so...

    uint32_t maxChunks = baseTrack.offsets.size(); //same as in base track
    //test
    //GOPcount = 2; //minimum one GOP
    if (baseTrack.keyframes.size() > GOPcount) {
        // maximum length of the mask in number of GOPs
        maxChunks = baseTrack.keyframes[GOPcount];
    } else {
        maxChunks = 120; //maximum length of the mask in chunks of the base track
    }
    logMe(LOG_INFO, "baseTrack.keyframes.size(): " + to_string(baseTrack.keyframes.size()));
    logMe(LOG_INFO, "Number of chunks used to build the mask: " + to_string(maxChunks));

    //---- TODO: implement 'merge sort' elements, because current algorithm inefficient ----//
    if (baseTrack.useOffsets64) { //questionable assumption that all tracks may use same short/long offsets (anyway short working file recommended, so 64-bit branch almost never used)
        //64-bit offsets
        logMe(LOG_DBG, "baseTrack.offsets.size()= " + to_string(baseTrack.longOffsets64.size()));

        while ((j[baseN] < maxChunks) && (j[baseN] < baseTrack.longOffsets64.size())) {
            minVal = baseN; //first track
            for (uint32_t k = 0; k < trackBoxContent.size(); k++) {
                //k - track number: first, second, third, fourth...
                i = j[k];
                logMe(LOG_DBG, "i= " + to_string(i));
                if (i < trackBoxContent[k].longOffsets64.size()) { //not more than track has
                    logMe(LOG_DBG, "trackBoxContent[k].longOffsets64.size() = " + to_string(trackBoxContent[k].longOffsets64.size()));
                    logMe(LOG_DBG, to_string(trackBoxContent[minVal].longOffsets64[j[minVal]]) + " > " + to_string(trackBoxContent[k].longOffsets64[i]));
                    if (trackBoxContent[minVal].longOffsets64[j[minVal]] > trackBoxContent[k].longOffsets64[i]) {
                        minVal = k; //remember track's number
                    }
                }
            }
            logMe(LOG_DBG, "minVal= " + to_string(minVal));
            interleavingMask.push_back(minVal);//add number of the track to the mask array
            j[minVal]++; //next track's element
            logMe(LOG_DBG, "j[minVal]++= " + to_string(j[minVal]));
        } //while

    } else {
        //32-bit offsets
        logMe(LOG_DBG, "baseTrack.offsets.size()= " + to_string(baseTrack.offsets.size()));

        while ((j[baseN] < maxChunks) && (j[baseN] < baseTrack.offsets.size())) {
            minVal = baseN; //first track
            for (uint32_t k = 0; k < trackBoxContent.size(); k++) {
                //k - track number: first, second, third, fourth...
                i = j[k];
                logMe(LOG_DBG, "i= " + to_string(i));
                if (i < trackBoxContent[k].offsets.size()) { //not more than track has
                    logMe(LOG_DBG, "trackBoxContent[k].offsets.size() = " + to_string(trackBoxContent[k].offsets.size()));
                    logMe(LOG_DBG, to_string(trackBoxContent[minVal].offsets[j[minVal]]) + " > " + to_string(trackBoxContent[k].offsets[i]));
                    if (trackBoxContent[minVal].offsets[j[minVal]] > trackBoxContent[k].offsets[i]) {
                        minVal = k; //remember track's number
                    }
                }
            }
            logMe(LOG_DBG, "minVal= " + to_string(minVal));
            interleavingMask.push_back(minVal);//add number of the track to the mask array
            j[minVal]++; //next track's element
            logMe(LOG_DBG, "j[minVal]++; j[" + to_string(minVal) + "]= " + to_string(j[minVal]));
        } //while

    } //if
	
    if (GOPcount - GOPloop == 0) {
        startLoopPoint = 0; //start loop from the start of the interleaving mask
    } else {
        startLoopPoint = 0; //start loop from the start of the interleaving mask
        if (baseTrack.keyframes.size() > GOPcount) {
            uint32_t i = 0;
            while (startLoopPoint < baseTrack.keyframes[GOPloop] && i < interleavingMask.size()) {
            if (interleavingMask[i] == baseN) //count only base track samples
                startLoopPoint++;
            i++;
            } //while
            startLoopPoint = i -1; //turn startLoopPoint into index storage now; -1 because last i++
        } //if
    } //if
}

/******************
 Example of the samplesPerChunkMask (track0 - video, track1 - audio).

 track0 'stsc' (one     entry): [1,1,1]
 track1 'stsc' (three entries): [1,2,1,  4,1,1,  5,2,1]

    interleavingMask: [  0,     1,     0,     1,     0,     1,     0,     1,     0,     1,     0  ]
 samplesPerChunkMask: [(1;1), (2;1), (1;1), (2;1), (1;1), (2;1), (1;1), (1;1), (1;1), (2;1), (1;1)]
      track0 chunk #:    1             2             3             4             5             6
      track1 chunk #:           1             2             3             4             5
 track0 chunk#=[1...6] shares same samples_per_chunk and description_index - this is one run of chunks
 track1 chunk#=[1...3] shares same samples_per_chunk and description_index - this is one run of chunks
 track1 chunk#=[4] - this is one run of chunks too (until next run occured)
 track1 chunk#=[5] - this is one run of chunks too (until next run occured)
*******************/

void Mp4::getSamplesPerChunkMask() {
    vector<uint32_t> j; // stores index counters for each track

    //begin from: track 0, chunk 1 or getChunkMembers(0,1);
    //first position of the chunk is 1 and inside 'stsc' box it is equal to chunk number: 1;
    for (uint32_t i = 0; i < trackBoxContent.size(); i++)
        j.push_back((uint32_t) 1);

    ChunkMembers x;
    uint32_t trackNumber;
    for (uint32_t i = 0; i < interleavingMask.size(); i++) {
        trackNumber = interleavingMask[i];
        x = getChunkMembers(trackNumber, j[trackNumber]);
        samplesPerChunkMask.push_back(x); //fill the samplesPerchunkMask with ChunkMembers (samples count)
        j[trackNumber]++;
    }
}

ChunkMembers Mp4::getChunkMembers(uint32_t trackNumber, uint32_t chunkPosition) {
    Track &track = trackBoxContent[trackNumber]; //get track

    ChunkMembers current, previous;
    previous = track.samplesFPD[0]; //the first entry

    uint32_t i = 0;
    while (i < track.samplesFPD.size()) {
        //update current
        current = track.samplesFPD[i];

        if (current.firstChunk > chunkPosition)
            return previous;
        if (current.firstChunk == chunkPosition)
            return current;

        //remember current
        previous = current;

        i++;
        }

    return previous; //last entry extends to the end of the box
}

void Mp4::repair(string filename) {
    if (!brokenFile.open(filename))
        throw "Could not open file to repair: " + filename;

    logMe(LOG_INFO, "Reading broken file: " + filename);

    if (brokenFile.length() < 2)
        throw string("Broken file too small - almost no data to recover.");

    //find 'mdat' by parsing file or find it manually and then parse header of the box (usefull when file header not_written/reserved)
    BufferedBox *mdat = new BufferedBox;
    while (brokenFile.pos() < brokenFile.length() -1) { //till the end of the file

        Box *box = new Box;
		try {
            box->parseBoxHeader(brokenFile);
		} catch(string) {
            throw string("Failed to parse boxes headers in truncated file: ") + filename;
		}

        if ((box->type) != (uint32_t)'mdat') {
            //seek to the next box
            brokenFile.seek((box->startOffset) + (box->actualSize));
            delete box;     //remove the current box - it is not what we are looking for
            continue;       //parse again until 'mdat' box found
        }

        //remember to mdat what we just found
        //here box->type is 'mdat'
        mdat->startOffset = box->startOffset;
        mdat->contentOffset = box->contentOffset;
        mdat->type = box->type;
        mdat->actualSize = box->actualSize; //let's assume that box truncated and box size = 0 (to the end of the file)
        mdat->contentSize = box->contentSize;

        mdatBAD = mdat; //remember this box parameters to use it later for raw copy

		break;
    } //while

    //if not found, try to find 'mdat' manually
    if ((mdat->type) != (uint32_t)'mdat') {
        int32_t fileBufLength = 800000; //bytes
        uint8_t *fileBuf;
        fileBuf = (uint8_t*) malloc(sizeof(uint8_t) * fileBufLength);
        if (fileBuf == NULL)
            throw string("Error. Cannot allocate memory buffer for 'mdat' find procedure.");

        //look only for first fileBufLength bytes
        uint32_t  wasRead = brokenFile.readBlockOfBytesP(0, fileBuf, fileBufLength);
        int32_t mdatPos = brokenFile.findLastUInt(fileBuf, wasRead, (uint32_t)'mdat');
        free(fileBuf);

        logMe(LOG_DBG, "mdatPos = " + to_string(mdatPos));

        if (mdatPos < 4) //4 bytes reserved per box size field
            throw string("Failed to find 'mdat' box in truncated file: ") + filename;

        brokenFile.seek((int64_t) mdatPos -4); //seek to the begin of the box
        logMe(LOG_INFO, "'mdat' box starting from " + to_string(brokenFile.pos()) + " position");

        Box *box = new Box;
        try {
            box->parseBoxHeader(brokenFile);
        } catch(string) {
            throw string("Failed to parse boxes headers in truncated file: ") + filename;
        }
        //remember to mdat what we just found
        //here box->type is 'mdat'
        mdat->startOffset = box->startOffset;
        mdat->contentOffset = box->contentOffset;
        mdat->type = box->type;
        mdat->actualSize = box->actualSize; //let's assume that box truncated and box size = 0 (to the end of the file)
        mdat->contentSize = box->contentSize;

        mdatBAD = mdat; //remember this box parameters to use it later for raw copy

    } //if

//The idea is to read block of 'mdat' data from file to memory,
//feed this memory buffer to FFmpeg,
//call FFmpeg decode using right codec (following the interleavingMask),
//call FFmpeg decode with the same codec sample_count times (following the samplesPerChunkMask) or split chunk into samples before decode.
//Each time the decode call success it returns number of bytes in decoded sample.
//Thus, possible to get start offset of the next block:
//correct the start offset of the buffered block and then read next block of 'mdat'...

    uint64_t foundCounter = 0; //number of Access Unit (AU) packets found inside 'mdat'
    int64_t offset = mdat->contentOffset;
    //test
    //offset = 0x00000030; //video 0

    int32_t fileBuffMaxlength = 8000000;

    uint8_t *fileBufStart;
    fileBufStart = (uint8_t*) malloc(sizeof(uint8_t) * fileBuffMaxlength);
    if (fileBufStart == NULL)
        throw string("Error. Cannot allocate memory buffer.");
    uint8_t *fileBufEnd;
    fileBufEnd = (uint8_t*) fileBuffMaxlength;

    uint8_t *smplBufStart; //points to buffered sample + few bytes...
    smplBufStart = fileBufStart;
    int32_t sampleBuffMaxlength = 4; //just min size; ffmpeg AV packet size is int32_t not uint32_t
    uint8_t *smplBufEnd;
    smplBufEnd = (uint8_t*) sampleBuffMaxlength;

    size_t readFileResult; //resut of the file read operation
    uint32_t j = 0; //interleaving mask iterator (cyclic) or chunks counter (cyclic)
    uint32_t trackNumber = 0; //get it from interleaving mask
    int32_t minSmplLength; //get it from optMinSampleSize()
    uint32_t samplesCount; //number of samples in current AU
    recoveredSample lengthResultsTmp; //temporary samples results per single chunk
    recoveredSample recoveredSTmp; //temporary storage of the recovered samples parameters

    bool secondTry = false; //to read file to the next block of data
    uint32_t retriesNum = 0; //number of retries with new interleaving mask member

    while (offset < brokenFile.length() -1) {

    reRead:
        //try to buffer maximum amount of data
        fileBuffMaxlength = 8000000; //int32_t

        logMe(LOG_INFO, "Processing at " + to_string(offset) + " of " + to_string(brokenFile.length()) + " bytes...");

        readFileResult = brokenFile.readBlockOfBytesP(offset, fileBufStart, fileBuffMaxlength);
        smplBufStart = fileBufStart; //align buffers after file read
        if (!(readFileResult == fileBuffMaxlength)) {
            logMe(LOG_DBG, "File buffer not filled up during file read.");
            fileBuffMaxlength = readFileResult;
            logMe(LOG_DBG, "readFileResult= " + to_string(readFileResult));
        }
        fileBufEnd = fileBufStart + fileBuffMaxlength;
        smplBufEnd = fileBufEnd;
        logMe(LOG_DBG, "new sampleBuffer size= " + to_string(smplBufEnd - smplBufStart));

        while (fileBuffMaxlength > 0) {

            if (j > (interleavingMask.size() -1 -1)) //-1 because index starts from 0; -1 because mask has additional element at the end
                    j = startLoopPoint; //j = 0 - from the first GOP; j = startLoopPoint - from the other (second) GOP

            trackNumber = interleavingMask[j]; //get track number
            Track &track = trackBoxContent[trackNumber]; //get track

            if (smpltrk == 0) {
                //multi-tracks mode only
                samplesCount = samplesPerChunkMask[j].samplesCount; //samplesPerChunkMask position equal to current interleaving mask possition
            } else {
                //two tracks mode only (1video+1audio); simple track
                samplesCount = (uint32_t) smpltrk;
            }

            //sampleBuffMaxlength = (int32_t) track.sizesMinMax[1] *2; //max twice bigger//* samplesCount; //max sample size * samples count = max possible chunk size; casting due to ffmpeg AV packet size is int32_t
            sampleBuffMaxlength = track.sizesMinMax[1] +10000 + uint32_t(track.sizesMinMax[1] * 0.10); //!!!bigger samples almost not possible; assumption by own experience, subject to change
            if (sampleBuffMaxlength > (smplBufEnd - smplBufStart)) {
                logMe(LOG_DBG, "Current chunk may require bigger buffer storage.");
                sampleBuffMaxlength = (int32_t) (smplBufEnd - smplBufStart); //casting due to ffmpeg AV packet size is int32_t
            }

            //split chunks into samples
            for (uint32_t i = 0; i < samplesCount; i++) {

                minSmplLength = (int32_t) track.optMinSampleSize(track, brokenFile, offset, track.codec.codecParamByFFmpeg);
                //AAC minSampleLength = 6;

                logMe(LOG_DBG, "");
                logMe(LOG_DBG, "Split chunks, sampleBuffer size= " + to_string(smplBufEnd - smplBufStart));
                lengthResultsTmp = track.codec.getSampleSize(smplBufStart, sampleBuffMaxlength, minSmplLength, track.codec.codecParamByFFmpeg, 1, track.nalSizeField);

                logMe(LOG_DBG, "original file, chunk offset= " + to_string(offset)); //absolute
                recoveredSTmp.chunkInBoxOffset = offset - (mdat->contentOffset); //relative to mdat box start
                recoveredSTmp.smplSize = lengthResultsTmp.smplSize;
                recoveredSTmp.smplNumInChunk = lengthResultsTmp.smplNumInChunk;
                recoveredSTmp.smplSend = lengthResultsTmp.smplSend;

                //TODO: compare sample to previous one
                //in case of bad match - the sample belongs to other track
                //and set recoveredSTmp.smplSend = -1; to try next interleaving mask member
                //TODO: detect edges between decoded packets data flow.
                //High gradient expected in case of packets belongs to different tracks.

                //check for keyframes (sync samples)
                uint8_t nalFirstByte = smplBufStart[track.nalSizeField];
                switch (track.codec.codecParamByFFmpeg->codec_id) {
                case AV_CODEC_ID_H264:
                    //brute force instead of ffmpeg
                    //TODO: implement proper recognition by ffmpeg of the H264_NAL_IDR_SLICE (5) and remove this code;
                    //for OBS Studio first nal is H264_NAL_SEI (6) usually...
                    if ((track.smplsRParams.size() == 0) && ((nalFirstByte & (uint8_t) 0x1F) == 6)) {
                        //skip this sample
                        logM(LOG_INFO, "skip from " + to_string(offset));
                        offset += recoveredSTmp.smplSize;
                        smplBufStart += recoveredSTmp.smplSize; //move it to the next sample
                        logMe(LOG_INFO, " to " + to_string(offset));
                        goto reRead; //skipping
                    }
                    if ((nalFirstByte & (uint8_t) 0x1F) == 5) {
                        recoveredSTmp.smplSync = 1; //this is sync sample
                    } else {
                        recoveredSTmp.smplSync = 0;
                    }
                    break;
                case AV_CODEC_ID_HEVC:
                    //TODO: add recognition of the
                    //NAL_IDR_W_RADL (19)
                    //NAL_IDR_N_LP (20)
                    break;
                default:
                    recoveredSTmp.smplSync = 0;
                    break;
                }

                //Send returns 0 or AVERROR_EOF on success,
                //but packet may be broken... (chunks were splitted - always 1 sample per packet)

                if (recoveredSTmp.smplSend == 0) {
                    track.smplsRParams.push_back(recoveredSTmp); //remember sample parameters
                    foundCounter++;

                    logMe(LOG_DBG, "----");
                    logMe(LOG_DBG, "Number of found samples: " + to_string(foundCounter));
                    logMe(LOG_DBG, "Sample number in chunk= " + to_string(recoveredSTmp.smplNumInChunk));
                    logMe(LOG_DBG, "Sample In Box Offset= " + to_string(recoveredSTmp.chunkInBoxOffset));
                    logMe(LOG_DBG, "Sync Sample (IDR)= " + to_string(recoveredSTmp.smplSync));
                    logMe(LOG_DBG, "Sample belongs to Track: " + to_string(trackNumber));
                    logMe(LOG_DBG, "j= " + to_string(j));

                    offset += recoveredSTmp.smplSize;
                    smplBufStart += recoveredSTmp.smplSize; //move it to the next sample
                    logMe(LOG_DBG, "");
                    logMe(LOG_DBG, "next chunk offset= " + to_string(offset)); //absolute
                    secondTry = false;
                    retriesNum = 0;
                } else {
                    logMe(LOG_DBG, "Packed not found. Trying to find next interleaving member...");
                    logMe(LOG_DBG, "offset= " + to_string(offset)); //absolute
                    if (secondTry) {
                        //Sample not found.
                        //TODO:
                        //[complete]re-use this sample acording to next member of the interleaving mask
                        //[?]or look for first track sample (reset j)
                        //[?]or maybe look for first sync sample
                        //and increase 'offset' for this found value

                        //if the sample unrecognized then it belongs to other track...
                        retriesNum++;
                        if (retriesNum > trackBoxContent.size())
                            goto endRead; //all tracks exhausted; stop repair
                        j++; //try next interleaving member (IM)
                        logMe(LOG_INFO, "curr IM -> offset= " +to_string(offset) + "; j= " + to_string(j) + "; IM= " + to_string(trackNumber));
                        goto reRead;
                    } else {
                        //read next block of data
                        logMe(LOG_DBG, "Reading next block of data...");
                        secondTry = true;
                        goto reRead;
                    }
                } //if

              if ((smplBufEnd - smplBufStart) <= 0) {
                  //if end of the fileBuffer reached...
                  logMe(LOG_DBG, "sampleBufferStart - fileBufferEnd. Time to read new block of data from file...");
                  fileBuffMaxlength = -1; //to exit 'while' loop
                  break; //exit 'for' loop
              }
              logMe(LOG_DBG, "----");
            } //for samplesCount

            j++; //next chunk

        } //while (fileBuffMaxlength > 0)

    } //while (offset < brokenFile.length() -1)
endRead:
    free(fileBufStart);

    logMe(LOG_INFO, "----");
    logMe(LOG_INFO, "End of the processing.");
    logMe(LOG_INFO, "");
    logM(LOG_INFO, "Repaired: " + to_string(foundCounter) + " samples ");
    logMe(LOG_INFO, "(~" + to_string((uint32_t)((double_t)offset / brokenFile.length() *100)) + "% of data processed)");
}

void Mp4::saveMovie(string filenameBAD) {
    //repairedBoxesFile - temporal storage for repaired stts, stss an other boxes that require changes
    //the workingBox start offsets should point to this file.
    clFile repairedBoxesFile;
    if (repairedBoxesFile.open(filenameBAD + ".tmp")) {
        //throw string ("Warning! File already exist: ") + filenameBAD + ".tmp";
        logMe(LOG_WRN, "Warning! Temporary file already exist: " + filenameBAD + ".tmp");
        deleteTmps = false; //keep .tmp file
        return;
    } else {
        repairedBoxesFile.close();
    }
    if (!repairedBoxesFile.create(filenameBAD + ".tmp"))
        throw string("Error. Could not create file: ") + filenameBAD + ".tmp";

    //repairedFile - it's a final result of the program
    clFile repairedFile;
    if (repairedFile.open(filenameBAD)) {
        //throw string ("Warning! File already exist: ") + filenameBAD;
        logMe(LOG_WRN, "Warning! File already exist: " + filenameBAD);
        return;
    } else {
        repairedFile.close();
    }
    if (!repairedFile.create(filenameBAD))
        throw string("Error. Could not create file: ") + filenameBAD;

    //build boxes, use next order:
    // 'ftyp' - new, major brand 'isom', compatible brands: 'isomiso2avc1mp41'
    // 'free' - in case of long64 file, it extends to 'mdat'
    // 'mdat' - raw copy content from brokenFile; adjust box size
    // 'moov' - copy from workingFile but rebuild:
    //      'stts'
    //      'stss'
    //      'ctts' - maybe rewrite this block to 'skip'
    //      'stsc'
    //      'stsz' (even if original was stz2)
    //      'stco' ('co64' in case of long64 file)


    //start writing resulting file
//====== restore box of file type (ftyp) ======//
    logMe(LOG_INFO, "restoring 'ftyp' box...");
    Box *ftyp = root->getBoxByType((uint32_t)'ftyp');
    if (ftyp) {
        ftyp->copyBoxRaw(ftyp, workingFile, repairedFile);
    } else {
        //write new 'ftyp' box (with minimum requirements according to ISO/IEC 14496-12 2015)
        //Major_brand='mp41', minor_version=0, and the single compatible brand 'mp41'.
        repairedFile.writeUint(20); //size (5*4 bytes)
        repairedFile.writeUint((uint32_t)'ftyp'); //type
        repairedFile.writeUint((uint32_t)'mp41'); //Major_brand
        repairedFile.writeUint(0); //minor version
        repairedFile.writeUint((uint32_t)'mp41'); //compatible_brand
    }

    int64_t mdatStartPoint = repairedFile.pos(); //all boxes that precede 'mdat' was just written, this is offset of the 'mdat'
    int64_t startPoint; //temporary variable to remeber box offset
    int64_t contentStartPoint; //temporary variable to remeber box content offset
    uint32_t entryCount; //temporary variable to work with in box counters
    int64_t contentEndPoint; //temporary variable to remeber end offset of the box content

    //write to temporary storage
    vector<Box *> trakBoxes = root->getBoxesCollByType((uint32_t)'trak');
    if (!(trackBoxContent.size() == trakBoxes.size()))
        throw ("Error. The root box of tracks was modified after last parsing. Cannot proceed further.");
    for (uint32_t i = 0; i < trackBoxContent.size(); i++) {

//====== restore box of decoding time to sample (stts) ======//
        logMe(LOG_INFO, "restoring 'stts' box...");
        Box *stts = trakBoxes[i]->getBoxByType((uint32_t)'stts');
        if (stts) {
            startPoint = stts->writeBoxHeader(stts, repairedBoxesFile); //write header
            contentStartPoint = repairedBoxesFile.pos(); //remember position to write entry_count later
            entryCount = 0; //entryies counter, default is 0
            repairedBoxesFile.writeUint(entryCount); //reserve space for entry_count field of this box

            logMe(LOG_INFO, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track
            uint32_t smplCount = track.smplsRParams.size(); //number of recovered samples (per track)
            //assuming all samples of the same delta
            repairedBoxesFile.writeUint(smplCount); //sample_count; maximum 2^32 per track
            repairedBoxesFile.writeUint(track.times[0]); //sample_delta (from working file, use first delta)
            entryCount++; //actually, single entry was written...

            track.duration64 = (uint64_t) smplCount * track.times[0]; //recalculate track duration
            if ((track.duration64 >> 32) == 0) {
                track.duration = (uint32_t) track.duration64; //it fits the uint32_t
                track.duration64 = 0;
            } else {
                track.duration = 0;
            }
            logMe(LOG_DBG, "repaired track timescale = " + to_string(track.timescale));
            logMe(LOG_DBG, "repaired track duration = " + to_string(track.duration));
            logMe(LOG_DBG, "repaired track duration64 = " + to_string(track.duration64));

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            repairedBoxesFile.seek(contentStartPoint);
            repairedBoxesFile.writeUint(entryCount); //update entry_count field of the box content
            repairedBoxesFile.seek(contentEndPoint); //return to the end of the box
            stts->writeBoxHeaderSize(stts, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            stts->startOffset = startPoint; //used by copyBoxRaw()
            stts->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if stts

//====== restore box of sync samples (stss) ======//
        logMe(LOG_INFO, "restoring 'stss' box...");
        Box *stss = trakBoxes[i]->getBoxByType((uint32_t)'stss');
        if (stss) {
            startPoint = stss->writeBoxHeader(stss, repairedBoxesFile); //write header
            contentStartPoint = repairedBoxesFile.pos(); //remember position to write entry_count later
            entryCount = 0; //entryies counter, default is 0
            repairedBoxesFile.writeUint(entryCount); //reserve space for entry_count field of this box

            logMe(LOG_INFO, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track
            int32_t syncSmpl;
            for (uint32_t j = 0; j < track.smplsRParams.size(); j++) {
                syncSmpl = track.smplsRParams[j].smplSync; //sample type (sync/other)
                if (syncSmpl == 1) {
                    repairedBoxesFile.writeUint(j +1); //samples starts from 1
                    entryCount++;
                    logMe(LOG_DBG, to_string(syncSmpl) + " sync sample # " + to_string(j +1));
                }
            } //for j
            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            repairedBoxesFile.seek(contentStartPoint);
            repairedBoxesFile.writeUint(entryCount); //update entry_count field of the box content
            repairedBoxesFile.seek(contentEndPoint); //return to the end of the box
            stss->writeBoxHeaderSize(stss, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            stss->startOffset = startPoint; //used by copyBoxRaw()
            stss->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if stss

//====== restore box of composition time to sample (ctts) ======//
        logMe(LOG_INFO, "restoring 'ctts' box...");
        Box *ctts = trakBoxes[i]->getBoxByType((uint32_t)'ctts');
        if (ctts) {
            //rewrite 'ctts' block to 'free' type to skip it completely while preserving box structure and content
            ctts->type = (uint32_t)'free';
        } //if ctts

//====== restore box of sample to chunk (stsc) ======//
        logMe(LOG_INFO, "restoring 'stsc' box...");
        Box *stsc = trakBoxes[i]->getBoxByType((uint32_t)'stsc');
        if (stsc) {
            startPoint = stsc->writeBoxHeader(stsc, repairedBoxesFile); //write header
            contentStartPoint = repairedBoxesFile.pos(); //remember position to write entry_count later
            entryCount = 0; //entryies counter, default is 0
            repairedBoxesFile.writeUint(entryCount); //reserve space for entry_count field of this box

            logMe(LOG_INFO, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track
            repairedBoxesFile.writeUint(1); //first chunk that shares same parameters is always the first sample of the track
            repairedBoxesFile.writeUint(1); //chunks was splitted, so only one sample per chunk
            repairedBoxesFile.writeUint(track.samplesFPD[0].descriptionIndex); //get first element 'sample description index' and use it for all samples
            entryCount++;

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            repairedBoxesFile.seek(contentStartPoint);
            repairedBoxesFile.writeUint(entryCount); //update entry_count field of the box content
            repairedBoxesFile.seek(contentEndPoint); //return to the end of the box
            stsc->writeBoxHeaderSize(stsc, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            stsc->startOffset = startPoint; //used by copyBoxRaw()
            stsc->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if stsc

//TODO: add support for 'stz2' box
//====== restore box of samples size (stsz) ======//
        logMe(LOG_INFO, "restoring 'stsz' box...");
        Box *stz2 = trakBoxes[i]->getBoxByType((uint32_t)'stz2');
        if (stz2)
            //rewrite 'stz2' block to 'stsz' type; add support for 'stz2' later
            stz2->type = (uint32_t)'stsz';

        Box *stsz = trakBoxes[i]->getBoxByType((uint32_t)'stsz');
        if (stsz) {
            startPoint = stsz->writeBoxHeader(stsz, repairedBoxesFile); //write header
            repairedBoxesFile.writeUint(0); //all samples may have different sizes (let's write not optimized box)
            contentStartPoint = repairedBoxesFile.pos(); //remember position to write entry_count later
            entryCount = 0; //entryies counter, default is 0; here equals to sample_count
            repairedBoxesFile.writeUint(entryCount); //reserve space for entry_count field of this box

            logMe(LOG_INFO, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track
            for (uint32_t j = 0; j < track.smplsRParams.size(); j++) {
                repairedBoxesFile.writeUint(track.smplsRParams[j].smplSize); //write repaired sample size
                entryCount++;
            } //for j

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            repairedBoxesFile.seek(contentStartPoint);
            repairedBoxesFile.writeUint(entryCount); //update entry_count field of the box content
            repairedBoxesFile.seek(contentEndPoint); //return to the end of the box
            stsz->writeBoxHeaderSize(stsz, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            stsz->startOffset = startPoint; //used by copyBoxRaw()
            stsz->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if stsz

//====== restore box of samples offsets (stco, co64) ======//
        logMe(LOG_INFO, "restoring 'stco'/'co64' box...");
        Box *stco = trakBoxes[i]->getBoxByType((uint32_t)'stco');
        Box *co64 = trakBoxes[i]->getBoxByType((uint32_t)'co64');

        Track &track = trackBoxContent[i]; //get track
        int64_t maxChunkOffset = 0; //max possible offset;
        if (track.smplsRParams.size() > 0) { //to make sure that at least one entry to smplsRParams exist
            maxChunkOffset = mdatStartPoint +8 +8 + track.smplsRParams[track.smplsRParams.size() -1].chunkInBoxOffset; //get max offset; +8 is 'free' box space +8 bytes of short header just in case of large64 box
        }
        bool co64Box = true; //flag what kind of the box should be written
        if ((maxChunkOffset >> 32) == 0) {
            //write 'stco' box
            logMe(LOG_DBG,"will use 'stco' box");
            co64Box = false;
        } else {
            //write 'co64' box
            logMe(LOG_DBG,"will use 'co64' box");
            //co64Box = true;
        }
        if (stco && co64Box)
            stco->type = (uint32_t)'co64'; //re-brand the box

        if (co64 && !co64Box)
            co64->type = (uint32_t)'stco'; //re-brand the box

        //re-define the 'stco' pointer
        if (co64Box) {
            stco = trakBoxes[i]->getBoxByType((uint32_t)'co64');
        } else {
            stco = trakBoxes[i]->getBoxByType((uint32_t)'stco');
        }

        if (stco) {
            startPoint = stco->writeBoxHeader(stco, repairedBoxesFile); //write header
            contentStartPoint = repairedBoxesFile.pos(); //remember position to write entry_count later
            entryCount = 0; //entryies counter, default is 0; here equals to sample_count
            repairedBoxesFile.writeUint(entryCount); //reserve space for entry_count field of this box

            logMe(LOG_DBG, "Track " + to_string(i));
            for (uint32_t j = 0; j < track.smplsRParams.size(); j++) {
                if (co64Box) {
                    repairedBoxesFile.writeUint64((uint64_t) (mdatStartPoint +8 +8 + track.smplsRParams[j].chunkInBoxOffset)); //write repaired sample size (casting to uint64_t); +8 is 'free' box space +8 bytes of short header just in case of large64 box
                } else {
                    repairedBoxesFile.writeUint((uint32_t) (mdatStartPoint +8 +8 + track.smplsRParams[j].chunkInBoxOffset)); //write repaired sample size (casting to uint32_t due to box format); +8 is 'free' box space +8 bytes of short header just in case of large64 box
                }
                entryCount++;
            } //for j

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            repairedBoxesFile.seek(contentStartPoint);
            repairedBoxesFile.writeUint(entryCount); //update entry_count field of the box content
            repairedBoxesFile.seek(contentEndPoint); //return to the end of the box
            stco->writeBoxHeaderSize(stco, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            stco->startOffset = startPoint; //used by copyBoxRaw()
            stco->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } else {
          logMe(LOG_ERR,"Error. The working file dosen't contain box of samples offsets");
        } //if stco

    } //for i

    //correct overall movie length
    double_t maxTrkTime = 0; //temporary variable to find which track has maximum length in seconds
    for (uint32_t i = 0; i < trackBoxContent.size(); i++) {
        //find longest track (track_time = track_duration / track_timescale)
        //convert longest track_duration to timescale of the movie (duration = track_time * timescale)
        //update movie duration
        uint64_t trkDur; //temporary variable to get actuall duration value
        Track &track = trackBoxContent[i]; //get track
        if (track.duration > 0) {
            trkDur = track.duration;
        } else {
            trkDur = track.duration64;
        }
        track.trkTime = (double_t) trkDur / track.timescale; //track's repaided length in seconds (casting to double_t)
        logMe(LOG_INFO,"repaired track " + to_string(i) + ", length (seconds): " + to_string(track.trkTime));
        if (maxTrkTime < track.trkTime)
            maxTrkTime = track.trkTime;
    }

    duration64 = (uint64_t) (maxTrkTime * timescale); //'mvhd' box (movie) duration by proper timescale; the longest track used
    if ((duration64 >> 32) == 0) {
        logMe(LOG_DBG,"Duration of the movie fits 32 bits");
        duration = (uint32_t) duration64;
        duration64 = 0;
    } else {
        logMe(LOG_DBG,"Duration of the movie fits 64 bits");
        duration = 0;
    }

    //now length of the tracks updated, restore boxes that has this fields of duration and timescale
    int64_t otherOffset; //offset to specific block of data inside 'mvhd', 'tkhd' and 'mdhd'

    //restore 'mvhd' (single instance)
//====== restore box of movie header (mvhd) ======//
    logMe(LOG_INFO, "restoring 'mvhd' box...");
    Box *mvhd = root->getBoxByType((uint32_t)'mvhd');
    if (mvhd) {
        //remember offset of the important data
        if ((mvhd->vpFlags) >> 24 == 1) {
            otherOffset = mvhd->contentOffset +8 +8 +4 +8; //some fields are 64-bits long
        } else {
            otherOffset = mvhd->contentOffset +4 +4 +4 +4;
        }
        //update box version
        if (duration == 0) {
            mvhd->vpFlags = (mvhd->vpFlags) & 0x1FFFFFF; //set box version 1
        } else {
            mvhd->vpFlags = (mvhd->vpFlags) & 0x0FFFFFF; //set box version 0
        }

        startPoint = mvhd->writeBoxHeader(mvhd, repairedBoxesFile); //write header
        if (duration == 0) {
                //writing FullBox version 1
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint64(42); //creation time
                repairedBoxesFile.writeUint64(42); //modification time
                repairedBoxesFile.writeUint(timescale); //movie timescale
                repairedBoxesFile.writeUint64(duration64); //movie duration
            } else {
                //writing FullBox version 0
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint(42); //creation time
                repairedBoxesFile.writeUint(42); //modification time
                repairedBoxesFile.writeUint(timescale); //movie timescale
                repairedBoxesFile.writeUint(duration); //movie duration
            }
            //copy all other info from working file (20 * 4 bytes)
            uint8_t *smallbuff;
            smallbuff = (uint8_t*) malloc(sizeof(uint8_t) * 80);
            if (smallbuff == NULL)
                throw string("Error. Cannot allocate memory buffer (copy).");

            size_t readRet = workingFile.readBlockOfBytesP(otherOffset, smallbuff, 80);
            logMe(LOG_DBG, "copy readRet=" + to_string(readRet));
            if (readRet > 0) {
                size_t writeRet = repairedBoxesFile.writeBlockOfBytes(smallbuff, readRet);
                if (!(writeRet == readRet))
                    throw string("Error. Copy write error.");
            }
            free(smallbuff);

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            mvhd->writeBoxHeaderSize(mvhd, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            mvhd->startOffset = startPoint; //used by copyBoxRaw()
            mvhd->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if mvhd

    //restore 'tkhd' and 'mdhd' per track
    for (uint32_t i = 0; i < trackBoxContent.size(); i++) {

//====== restore box of track header (tkhd) ======//
        logMe(LOG_INFO, "restoring 'tkhd' box...");
        Box *tkhd = trakBoxes[i]->getBoxByType((uint32_t)'tkhd');
        if (tkhd) {
            //remember offset of the important data
            if ((tkhd->vpFlags) >> 24 == 1) {
                otherOffset = tkhd->contentOffset +8 +8 +4 +4 +8; //some fields are 64-bits long
            } else {
                otherOffset = tkhd->contentOffset +4 +4 +4 +4 +4;
            }
            //update box version
            if (duration == 0) {
                tkhd->vpFlags = (tkhd->vpFlags) & 0x1FFFFFF; //set box version 1
            } else {
                tkhd->vpFlags = (tkhd->vpFlags) & 0x0FFFFFF; //set box version 0
            }

            logMe(LOG_DBG, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track

            startPoint = tkhd->writeBoxHeader(tkhd, repairedBoxesFile); //write header

            uint64_t trkMovDur = (uint64_t) (track.trkTime * timescale); //track duration in timescale of the movie ('mvhd' box)
            if ((trkMovDur) >> 32 == 0) {
                //writing FullBox version 0
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint(42); //creation time
                repairedBoxesFile.writeUint(42); //modification time
                repairedBoxesFile.writeUint(track.trkID); //track ID
                repairedBoxesFile.writeUint(0); //reserved
                repairedBoxesFile.writeUint((uint32_t) trkMovDur); //fits 32-bits
            } else {
                //writing FullBox version 1
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint64(42); //creation time
                repairedBoxesFile.writeUint64(42); //modification time
                repairedBoxesFile.writeUint(track.trkID); //track ID
                repairedBoxesFile.writeUint(0); //reserved
                repairedBoxesFile.writeUint64(trkMovDur); //64-bit
            }
            //copy all other info from working file (15 * 4 bytes)
            uint8_t *smallbuff;
            smallbuff = (uint8_t*) malloc(sizeof(uint8_t) * 60);
            if (smallbuff == NULL)
                throw string("Error. Cannot allocate memory buffer (copy).");

            size_t readRet = workingFile.readBlockOfBytesP(otherOffset, smallbuff, 60);
            logMe(LOG_DBG, "copy readRet=" + to_string(readRet));
            if (readRet > 0) {
                size_t writeRet = repairedBoxesFile.writeBlockOfBytes(smallbuff, readRet);
                if (!(writeRet == readRet))
                    throw string("Error. Copy write error.");
            }
            free(smallbuff);

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            tkhd->writeBoxHeaderSize(tkhd, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            tkhd->startOffset = startPoint; //used by copyBoxRaw()
            tkhd->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if tkhd

//====== restore box of media header (mdhd) ======//
        logMe(LOG_INFO, "restoring 'mdhd' box...");
        Box *mdhd = trakBoxes[i]->getBoxByType((uint32_t)'mdhd');
        if (mdhd) {
            //remember offset of the important data
            if ((mdhd->vpFlags) >> 24 == 1) {
                otherOffset = mdhd->contentOffset +8 +8 +4 +8; //some fields are 64-bits long
            } else {
                otherOffset = mdhd->contentOffset +4 +4 +4 +4;
            }
            //update box version
            if (duration == 0) {
                mdhd->vpFlags = (mdhd->vpFlags) & 0x1FFFFFF; //set box version 1
            } else {
                mdhd->vpFlags = (mdhd->vpFlags) & 0x0FFFFFF; //set box version 0
            }

            logMe(LOG_DBG, "Track " + to_string(i));
            Track &track = trackBoxContent[i]; //get track

            startPoint = mdhd->writeBoxHeader(mdhd, repairedBoxesFile); //write header

            //uint64_t trkMovDur = (uint64_t) (track.trkTime * timescale); //track duration in timescale of the movie ('mvhd' box)
            if (track.duration == 0) {
                //writing FullBox version 1
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint64(42); //creation time
                repairedBoxesFile.writeUint64(42); //modification time
                repairedBoxesFile.writeUint(track.timescale); //track timescale
                repairedBoxesFile.writeUint64(track.duration64); //track duration
            } else {
                //writing FullBox version 0
                //TODO: write actual creation and modification time
                repairedBoxesFile.writeUint(42); //creation time
                repairedBoxesFile.writeUint(42); //modification time
                repairedBoxesFile.writeUint(track.timescale); //track timescale
                repairedBoxesFile.writeUint(track.duration); //track duration
            }
            //copy all other info from working file (1 * 4 bytes)
            repairedBoxesFile.writeUint(workingFile.readUintP(otherOffset));

            contentEndPoint = repairedBoxesFile.pos(); //remember position to call writeBoxHeaderSize() later
            mdhd->writeBoxHeaderSize(mdhd, startPoint, repairedBoxesFile); //update size of the box
            //update root boxes offsets
            mdhd->startOffset = startPoint; //used by copyBoxRaw()
            mdhd->actualSize = (uint64_t) (contentEndPoint - startPoint); //used by copyBoxRaw()
        } //if mdhd
    } //for i

    //open temporary storage for reading
    repairedBoxesFile.close();
    if (!repairedBoxesFile.open(filenameBAD + ".tmp"))
        throw string ("Error! Cannot open file for reading: ") + filenameBAD + ".tmp";

    //copy whole 'mdat' from broken file to repaired
    Box *mdat = mdatBAD;
    //check for uncomplete 'mdat' box size info
    if ((mdat->size != 0) && (mdat->contentSize < 1024LL)) {
        logM(LOG_INFO, "Too small 'mdat' content size info detected (bytes): ");
        logMe(LOG_INFO, to_string(mdat->contentSize));

        mdat->actualSize = brokenFile.length() - mdat->startOffset; //startOffset max = 2^63 due to file.length()
        mdat->contentSize = (int64_t) mdat->actualSize - (mdat->contentOffset - mdat->startOffset);

        logM(LOG_INFO, "Info was updated. New 'mdat' content size (bytes): ");
        logMe(LOG_INFO, to_string(mdat->contentSize));

    }
    logMe(LOG_INFO, "copying 'mdat' box...");
    mdat->copyBoxFixSize(mdat, brokenFile, repairedFile);

    //iterate through all working file boxes (root) and copy them to repairedFile

    //remember start offset
    //write 'moov' header
    //set size 0, to the end of the box (anyway 'moov' is small - it always fits in 4 bytes)

    //work now with 'moov' and its content, all other boxes has been written
    logMe(LOG_INFO, "building 'moov' box...");
    Box *moov = root->getBoxByType((uint32_t)'moov');

    //writing 'moov' box header
    //
    startPoint = moov->writeBoxHeader(moov, repairedFile); //remember position of the box to adjust its header later

    //write 'moov' content
    //
    moov->copyBoxes(workingFile, moov, repairedBoxesFile, repairedFile); //repairedBoxesFile has only corrected boxes

    //from the start offset correct the 'moov' box size (or skip this step because it is last block).
    //
    moov->writeBoxHeaderSize(moov, startPoint, repairedFile);
    //end of writing resulting file

    workingFile.close();
    brokenFile.close();
    repairedFile.close();
    repairedBoxesFile.close();
    complete = true; //task complete
}

void Mp4::removeTmps(string filenameTmp) {
    if (deleteTmps) {
        logMe(LOG_INFO, "Removing .tmp files...");
        if (remove(filenameTmp.c_str()) == 0) {
            logMe(LOG_INFO, "...remove complete.");
        } else {
            logMe(LOG_ERR, "Error deleting .tmp file ");
        }
    }
}
