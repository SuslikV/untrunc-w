//GPLv2 or later

//edited 2017

#include "AP_AtomDefinitions.h"
#include "atom.h"
#include "file.h"

#include <string>
#include <map>
#include <iostream>

#include <assert.h>
#include "portable_endian.h"
#include "loginfo.h"

using namespace std;

Box::Box() {
        //startOffset = 0ULL;
}

Box::~Box() {
    for(unsigned int i = 0; i < children.size(); i++)
        delete children[i];
}

/**************************
//ISO IEC 14496-12 2015
//header = {box size + box type}
//box compact size: 32 bit
//box compact type: 32 bit
//box extended size: 64 bit
//box extended type: full UUIDs
//typically only 'mdat'(es) may use 64 bit
//most boxes are compact
//boxes with an unrecognized type shall be ignored and skipped

aligned(8) class Box(unsigned int(32) boxtype, optional unsigned int(8)[16] extended_type) {
    unsigned int(32) size;
    unsigned int(32) type = boxtype;
    if (size == 1){
        unsigned int(64) largesize;
    } else if (size == 0) {
        //box extends to end of file
    }
    if (boxtype == 'uuid') {
        unsigned int(8)[16] usertype = extended_type;
    }
}

//Box, header structure (X is byte):
//
//           if size=1            if type == 'uuid'
// XXXX XXXX [XXXX XXXX] [X X X X X X X X X X X X X X X X] XXXXX...to the end of the box
// size type  largesize                 uuid
//
// uuid is user defined.
// Standard uuid = X X X X - 00 11 - 00 10 - 80 00 - 00 AA 00 38 9B 71 (h)
//                  type

// many objects also contain a version number and flags field

aligned(8) class FullBox(unsigned int(32) boxtype, unsigned int(8) v, bit(24) f)
    extends Box(boxtype) {
    unsigned int(8) version = v;
    bit(24) flags = f;
}

//FullBox:
//
//            if size=1          if type == 'uuid'
// XXXX XXXX [XXXX XXXX] [X X X X X X X X X X X X X X X X] X XXX   XXXXX...to the end of the box
// size type  largesize                 uuid               v flags

//example, file type box 'ftyp' (first box)

aligned(8) class FileTypeBox
    extends Box('ftyp') {
    unsigned int(32) major_brand;
    unsigned int(32) minor_version;
    unsigned int(32) compatible_brands[]; //to end of the box
}
*****************************/

void Box::parseBoxHeader(clFile &anyfile){
    startOffset = anyfile.pos();   //remember box start position inside the file
    size = anyfile.readUint();     //get box size
    type = anyfile.readUint();     //get box type

    actualSize = (uint64_t)size;    //let's asume small size of the box

    //check for large size box
    if (size == 1) {
        largesize = anyfile.readUint64(); //get box large size (lies right after 'type')
        actualSize = largesize;
    } else if (size == 0) {
        //box extends to the end of file
        wholeSpace = true;
        actualSize = anyfile.length() - startOffset; //startOffset max = 2^63 due to file.length()
    }

    if (type == (uint32_t)'uuid') {
        //read uuid
        for(int i = 0; i < 16; i++)
            usertype.push_back(anyfile.readUchar());
        hasUUID = true;
    }

    //usually users uuid type hasn't FullBox propeties
    if (isVersioned(type)) {
        vpFlags = anyfile.readUint();  //get version + flags as unsigned integer
        fullBox = true;
    }
    contentOffset = anyfile.pos(); //actual data of the box starts from here
    contentSize = (int64_t) actualSize - (contentOffset - startOffset);
}

void Box::parseMP4ForBoxes(clFile &anyfile) {
    parseBoxHeader(anyfile);
    if (isParent(type) && (type != (uint32_t)'udta')) { //skip parsing all user data boxes

        //test
        logMe(LOG_DBG, "not 'udta' box but PARENT, type= " + getTypeInChars(type));

            while (anyfile.pos() < startOffset + (int64_t) actualSize) {//uint64_t cast to int64_t, possible offset 2^63
                Box *box = new Box;
                box->parseMP4ForBoxes(anyfile); //recursion
                children.push_back(box); // fill children array with child (building children tree)
            }
        assert(anyfile.pos() == startOffset + (int64_t) actualSize); //abort if anyfile.pos != start + length

    } else {
        //seek to the next box
        anyfile.seek(startOffset + (int64_t) actualSize); //uint64_t cast to int64_t, possible offset 2^63
    }
}

