untrunc-w, 2017
---------------

## About

utrunc-w designed to restore playability of the aborted .mp4 recordings.

## Features

* Windows platform
* multitrack support (tested with OBS Studio v18.0.1)
* command line utility

## Usage

untrunc-w.exe [options] <workingfile.mp4> [<brokenfile.mp4>]

where:

- 'workingfile.mp4' is working file sample from the same encoding setup (same device),
- 'brokenfile.mp4' is file from aborted recording session (any player fails to playback it).

The repaired file saved under the same directory as 'brokenfile.mp4' and named 'brokenfile.mp4_untrunc.mp4'.
The program may create temporary files (.tmp) under the same directory.


Options:

--uverbose: Display all untrunc-w messages.

--ffverbose: Display AV_LOG_INFO level of FFmpeg messages.

--keeptmp: Do not delete .tmp files after task complete.

--gopm [n]: Interleaving Mask size in number [n] of GOPs. Default setting is 1.

--gopl [n]: Interleaving Mask loop start in number [n] of GOPs. Default setting is 1.

--smpltrk [0..100]: Use number of samples per chunk instead of using Interleaving Mask values. Default is 0 (multi-track mode).

--help: Display short help info.

Good result was achieved for --gopm 12 --gopl 10, with working file sample that had 25 keyframes entries

## Compile

Requirements:

Windows 7 (minimum);
qt v5.8.0 (x86);
Visual Studio 2013 UPD5;
FFmpeg libs and headers files (ffmpeg-20170425-b4330a0-win32).

Project developed under qt (v5.8.0), compiled by Visual Studio 2013 default compilers.
Default folder for dependencies libs:

    ./untrunc_deps/win32/bin/
    
Default folder for dependencies headers:

    ./untrunc_deps/win32/include/
    
Default folder for project files:

    ./untrunc-w/

## Legal

New parts of the code covered by MIT, joined by MPL2, but whole program still under the GPL2 license or so.

## Conclusion

A lot of work is done.

Project was started for educational purposes. It was completely rewitten from 'untrunc' project 
just to support multi-track, large files, to run by Windows and consume less RAM.
It takes about 1070 sec to recover 484 sec of aborted recording (on system powered by AMD E2-1800 APU).
Some samples were skipped and there is place to improve algorithm of the recovery. But at least, 
it shows to you how much data you have.
My own estimation on full file recovery is about 25% of success.

If you can - do it, do it better!

## Links

- https://github.com/ponchio/untrunc
- https://obsproject.com

With best wishes, Suslik V.
