/*
    Untrunc - file.cpp
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#include "file.h"
#include <string>
#include "portable_endian.h"
#include "portable_stdio.h"
#include <iostream>
#include "loginfo.h"

using namespace std;

clFile::clFile(): pfile(NULL) {
}

clFile::~clFile() {
    if(pfile)
        fclose(pfile);
}

bool clFile::open(string filename) {
    pfile = fopen(filename.c_str(), "rb"); //read binary mode
    if(pfile == NULL) return false;

    fseeko64(pfile, 0LL, SEEK_END);
    size = ftello64(pfile); //int64_t
    fseeko64(pfile, 0LL, SEEK_SET);

    logM(LOG_INFO, "(" + filename + ")");
    logMe(LOG_INFO, " file size= " + to_string(size) + " bytes");

    return true;
}

bool clFile::create(string filename) {
    pfile = fopen(filename.c_str(), "wb"); //write binary mode
    if (pfile == NULL) return false;
    return true;
}

void clFile::close() {
    if (pfile)
        fclose(pfile);
}

void clFile::seek(int64_t p) {
    //TODO: error on non-zero result
    fseeko64(pfile, p, SEEK_SET); //uint64_t cast to int64_t, possible offset 2^63
}

int64_t clFile::pos() {
    //TODO: error on <0 ftello64 result
    return ftello64(pfile); //uint64_t cast to int64_t, possible offset 2^63
}

bool clFile::atEnd() {
    return ftello64(pfile) == size;
}

uint8_t clFile::readUchar() {
    uint8_t value;
    int n = fread(&value, sizeof(uint8_t), 1, pfile);
    if (n != 1)
        throw string("Could not read unsigned char");

    //fields in box objects stored in big-endian format.
    // 2bit field then 6bit field inside single byte stored as:
    //
    // 11000000 for first field
    // 00111111 for second field of the same byte
    //
    // XXxxxxxx >> 6 to get first field
    // xxXXXXXX & 00111111 to get second field
    return value;
}

uint32_t clFile::readUint() {
    uint32_t value;
    int n = fread(&value, sizeof(uint32_t), 1, pfile);
    if (n != 1)
        throw string("Could not read unsigned int");
    return be32toh(value); //fields in box objects stored in big-endian format
}

uint32_t clFile::readUintP(uint64_t offset) {
    uint32_t value;
    fseeko64(pfile, (int64_t) offset, SEEK_SET); //uint64_t cast to int64_t, possible offset 2^63
    int n = fread(&value, sizeof(uint32_t), 1, pfile);
    if (n != 1)
        throw string("Could not read unsigned int from file offset possition: ") + to_string(ftello64(pfile));
    return be32toh(value); //fields in box objects stored in big-endian format
}

uint64_t clFile::readUint64() {
    uint64_t value;
    int n = fread(&value, sizeof(value), 1, pfile);
    if (n != 1)
        throw string("Could not read unsigned int64");
    return be64toh(value); //fields in box objects stored in big-endian format
}

uint64_t clFile::readUint64P(uint64_t offset) {
    uint64_t value;
    fseeko64(pfile, (int64_t) offset, SEEK_SET); //uint64_t cast to int64_t, possible offset 2^63
    int n = fread(&value, sizeof(value), 1, pfile);
    if (n != 1)
        throw string("Could not read unsigned int64 from file offset possition:") + to_string(ftello64(pfile));
    return be64toh(value); //fields in box objects stored in big-endian format
}

size_t clFile::readBlockOfBytesP(uint64_t offset, uint8_t *buffer, size_t count) {
    fseeko64(pfile, (int64_t) offset, SEEK_SET); //uint64_t cast to int64_t, possible offset 2^63
    return fread(buffer, sizeof(uint8_t), count, pfile);
}

int32_t clFile::findLastUInt(uint8_t *buffer, int32_t bufLength, uint32_t info) {
    //The fastes of way is by plain. Easter egg.
    int32_t index;
    uint8_t *pEndOfTheBuffer;
    uint8_t *pStartOfTheBuffer;

    pStartOfTheBuffer = buffer -1; //-1 to get 0 index
    pEndOfTheBuffer = buffer + (bufLength - sizeof(info));

    info = be32toh(info); //swap due to casting uit8_t to uint32t_t and little endian

    while (pEndOfTheBuffer - pStartOfTheBuffer) {
        //compare from the end of the buffer to get last match
        if ((*(uint32_t*) pEndOfTheBuffer) == info) //casting to get uint32_t instead of just uint8_t
            break; //here last match is found, leave the loop
        pEndOfTheBuffer--; //shorten the end
    }

    return index = ((pEndOfTheBuffer == pStartOfTheBuffer) ? (-1) : (pEndOfTheBuffer -(pStartOfTheBuffer +1))); //-1 if not found
}

int clFile::writeUchar(uint8_t n) {
    fwrite(&n, sizeof(n), 1, pfile);
    return 1;
}

int clFile::writeUint(uint32_t n) {
    n = htobe32(n);
    fwrite(&n, sizeof(n), 1, pfile);
    return 4;
}

int clFile::writeUint64(uint64_t n) {
    n = htobe64(n);
    fwrite(&n, sizeof(n), 1, pfile);
    return 8;
}

size_t clFile::writeBlockOfBytes(uint8_t *buffer, size_t count) {
    //fseeko64(pfile, (int64_t) offset, SEEK_SET); //uint64_t cast to int64_t, possible offset 2^63
    return fwrite(buffer, sizeof(uint8_t), count, pfile);
}