AtomDefinition definition(uint32_t id) {
    map<uint32_t, AtomDefinition> def;
    if (def.size() == 0) {
        for(int i = 0; i < atomsCount -1; i++)  //atomsCount=175 - number of known boxes -1
            def[knownAtoms[i].known_atom_name] = knownAtoms[i];
    }
    if (!def.count(id)) {
       //return a fake definition
        return def['<()>'];
    }
    return def[id];
}

bool Box::isParent(uint32_t id) {
    AtomDefinition def = definition(id);
    return def.container_state == PARENT_ATOM;
}

bool Box::isDual(uint32_t id) {
    AtomDefinition def = definition(id);
    return def.container_state == DUAL_STATE_ATOM;
}

bool Box::isVersioned(uint32_t id) {
    AtomDefinition def = definition(id);
    return def.box_type == VERSIONED_ATOM;
}

vector<Box *> Box::getBoxesCollByType(uint32_t type) {
    vector<Box *> boxes;
    for(unsigned int i = 0; i < children.size(); i++) {
        if (children[i]->type == type) {
            boxes.push_back(children[i]); //add single Box to array
        }
        vector<Box *> a = children[i]->getBoxesCollByType(type); //recursive call to add to array "a" all child Box objects with the 'type'
        boxes.insert(boxes.end(), a.begin(), a.end()); // join array "boxes" and "a" array
    }
    return boxes; //returns array of pointers of Boxes of 'type'
}

Box *Box::getBoxByType(uint32_t type) {
    for (unsigned int i = 0; i < children.size(); i++) {
        if (children[i]->type == type)
            return children[i];
        Box *a = children[i]->getBoxByType(type); //recursive call to look through all child Box objects until first match found
        if (a) return a; //return pointer of Box of 'type'
    }
    return NULL; //box of 'type' not found
}


BufferedBox::BufferedBox() {
    logMe(LOG_INFO, "Buffered stuff starts from here.");
}

BufferedBox::~BufferedBox() {
}

void Box::writeSkipBox(clFile &repairedfile, uint64_t boxSize) {
    //min box length is 8 bytes
    if (boxSize > 7) {
        if (boxSize >> 32 == 0) {
            repairedfile.writeUint((uint32_t) boxSize); //size
            repairedfile.writeUint((uint32_t)'free'); //boxtype
            repairedfile.seek(repairedfile.pos() + (int64_t) boxSize -4 -4 -1); //casting to int64_t possible offset is 2^63;
            repairedfile.writeUchar((uint8_t)'e'); //last byte 65h; the content of the box is not important
        } else {
            repairedfile.writeUint(1); //size
            repairedfile.writeUint((uint32_t)'free'); //boxtype
            repairedfile.writeUint64(boxSize); //largesize
            repairedfile.seek(repairedfile.pos() + (int64_t) boxSize -4 -4 -8 -1); //casting to int64_t possible offset is 2^63
            repairedfile.writeUchar((uint8_t)'e'); //last byte 65h; the content of the box is not important
        }
    }
}

int64_t Box::writeBoxHeader(Box *currentBox, clFile &destinationFile) {
    int64_t startPoint = destinationFile.pos(); //remember position of the box to adjust its header later
    writeSkipBox(destinationFile, 8); //always reserve space for length64 bytes of the next box; not perfect, but it works.

    //writing box header
    //
    destinationFile.writeUint(0); //write box size, extends to the end of the file
    destinationFile.writeUint(currentBox->type); //write boxtype
    if (currentBox->hasUUID) {
        for (uint32_t i = 0; i < currentBox->usertype.size(); i++)
            destinationFile.writeUchar(currentBox->usertype[i]); //write usertype property
    }
    if (currentBox->fullBox) {
        destinationFile.writeUint(currentBox->vpFlags); //write vpFlags property
    }
    return startPoint;
}

