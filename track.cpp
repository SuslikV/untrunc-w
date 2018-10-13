/*
	Untrunc - track.cpp
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#include "track.h"
#include "atom.h"
#include "portable_endian.h"
#include "loginfo.h"

#include <iostream>
#include <vector>
#include <string.h>

using namespace std;

recoveredSample Codec::getSampleSize(uint8_t *inbuf, int32_t blockLength, int32_t minLength, AVCodecParameters *streamCodec, uint32_t samplesCount, uint8_t nalSizeField) {
    recoveredSample sizeResults;
    recoveredSample recoveredSTmp;

    AVCodec *codec;
    AVCodecContext *c = NULL;

    //uint8_t inbufmpeg[inbuf + AV_INPUT_BUFFER_PADDING_SIZE];
    //uint8_t *inbuf;
    //inbuf = inbufRaw;

    codec = avcodec_find_decoder(streamCodec->codec_id);
    if (!codec) {
        throw string("Codec not found! id:")+to_string(streamCodec->codec_id);
        //exit(1);
    }

    c = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(c, streamCodec);
    if (!c) {
        throw string("Could not allocate codec context!");
        //exit(1);
    }

    logMe(LOG_DBG, "CodecID " + to_string(streamCodec->codec_id));
    logMe(LOG_DBG, codec->long_name);

    if (avcodec_open2(c, codec, NULL) < 0) {
        throw string("Error. Could not open codec!");
        //exit(1);
    }

    AVPacket *avpkt;
    AVFrame *decoded_frame = NULL;

    decoded_frame = av_frame_alloc();
    //av_init_packet(avpkt);
    avpkt = av_packet_alloc();
    if (!avpkt)
        throw string("Error. AVpacket allocate failure.");

    avpkt->data = inbuf;
    avpkt->size = minLength; //put minimum size of the packet here
    logMe(LOG_DBG, "        blockLength " + to_string(blockLength));
    logMe(LOG_DBG, "initial avpkt->size " + to_string(avpkt->size));
    int ret;
    ret = -1; //assume samples not found, size unknown

    uint8_t nalFirstByte;//NAL first byte to get forbidden_zero_bit, nal_ref_idc and nal_type

    switch (streamCodec->codec_id) {
    case AV_CODEC_ID_H264:
        //try to recognize AVC sample and confirm its size without decoding it

        if (h264alg == 10) {
            //v1.0
            logMe(LOG_DBG, "v1.0 in use");
            //!!!___works only for: nalSizeField = 4 bytes; max sample size < 16777215; nal_type = [1..20]; min nal size = 5
            if ((*inbuf) == 0 &&
                ((*inbuf +4) & 0x1F) < 21) {

                ret = 0; //all OK
            }

        } else if (h264alg == 11) {
            //v1.1
            logMe(LOG_DBG, "v1.1 in use");
            //!!!___works only for: nalSizeField = 4 bytes; max sample size < 16777215; nal_type = [1..20]; min nal size = 5
            if (inbuf[0] == 0 &&
                (inbuf[4] & 0x1F) < 21 &&
                (inbuf[4] & 0x1F) > 0){

                ret = 0; //all OK
            }

        } else {
            //v1.2
            logMe(LOG_DBG, "v1.2 in use");
            nalFirstByte = inbuf[nalSizeField];
            logMe(LOG_DBG, "nal first byte " + to_string(nalFirstByte));

            //nal size lower then max allowed sample size for the track and
            //nal_type (5-bits) in range [1..21] and
            //nal_forbidden_bit (1-bit) equals to 0
            if ((minLength <= blockLength) &&
                (nalFirstByte & 0x1F) < 22 &&
                (nalFirstByte & 0x1F) > 0 &&
                (nalFirstByte & 0x80) == 0) {

                switch (nalFirstByte & 0x1F) {

                case 6:
                case 9:
                case 10:
                case 11:
                case 12:
                    //nal_ref_idc (2-bits)
                    if ((nalFirstByte & 0x60) == 0)
                        ret = 0;
                    break;

                case 5:
                case 7:
                case 8:
                case 13:
                case 15:
                    //nal_ref_idc (2-bits)
                    if ((nalFirstByte & 0x60) > 0)
                        ret = 0;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                    //nal_ref_idc (2-bits)
                    //any value
                        ret = 0;
                    break;

                default:
                    //16,17,18 - unknown NAL
                    ret = -1;
                    break;
                }
            } //if
        }
        break;

    //TODO: add AV_CODEC_ID_HEVC and others codecs

    case AV_CODEC_ID_AAC:
        if (inbuf[0] == 0)
            break; //skip zero byte tracks. Let's assume that is not AAC track (ISO/IEC 13818-7 2004)

        //unknown exact sample size, so try to use ffmpeg by slowly increasing packet size
        while ((avpkt->size <= blockLength) && (avpkt->size > 0) && (ret < 0) && !(ret == AVERROR_EOF)) {
            //logMe(LOG_DBG, "avpkt->size = " + to_string(avpkt->size)); //too verbose...
            ret = avcodec_send_packet(c, avpkt);
            //Here AVERROR(EAGAIN) return value means that decoder's buffer full and it's time to
            //call avcodec_receive_frame. Then, probably, will be possible to send this packet again...
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    logMe(LOG_DBG, "send AVERROR_EOF");
                }
                if (ret == AVERROR(EAGAIN)) {
                    logMe(LOG_DBG, "send AVERROR(EAGAIN)");
                }
                avpkt->size++;
            }
        } //while
        break;

    default:
        //unknown exact sample size, so try to use ffmpeg by slowly increasing packet size
        while ((avpkt->size <= blockLength) && (avpkt->size > 0) && (ret < 0) && !(ret == AVERROR_EOF)) {
            //logMe(LOG_DBG, "avpkt->size = " + to_string(avpkt->size)); //too verbose...
            ret = avcodec_send_packet(c, avpkt);
            //Here AVERROR(EAGAIN) return value means that decoder's buffer full and it's time to
            //call avcodec_receive_frame. Then, probably, will be possible to send this packet again...
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    logMe(LOG_DBG, "send AVERROR_EOF");
                }
                if (ret == AVERROR(EAGAIN)) {
                    logMe(LOG_DBG, "send AVERROR(EAGAIN)");
                }
                avpkt->size++;
            }
        } //while
    } //switch

    recoveredSTmp.smplSize = avpkt->size; //send was success or not
    recoveredSTmp.smplSend = ret;
    logMe(LOG_DBG, "send packet ret = " + to_string(ret));

    int32_t len = 0;

    for (uint32_t i = 0; i < samplesCount; i++) {
        //TODO: enable this code later
        //len = avcodec_receive_frame(c, decoded_frame);

        //Here AVERROR(EAGAIN) return value means that new input data is required to
        //return new output.
        if ((len < 0) && (len != AVERROR(EAGAIN)) && (len != AVERROR_EOF)) {
            logMe(LOG_DBG, "Cannot decode the frame...");
        }
        if (len == AVERROR(EAGAIN)) len = avpkt->size; //sample consumes whole packet, this is equal to 1 sample per chunk

        recoveredSTmp.smplNumInChunk = i +1;

        sizeResults = recoveredSTmp;

        logMe(LOG_DBG, "len = " + to_string(len));
    } //for

    //TODO:
    //keyframe, IDR (check it later)
    //NALUnitType
    //by H264Context
    //by HEVCContext
    //check for timestamp, there is no two same timestamps
    //there is no big gaps between timestamps (probably sample just missed)

    //special flushPacket which signals the end of the stream
    //avpkt.data = NULL;
    //avpkt.size = 0;
    //
    avcodec_free_context(&c);
    av_frame_free(&decoded_frame);
    av_packet_free(&avpkt);

    return sizeResults;
}

uint32_t Track::optMinSampleSize(Track &track, clFile &brokenFile, uint64_t sampleOffset, AVCodecParameters *streamCodec) {
    uint32_t sampleSizeAssumption; //expected size of the sample
    uint32_t bb;

    switch(streamCodec->codec_id) {
    case AV_CODEC_ID_H264:
        //Place here optimizations for minimum sample size
        //
        //read NAL-size field and return its value as minimal size for the sample
        //the NAL-size field can be 1, 2 or 4 bytes long
        //nalSizeField stores lenght of this field per track
        sampleSizeAssumption = brokenFile.readUintP(sampleOffset);
        logMe(LOG_DBG, "sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        bb = 8 * (4 - track.nalSizeField);
        logMe(LOG_DBG, " field length = " + to_string(track.nalSizeField));
        sampleSizeAssumption = sampleSizeAssumption >> bb;
        sampleSizeAssumption += track.nalSizeField; //sample size should include NAL-size field
        logMe(LOG_DBG, "First sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        if (sampleSizeAssumption > (track.sizesMinMax[1] *2)) { //twice bigger video samples almost not possible
            sampleSizeAssumption = (track.sizesMinMax[1] *2);
            logMe(LOG_DBG, "Sample size assumption exceeds max sample size of the working example");
            logMe(LOG_DBG, "New sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        }
        return sampleSizeAssumption;
        break;

    case AV_CODEC_ID_HEVC:
        //Place here optimizations for minimum sample size
        sampleSizeAssumption = 4;
        logMe(LOG_DBG, "First sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        if (sampleSizeAssumption > (track.sizesMinMax[1] *2)) { //twice bigger video samples almost not possible
            sampleSizeAssumption = (track.sizesMinMax[1] *2);
            logMe(LOG_DBG, "Sample size assumption exceeds max sample size of the working example");
            logMe(LOG_DBG, "New sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        }
        return sampleSizeAssumption;
        break;

    case AV_CODEC_ID_AAC:
        //Place here optimizations for minimum sample size
        //TODO: optimizations on AAC packet size, maybe...
        sampleSizeAssumption = 6;
        if ((track.sizesMinMax[0] < 6) && (track.sizesMinMax[0] > 1)) //4 is possible value
             sampleSizeAssumption = track.sizesMinMax[0];
        logMe(LOG_DBG, "First sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        if (sampleSizeAssumption > 12000) { //!!!assumption based on own experience; subject to change
            sampleSizeAssumption = (track.sizesMinMax[1] *2); //make it twice bigger
            logMe(LOG_DBG, "Sample size assumption exceeds max sample size of the working example (" + to_string(track.sizesMinMax[1]) + ")");
            logMe(LOG_DBG, "New sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        }
        return sampleSizeAssumption;
        break;

    case AV_CODEC_ID_PCM_S16LE:
        //Place here optimizations for minimum sample size
        //format: 'sowt' ('soun' sound media handler_type); uncompressed PCM signed 16-bit little-endian
        sampleSizeAssumption = track.sizesMinMax[0]; //minimum and always fixed
        logMe(LOG_DBG, "First sampleSizeAssumption = " + to_string(sampleSizeAssumption));
        return sampleSizeAssumption;
        break;

    default:
        return 4; //sampleSizeAssumption for unknown codec
    }
}

void Track::parse(Track *track, Box *t, clFile &workingFile) {

    logMe(LOG_DBG, "===============");

    Box *tkhd = t->getBoxByType((uint32_t)'tkhd');
    if(!tkhd)
        throw string("No 'tkhd' box inside current 'trak': unknown track ID");

    if (((tkhd->vpFlags) >> 24) == 1) {
        //for FullBox version 1
        track->trkID = workingFile.readUintP((tkhd->contentOffset) +8 +8);
    } else {
        track->trkID = workingFile.readUintP((tkhd->contentOffset) +4 +4);
    }
    logMe(LOG_DBG, "track ID = " + to_string(track->trkID));

    Box *mdhd = t->getBoxByType((uint32_t)'mdhd');
	if(!mdhd)
        throw string("No 'mdhd' box inside current 'trak': unknown track duration and timescale");

    //test
    //AAC (857 samples of 1024 time delta + 1 sample of 0 time delta at the end)
    //track timescale = 44100
    //track duration = 877568
    //time ~19.899501133786848072562358276644
    //H264 (498 samples of 512 time delta)
    //track timescale = 12800
    //track duration = 254976
    //time ~19.92

    if (((mdhd->vpFlags) >> 24) == 1) {
        //for FullBox version 1
        timescale = workingFile.readUintP((mdhd->contentOffset) +8 +8);
        duration64 = workingFile.readUint64(); //always lies next to timescale
    } else {
        timescale = workingFile.readUintP((mdhd->contentOffset) +4 +4);
        duration = workingFile.readUint(); //always lies next to timescale
    }

    logMe(LOG_DBG, "track timescale = " + to_string(timescale));
    logMe(LOG_DBG, "track duration = " + to_string(duration));
    logMe(LOG_DBG, "track duration64 = " + to_string(duration64));

    track->times = getSampleDeltas(t, workingFile);
    track->keyframes = getKeyframes(t, workingFile);
    //TODO: add option to define custom min and max sizes
    track->sizesMinMax = getMinMaxSTSZ(t, workingFile);
    logMe(LOG_INFO, "Samples size: MIN " + to_string(track->sizesMinMax[0]) + "; MAX " + to_string(track->sizesMinMax[1]));
    track->offsets = getChunkOffsets(t, workingFile);
    if (track->offsets.size() == 0) {
        logMe(LOG_INFO, "Chunks offset box 'stco' empty. Trying 'co64' box...");
        track->longOffsets64 = getChunkOffsets64(t, workingFile);
        track->useOffsets64 = true;
    } else {
        track->useOffsets64 = false;
    };
    track->samplesFPD = getSamplesNum(t, workingFile);
    //for (int i=0; i<samplesFPD.size();i++) {
        //logMe(LOG_DBG,  to_string(samplesFPD[i].firstChunk) + " ");
        //logMe(LOG_DBG, to_string(samplesFPD[i].samplesCount) + " ");
        //logMe(LOG_DBG, to_string(samplesFPD[i].descriptionIndex) + "  ");
    //}
    logMe(LOG_DBG, "");

    track->nalSizeField = getNalSizeField(track, t, workingFile);
    logMe(LOG_DBG, "nalSizeField= " + to_string(track->nalSizeField));

} //temporal end.
  //By now, I don't see any reason to continue parsing working tracks,
  //because we can get all we need from FFmpeg...

//not parsing, just collection of deltas from the 'stts' box
vector<uint32_t> Track::getSampleDeltas(Box *t, clFile &workingFile) {
    vector<uint32_t> sampleTimes;
    //decoding time to sample
    Box *stts = t->getBoxByType((uint32_t)'stts');
    if (!stts)
        throw string("Missing sample to time box ('stts' not found)");

    uint32_t entries = workingFile.readUintP(stts->contentOffset);
    logMe(LOG_INFO, "number of entries (stts) = " + to_string(entries));
    for(uint32_t i = 0; i < entries; i++) {
        uint32_t nsamples = workingFile.readUint(); //sample_count
        logMe(LOG_DBG, "nsamples = " + to_string(nsamples));
        uint32_t sampleDelta = workingFile.readUint(); //sample_delta
        logMe(LOG_DBG, "sample delta = " + to_string(sampleDelta));
        //for(int j = 0; j < nsamples; j++)
        sampleTimes.push_back(sampleDelta); //just pushing deltas without samples count for this deltas
    }

    return sampleTimes;
}

vector<uint32_t> Track::getKeyframes(Box *t, clFile &workingFile) {
    vector<uint32_t> sampleKey;
    Box *stss = t->getBoxByType((uint32_t)'stss');
    if (!stss)
        return sampleKey;

    uint32_t entries = workingFile.readUintP(stss->contentOffset);
    logMe(LOG_INFO, "number of entries (stss) = " + to_string(entries));
    for(uint32_t i = 0; i < entries; i++)
        sampleKey.push_back(workingFile.readUint()); //sample_number

    return sampleKey;
}

//not parsing, just looking for min/max values
std::vector<uint32_t> Track::getMinMaxSTSZ(Box *t, clFile &workingFile) {
    vector<uint32_t> sampleMinMaxSizes;
    uint32_t sampleOfMinSize;
    uint32_t sampleOfMaxSize;
    uint32_t currentSampleSize;

    Box *stsz = t->getBoxByType((uint32_t)'stsz');
    if (stsz) {
        uint32_t defaultSize = workingFile.readUintP(stsz->contentOffset); //sample_size
        logMe(LOG_DBG, "default sample size (stsz) = " + to_string(defaultSize));
        uint32_t entries = workingFile.readUint(); //sample_count
        logMe(LOG_INFO, "number of entries (stsz) = " + to_string(entries));

        if (defaultSize == 0) {
            sampleOfMinSize = workingFile.readUint(); //read first entry
            sampleOfMaxSize = sampleOfMinSize;
            //read all next entries and find min/max values
            for (uint32_t i = 1; i < entries; i++) {
                currentSampleSize = workingFile.readUint(); //entry_size
                if (currentSampleSize < sampleOfMinSize) {
                    sampleOfMinSize = currentSampleSize;
                }
                if (currentSampleSize > sampleOfMaxSize) {
                    sampleOfMaxSize = currentSampleSize;
                }
            }
            sampleMinMaxSizes.push_back(sampleOfMinSize); //[0] is min
            sampleMinMaxSizes.push_back(sampleOfMaxSize); //[0] is max
        } else {
            sampleMinMaxSizes.resize(2, defaultSize); //all samples has default size in bytes (same value)
        }
    } else {
        logMe(LOG_INFO, "'stsz' box not found. Trying compact sample size box 'stz2'...");
        Box *stz2 = t->getBoxByType((uint32_t)'stz2');
        if (!stz2)
            throw string("Missing sample to size box ('stz2' not found)");

        uint32_t fldSize = workingFile.readUintP(stz2->contentOffset); //field_size
        logMe(LOG_DBG, "'stz2' field_size = " + to_string(fldSize)); //000X - first 3 bytes: reserved = 0
        uint32_t entries = workingFile.readUint(); //sample_count
        logMe(LOG_INFO, "number of entries (stz2) = " + to_string(entries));

        sampleOfMinSize = 0xFFFFFFFF; //max possible value
        sampleOfMaxSize = 0;
        //read all entries and find min/max values
        uint32_t i = 1;
        uint32_t curSmpl; //small sample storage (first few bits never used)
        while (i < entries) {
            currentSampleSize = workingFile.readUint(); //next 4 bytes
            for (int j = 0; j < (int)(32 / fldSize); j++) {
                i++;
                if (i > entries)
                    break; //to skip padding bytes reading
                //possible sizes 4; 8; 16 bit, in case of 4 - two values at 1 byte (byte[xxxxyyyy] xxxx-first, yyyy-second);
                curSmpl = currentSampleSize >> (32 - fldSize * (j +1)); //entry_size
                if (curSmpl < sampleOfMinSize) {
                    sampleOfMinSize = curSmpl;
                }
                if (curSmpl > sampleOfMaxSize) {
                    sampleOfMaxSize = curSmpl;
                }
            } //for
        } //while
        sampleMinMaxSizes.push_back(sampleOfMinSize); //[0] is min
        sampleMinMaxSizes.push_back(sampleOfMaxSize); //[0] is max
    } //if

    return sampleMinMaxSizes; //[0] is min; [1] is max;
}

std::vector<uint32_t> Track::getChunkOffsets(Box *t, clFile &workingFile) {
    vector<uint32_t> chunkOffsets;
	//chunk offsets
    Box *stco = t->getBoxByType((uint32_t)'stco');

    // 'stco' box not always present ('co64' is possible)
    if (stco) {
        uint32_t nchunks = workingFile.readUintP(stco->contentOffset); //entry_count
        logMe(LOG_INFO, "number of entries (stco) = " + to_string(nchunks));
        for (uint32_t i = 0; i < nchunks; i++) {
            chunkOffsets.push_back(workingFile.readUint()); //chunk_offset
        }
    }

    return chunkOffsets;
}

std::vector<uint64_t> Track::getChunkOffsets64(Box *t, clFile &workingFile) {
    vector<uint64_t> chunkOffsets64;
    Box *co64 = t->getBoxByType((uint32_t)'co64');
    if (!co64)
        throw string("Missing chunk offset box ('co64' not found)");

    uint32_t nchunks = workingFile.readUintP(co64->contentOffset); //entry_count
    logMe(LOG_INFO, "number of entries (co64) = " + to_string(nchunks));
    for (uint32_t i = 0; i < nchunks; i++)
        chunkOffsets64.push_back(workingFile.readUint64()); //chunk_offset

    return chunkOffsets64;
}

std::vector<ChunkMembers> Track::getSamplesNum(Box *t, clFile &workingFile) {
    vector<ChunkMembers> firstChunkSamplesDesc;
    Box *stsc = t->getBoxByType((uint32_t)'stsc');
    if (!stsc)
        throw string("Missing sample to chunk box ('stsc' not found)");

    uint32_t entries = workingFile.readUintP(stsc->contentOffset); //entry_count
    logMe(LOG_INFO, "number of entries (stsc) = " + to_string(entries));
    for (uint32_t i = 0; i < entries; i++) {
        ChunkMembers x;
        x.firstChunk = workingFile.readUint();         //first_chunk
        x.samplesCount = workingFile.readUint();       //samples_per_chunk
        x.descriptionIndex = workingFile.readUint();   //sample_description_index
        firstChunkSamplesDesc.push_back(x);
    }
    return firstChunkSamplesDesc;
}

uint8_t Track::getNalSizeField(Track *track, Box *t, clFile &workingFile) {
    //try to get 'stsd' box from current track
    Box *stsd = t->getBoxByType((uint32_t)'stsd');

    uint8_t nalFld = 0; //only 1, 2 or 4 bytes allowed

    AVCodecID codecID = track->codec.codecParamByFFmpeg->codec_id;
    logM(LOG_DBG, "CodecID= " + to_string(codecID) + "; ");
    switch(codecID) {
    case AV_CODEC_ID_H264:
        //for avcC nal_length_size = (data[4] & 0x03) + 1; see h264_parse.c
        //read fifth byte from avcC and apply 0000 0011 mask, add 1 to get field length for nal size.
        //example, FF & 0x03 = 3 (field size = 4)
        //or use h264_init_context
        if (stsd) {
            int64_t count = stsd->contentSize; //the 'stsd' box size always fits uint32_t, its content - too (even int32_t).
            logM(LOG_DBG, "stsd->contentSize: ");
            logMe(LOG_DBG, to_string(stsd->contentSize));

            uint8_t *buffer;
            buffer = (uint8_t*) malloc(sizeof(uint8_t) * count);
            if (buffer == NULL)
                throw string("Error. Cannot allocate memory buffer to get NAL size field.");

            //1. Read whole content of 'stsd' box to buffer
            //2. Search for avcC instead of parsing whole 'stsd' box
            //'avcC' decimal: 1635148611 or 97 118 99 67; hex: 61 76 63 43

            workingFile.readBlockOfBytesP(stsd->contentOffset, buffer, count);
            int32_t index = workingFile.findLastUInt(buffer, (int32_t) count, (uint32_t)'avcC'); //count casting to int32_t due to findLastUInt

            if (index > 0) {
                //read for fifth byte next to avcC,
                //which is two fields of 6+2 bit, the last one - unsigned int(2) is lengthSizeMinusOne.
                nalFld = buffer[sizeof((uint32_t)'avcC') + index +4] & (uint8_t) 0x03;
                nalFld += (uint8_t) 1; //lengthSizeMinusOne +1 from avcC ISO 14496-15 2010
            }
            free(buffer);
        }
        return nalFld;
        break;
    case AV_CODEC_ID_HEVC:
        nalFld = 2; //nal units in the hvcC always have length coded with 2 bytes; see hevc.c ffmpeg
        return nalFld;
        break;
    default:
        logMe(LOG_DBG, "NAL-size for codec not present.");
    }

    return 0; //assuming there is no NALs for this codec
}
