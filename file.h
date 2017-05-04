/*
    Untrunc - file.h
    GPLv2 or later
    Greetings to work of: 2010, Federico Ponchio
*/

//edited 2017

#ifndef FILE_H
#define FILE_H

//extern "C" {
#include <stdint.h>
//}
#include <stdio.h>
#include <string>

class clFile {
public:
    clFile();
    ~clFile();
    bool open(std::string filename);
    bool create(std::string filename);
    void close();

    void seek(int64_t p);  //possible offset 2^63
    int64_t pos();         //possible offset 2^63
    bool atEnd();          //flag - the end reached!
    int64_t length() { return size; }

    uint8_t readUchar();                    //1 byte read
    uint32_t readUint();                    //4 bytes read
    uint32_t readUintP(uint64_t offset);    //4 bytes read from offset; uint64_t cast to int64_t, possible offset 2^63
    uint64_t readUint64();                  //8 bytes read
    uint64_t readUint64P(uint64_t offset);  //8 bytes read from offset; uint64_t cast to int64_t, possible offset 2^63

    size_t readBlockOfBytesP(uint64_t offset, uint8_t *buffer, size_t count); //read 'count' number of bytes from file to the buffer
    size_t writeBlockOfBytes(uint8_t *buffer, size_t count); //write 'count' number of bytes from buffer to file


    int32_t findLastUInt(uint8_t *buffer, int32_t bufLength, uint32_t info); //returns last index to 'info' inside block of data or -1 if not found

    int writeUchar(uint8_t n);      //write 1 byte
    int writeUint(uint32_t n);      //write 4 bytes
    int writeUint64(uint64_t n);    //write 8 bytes

protected:
    int64_t size; //opened file size, by ftello64, so int64_t
    FILE *pfile;  //pointer to FILE object, used for file seeking and reading operations
};

#endif // FILE_H