void Box::writeBoxHeaderSize(Box *currentBox, int64_t startPoint, clFile &destinationFile) {
    int64_t endPoint = destinationFile.pos(); //get position after last successful write operation
    uint64_t largesize64 = (uint64_t) (endPoint - (startPoint +8)); // 8 is 'free' box size reserved earlier by writeBoxHeader()

    //writing actuall size of the written data to the box header
    //
    if ((largesize64 >> 32) == 0) {
        //write 32 bit size
        destinationFile.seek(startPoint +8);                //skip the 'free' box
        destinationFile.writeUint((uint32_t) largesize64);  //update box size
    } else {
        //write 64 bit size
        destinationFile.seek(startPoint);           //to the start of the 'free' box
        destinationFile.writeUint(1);               //overwrite; large size is used instesad
        destinationFile.writeUint(currentBox->type);//overwrite; boxtype
        destinationFile.writeUint64(largesize64 +8);   //overwrite; write large size; +8 is largesize64 field itself instead of 'free' box
    }
    destinationFile.seek(endPoint); //anyway, move file pointer to the end of the box
}

void Box::copyBoxRaw(Box *workingBox, clFile &sourceFile, clFile &destinationFile) {
    //read from sourcefile data block to buffer
    //write buffer to destination file

    int32_t buffMaxlength = 200000;
    int32_t count = buffMaxlength; //number of bytes to read
    size_t readRet =1; //1 just to enter first loop
    size_t writeRet; //returns result of the writing data block operation

    uint8_t *buff;
    buff = (uint8_t*) malloc(sizeof(uint8_t) * buffMaxlength);
    if (buff == NULL)
        throw string("Error. Cannot allocate memory buffer (copy box).");

    int64_t offset = workingBox->startOffset;
    int64_t endOffset = offset + (int64_t) workingBox->actualSize;

    logMe(LOG_DBG, "copyBox start offset=" + to_string(offset));
    logMe(LOG_DBG, "copyBox endOffset=" + to_string(endOffset));

    //read until end of the box or reading error occured
    while ((offset < endOffset) && (readRet > 0)) {
        if ((endOffset - offset) < (int64_t) buffMaxlength)
            count = (int32_t)(endOffset - offset); //read no more than allowed by box size when reading last bytes of the box

        readRet = sourceFile.readBlockOfBytesP(offset, buff, count);

        logMe(LOG_DBG, "copyBox readRet=" + to_string(readRet));

        if (readRet > 0) {
            writeRet = destinationFile.writeBlockOfBytes(buff, readRet);
            if (!(writeRet == readRet))
                throw string("Error. Copy box write error.");
        }

        offset = sourceFile.pos();
        logMe(LOG_DBG, "copyBox offset= " + to_string(offset));
    }
    free(buff);
}

void Box::copyBoxFixSize(Box *workingBox, clFile &sourceFile, clFile &destinationFile) {
    //read from sourcefile data block to buffer
    //write buffer to destination file

    int32_t buffMaxlength = 200000;
    int32_t count = buffMaxlength; //number of bytes to read
    size_t readRet =1; //1 just to enter first loop
    size_t writeRet; //returns result of the writing data block operation

    //writing box header
    //
    int64_t startPoint = writeBoxHeader(workingBox, destinationFile); //remember position of the box to adjust its header later

    //writing box content
    //
    uint8_t *buff;
    buff = (uint8_t*) malloc(sizeof(uint8_t) * buffMaxlength);
    if (buff == NULL)
        throw string("Error. Cannot allocate memory buffer (copy box fix size).");

    int64_t offset = workingBox->contentOffset;
    int64_t endOffset = offset + (int64_t) workingBox->contentSize;

    logMe(LOG_DBG, "copyBoxFixSize start offset=" + to_string(offset));
    logMe(LOG_DBG, "copyBoxFixSize endOffset=" + to_string(endOffset));

    while ((offset < endOffset) && (readRet > 0)) { //read until end of the box or reading error occured
        if ((endOffset - offset) < (int64_t) buffMaxlength)
            count = (int32_t)(endOffset - offset); //read no more than allowed by box size

        readRet = sourceFile.readBlockOfBytesP(offset, buff, count);

        //logMe(LOG_DBG, "copyBoxFixSize readRet=" + to_string(readRet));

        if (readRet > 0) {
            writeRet = destinationFile.writeBlockOfBytes(buff, readRet);
            if (!(writeRet == readRet))
                throw string("Error. Copy box (fix size) write error.");
        }

        offset = sourceFile.pos();
        //logMe(LOG_DBG, "copyBoxFixSize offset= " + to_string(offset));
    }
    free(buff);

    //writing actuall size of written data to the box header
    //
    writeBoxHeaderSize(workingBox, startPoint, destinationFile);
}

