//GPLv2 or later

//edited 2017

#ifndef ATOM_H
#define ATOM_H
//extern "C" {
#include <stdint.h>
//}
#include <vector>
#include <string>

#include "file.h"

//Box class desingned to store dead masks from the MP4 file.
//It contains general header info of the each box according to ISO IEC 14496-12 2015
//and has pointers (offsets) to actual data from the file.
class Box {
public:
    int64_t startOffset;    //box begins from this offset in file
    uint32_t size;          //box's size
    uint32_t type;          //box's type
    bool fullBox = false;   //FullBox type that extends Box for version + flags bytes, VERSIONED_ATOM by AtomicParsley application
    uint32_t vpFlags;       //version + flags = 4 byte
    uint64_t largesize;     //only used if original size = 1
    bool wholeSpace;        //only used if original size = 0
    uint64_t actualSize;    //no matter size or largesize or to the end of file, exact size is here
    std::vector<uint8_t> usertype;  //used when type = 'uuid' (0x75756964)
                                    //max length of usertype = 16 bytes (symbols)
    bool hasUUID = false;   //has usertype

    int64_t contentOffset;  //box content offset right after the header (header includes Box&FullBox fields)
    int64_t contentSize;    //size of the box content (without header, which includes Box&FullBox fields)

    std::vector<Box *> children;    //storage for pointers to all child boxes from the current box

    Box();
    ~Box();

    void parseBoxHeader(clFile &anyfile); //remember startOffset, size, type, vpFlags (if any), contentOffset
    void parseMP4ForBoxes(clFile &anyfile); //build boxes tree

    std::vector<Box *> getBoxesCollByType(uint32_t type); //returns ponters to all boxes with the 'type'
    Box *getBoxByType(uint32_t type); //returns pointer to single Box of 'type'

    static bool isParent(uint32_t id); //check if the box can hold other boxes
    static bool isDual(uint32_t id); //check if the box can hold other boxes and has a parent box
    static bool isVersioned(uint32_t id); //check is the box is FullBox (includes 'version + flags' bytes)

    //writes empty box of type 'free'
    void writeSkipBox(clFile &repairedfile, uint64_t boxSize); //writes 'free' type box of boxSize from current position

    int64_t writeBoxHeader(Box *currentBox, clFile &destinationFile); //write box header size =0 (extends to the end of the file) and return its start offset
    void writeBoxHeaderSize(Box *currentBox, int64_t startPoint, clFile &destinationFile); //write box actual header (turn size =0 to number)
    void copyBoxRaw(Box *workingBox, clFile &sourceFile, clFile &destinationFile); //raw copy of the box
    void copyBoxFixSize(Box *workingBox, clFile &sourceFile, clFile &destinationFile); //copy of the box + adjust box size, if needed - length64 added.
    void copyBoxes(clFile &workingBoxFile, Box *workingBox, clFile &sourceFile, clFile &destinationFile); //copy all boxes in recursive call

    std::string getTypeInChars(uint32_t typeUInt); //to print uint32_t on the screen symbol by symbol
};

class BufferedBox: public Box {
public:
    //TODO: maybe add some options here or remove this class

    BufferedBox();
    ~BufferedBox();

};

#endif // ATOM_H