void Box::copyBoxes(clFile &workingBoxFile, Box *workingBox, clFile &sourceFile, clFile &destinationFile) {
    for (unsigned int i = 0; i < workingBox->children.size(); i++) {
        //if box has child boxes...
        if (workingBox->children[i]->children.size() > 0) {
            logM(LOG_DBG, " r-ParentBox-" + getTypeInChars(workingBox->children[i]->type));

            //writing box header
            //
            int64_t startPoint = writeBoxHeader(workingBox->children[i], destinationFile); //remember position of the box to adjust its header later

            //process all child boxes
            //
            workingBox->children[i]->copyBoxes(workingBoxFile, workingBox->children[i], sourceFile, destinationFile); //recursive call
            logMe(LOG_DBG, "-end_r-" + getTypeInChars(workingBox->children[i]->type));

            //update box header for size field
            //box ends ("closes") right here.
            writeBoxHeaderSize(workingBox->children[i], startPoint, destinationFile);

        } else {
            //write whole box here (it has no children)
            //if box type = stts, stss, ctts, stsc, stsz (stz2), stco (co64) then call for new box build and write it
            logM(LOG_DBG, "  i= " + to_string(i));
            logMe(LOG_DBG, " box without children, type: " + getTypeInChars(workingBox->children[i]->type));
            //int64_t startPoint;
            switch(workingBox->children[i]->type) {
            case (uint32_t)'stts':
                //stts box copy
                logMe(LOG_DBG, "sttsContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'stss':
                //stss box copy
                logMe(LOG_DBG, "stssContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                //writeBoxHeaderSize(workingBox->children[i], startPoint, destinationFile); //writing actuall size of written data to the box header
                break;
            case (uint32_t)'ctts':
                //ctts box copy
                logMe(LOG_DBG, "cttsContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'stsc':
                //stsc box copy
                logMe(LOG_DBG, "stscContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'stsz':
                //stsz box copy
                logMe(LOG_DBG,"stszContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'stz2':
                //---- TODO: stsz box copy; now using 4 bytes field instead of compact stz2 ----//
                break;
            case (uint32_t)'stco':
                //stco box copy
                logMe(LOG_DBG,"stcoContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'co64':
                //co64 box copy
                logMe(LOG_DBG,"co64Content");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'mvhd':
                //mvhd box copy
                logMe(LOG_DBG,"mvhdContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'tkhd':
                //tkhd box copy
                logMe(LOG_DBG,"tkhdContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'mdhd':
                //mdhd box copy
                logMe(LOG_DBG,"mdhdContent");
                copyBoxRaw(workingBox->children[i], sourceFile, destinationFile); //changed sourceFile = new file with corrected boxes
                break;
            case (uint32_t)'free':
                //skip this box, while still copy 'skip' box
                break;

            default:
                //copy box from working file without changes
                copyBoxRaw(workingBox->children[i], workingBoxFile, destinationFile);
            } //switch
        } //if
    } //for
}

string Box::getTypeInChars(uint32_t typeUInt) {
    //all this stuff just to swap bytes and print 'type' on screen by cout
    //it's not used anywhere else
    typeUInt = htobe32(typeUInt);
    char symbols[5];
    for (int i = 0; i < 4; i++)
        symbols[i] = (uint8_t) ((typeUInt >> (i *8)) & (uint32_t)0x000000FF);
    symbols[4] = (uint8_t) 0; //null terminated string
    //other method:
    //char symbols[5];
    //for (int i = 4; i > 0; i--)
    //    symbols[4-i] = (uint8_t) ((typeUInt>>(i-1)*8) & (uint32_t)0x000000FF);
    //symbols[4] = (uint8_t) 0; //null terminated string
    return symbols;
}
