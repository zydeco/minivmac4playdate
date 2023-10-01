/*
    OSGLUPDX.c

    Copyright (C) 2023 Jesús A. Álvarez

    You can redistribute this file and/or modify it under the terms
    of version 2 of the GNU General Public License as published by
    the Free Software Foundation.  You should have received a copy
    of the license along with this file; see the file COPYING.

    This file is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    license for more details.
    */

/*
    Operating System GLUe for PlayDate
 */

#include <stdio.h>
#include <stdlib.h>
#include "pd_api.h"
#include "OSGCOMUI.h"
#include "OSGCOMUD.h"

PlaydateAPI *pd = NULL;

#pragma mark - Initialization

// emulating in update callbacks is slow
#define TargetFPS 5
#define TargetSpeed 0.5
#define TicksPerFrame ((60 / TargetFPS) * TargetSpeed)

#define kRAM_Size (kRAMa_Size + kRAMb_Size)
EXPORTVAR(ui3p, RAM)
EXPORTVAR(ui3p, VidROM)
EXPORTVAR(ui3p, VidMem)

GLOBALPROC MyMoveBytes(anyp srcPtr, anyp destPtr, si5b byteCount) {
    (void)memcpy((char *)destPtr, (char *)srcPtr, byteCount);
}

LOCALFUNC blnr LoadMacRom(void) {
    SDFile *romFile = pd->file->open(RomFileName, kFileRead|kFileReadData);
    if (romFile == NULL) {
        pd->system->error("Opening %s: %s", RomFileName, pd->file->geterr());
        return falseblnr;
    }
    if (pd->file->read(romFile, ROM, kROM_Size) != kROM_Size) {
        return falseblnr;
    }
    pd->file->close(romFile);
    return trueblnr;
}

#pragma mark - Events & Run Loop

FORWARDPROC ZapOSGLUVars(void);
FORWARDFUNC blnr InitOSGLU(void);
IMPORTFUNC blnr InitEmulation(void);
FORWARDFUNC int DoUpdate(void* userdata);
FORWARDPROC UnInitOSGLU(void);

LOCALVAR blnr showFPS = falseblnr;
LOCALVAR PDMenuItem *fpsMenuItem, *inputMenuItem;
FORWARDPROC SetDpadMode(blnr mouse);
FORWARDPROC InsertDiskMenuCallback(void *userdata);

void FPSMenuCallback(void *userdata) {
    showFPS = pd->system->getMenuItemValue(fpsMenuItem);
    pd->graphics->markUpdatedRows(4, 15);
}

void InputMenuCallback(void *userdata) {
    SetDpadMode(pd->system->getMenuItemValue(inputMenuItem) == 0);
}

LOCALPROC InitMenus(void) {
    pd->system->addMenuItem("insert disk", InsertDiskMenuCallback, NULL);
    fpsMenuItem = pd->system->addCheckmarkMenuItem("show FPS", 0, FPSMenuCallback, NULL);
    const char *inputOptions[] = {"mouse", "wasd"};
    inputMenuItem = pd->system->addOptionsMenuItem("d-pad", inputOptions, 2, InputMenuCallback, NULL);
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* api, PDSystemEvent event, uint32_t arg) {
    pd = api;
    switch (event) {
        case kEventInit:
            ZapOSGLUVars();
            if (InitOSGLU() && InitEmulation()) {
                pd->system->logToConsole("Welcome to Macintosh");
                pd->display->setRefreshRate(TargetFPS);
                pd->display->setInverted(1);
                pd->system->setUpdateCallback(DoUpdate, pd);
                InitMenus();
            } else {
                pd->system->error("Initialization error!");
            }
            break;
        case kEventTerminate:
            pd->system->logToConsole("Bye!");
            UnInitOSGLU();
            break;
        default:
            break;
    }
    return 0;
}

#pragma mark - Debug Logging

#if dbglog_HAVE

LOCALFUNC blnr dbglog_open0(void) {
    return trueblnr;
}

LOCALPROC dbglog_write0(char *s, uimr L) {
    if (s[L] == 0) {
        pd->system->logToConsole("%s", s);
    } else {
        char buf[L+1];
        memcpy(buf, s, L);
        buf[L] = 0;
        pd->system->logToConsole("%s", buf);
    }
}

LOCALPROC dbglog_close0(void) {}

#endif

#include "COMOSGLU.h"
#include "PBUFSTDC.h"
#include "INTLCHAR.h"

FORWARDPROC DrawInsertDiskMenuBody(void);
FORWARDFUNC const char* InsertDiskMenuTitle(void);
#include "CONTROLM.h"

GLOBALPROC DoneWithDrawingForTick(void) {
    // draw in update callback
}

#define MouseSpeed 2
#define CrankThreshold 5.f

LOCALVAR blnr dpadIsMouse = trueblnr;
LOCALVAR int MouseAccel;
FORWARDPROC HandleDiskMenuInput(PDButtons pushed, float crankChange);

LOCALPROC SetDpadMode(blnr mouse) {
    dpadIsMouse = mouse;
    if (mouse) {
        Keyboard_UpdateKeyMap(MKC_W, falseblnr);
        Keyboard_UpdateKeyMap(MKC_A, falseblnr);
        Keyboard_UpdateKeyMap(MKC_S, falseblnr);
        Keyboard_UpdateKeyMap(MKC_D, falseblnr);
    }
}

LOCALPROC UpdateInput(void) {
    PDButtons current, pushed, released;
    pd->system->getButtonState(&current, &pushed, &released);

    if (SpecialModeTst(SpclModeInsertDisk)) {
        HandleDiskMenuInput(pushed, pd->system->getCrankChange());
        return;
    }

    if (dpadIsMouse) {
        // Move Mouse with d-pad
        ui4r dh = current & kButtonLeft ? -1 : current & kButtonRight ? 1 : 0;
        ui4r dv = current & kButtonUp ? -1 : current & kButtonDown ? 1 : 0;
        if (dh == 0 && dv == 0) {
            MouseAccel = 1;
        } else {
            MouseAccel = MIN(MouseAccel + 1, 8);
        }
        MyMousePositionSetDelta(dh * MouseSpeed * MouseAccel, dv * MouseSpeed * MouseAccel);
    } else {
        // WASD
        Keyboard_UpdateKeyMap(MKC_W, current & kButtonUp);
        Keyboard_UpdateKeyMap(MKC_A, current & kButtonLeft);
        Keyboard_UpdateKeyMap(MKC_S, current & kButtonDown);
        Keyboard_UpdateKeyMap(MKC_D, current & kButtonRight);
    }

    // Button A: mouse button
    MyMouseButtonSet(current & kButtonA);

    // Button B: space bar
    Keyboard_UpdateKeyMap(MKC_Space, current & kButtonB);

    // Crank: arrow up/down
    float crank = pd->system->getCrankChange();
    if (crank < -CrankThreshold) {
        // counter-clockwise: arrow up
        Keyboard_UpdateKeyMap(0x48, falseblnr);
        Keyboard_UpdateKeyMap(0x4D, trueblnr);
    } else if (crank > CrankThreshold) {
        // clockwise: arrow down
        Keyboard_UpdateKeyMap(0x4D, falseblnr);
        Keyboard_UpdateKeyMap(0x48, trueblnr);
    } else {
        // no change, keys up
        Keyboard_UpdateKeyMap(0x4D, falseblnr);
        Keyboard_UpdateKeyMap(0x48, falseblnr);
    }
}

#pragma mark - Screen

LOCALFUNC blnr Screen_Init(void) {
    return trueblnr;
}

LOCALPROC Screen_UnInit(void) {

}

#pragma mark - Time and Space

LOCALVAR ui5b TrueEmulatedTime = 0;
LOCALVAR float NextTickChangeTime;
#define MyTickDuration (1.0f / 60.14742f)

LOCALVAR ui5b NewMacDateInSeconds;

LOCALPROC UpdateTrueEmulatedTime(void) {
    float TimeDiff = pd->system->getElapsedTime() - NextTickChangeTime;

    if (TimeDiff >= 0.0f) {
        if (TimeDiff > 16 * MyTickDuration) {
            // emulation interrupted, forget it
            ++TrueEmulatedTime;
            NextTickChangeTime = MyTickDuration;
        } else {
            do {
                ++TrueEmulatedTime;
                TimeDiff -= MyTickDuration;
                NextTickChangeTime += MyTickDuration;
            } while (TimeDiff >= 0.0f);
            NextTickChangeTime -= pd->system->getElapsedTime();
        }
        pd->system->resetElapsedTime();
    }
}

GLOBALFUNC blnr ExtraTimeNotOver(void) {
    UpdateTrueEmulatedTime();
    return falseblnr;
}

LOCALPROC StartUpTimeAdjust(void) {
    pd->system->resetElapsedTime();
    NextTickChangeTime = MyTickDuration;
}

LOCALVAR ui5b MyDateDelta;

LOCALFUNC blnr CheckDateTime(void) {
    NewMacDateInSeconds = ((ui5b)pd->system->getSecondsSinceEpoch(NULL)) + MyDateDelta;
    if (CurMacDateInSeconds != NewMacDateInSeconds) {
        CurMacDateInSeconds = NewMacDateInSeconds;
        return trueblnr;
    } else {
        return falseblnr;
    }
}

#if MySoundEnabled
FORWARDPROC MySound_SecondNotify(void);
#endif

GLOBALPROC WaitForNextTick(void) {
    if (CheckDateTime()) {
#if MySoundEnabled
        MySound_SecondNotify();
#endif
#if EnableDemoMsg
        DemoModeSecondNotify();
#endif
    }
    OnTrueTime = TrueEmulatedTime;
}

LOCALFUNC blnr InitLocationDat(void) {
    ui5b TzOffSet = pd->system->getTimezoneOffset();
    MyDateDelta = 3029529600 - TzOffSet;
    CurMacDateInSeconds = 0;
    CheckDateTime();
    return trueblnr;
}

#pragma mark - Sound

#if MySoundEnabled


#define kLn2SoundBuffers 4 /* kSoundBuffers must be a power of two */
#define kSoundBuffers (1 << kLn2SoundBuffers)
#define kSoundBuffMask (kSoundBuffers - 1)

#define DesiredMinFilledSoundBuffs 4
/*
 if too big then sound lags behind emulation.
 if too small then sound will have pauses.
 */

#define kLnOneBuffLen 9
#define kLnAllBuffLen (kLn2SoundBuffers + kLnOneBuffLen)
#define kOneBuffLen (1UL << kLnOneBuffLen)
#define kAllBuffLen (1UL << kLnAllBuffLen)
#define kLnOneBuffSz (kLnOneBuffLen + kLn2SoundSampSz - 3)
#define kLnAllBuffSz (kLnAllBuffLen + kLn2SoundSampSz - 3)
#define kOneBuffSz (1UL << kLnOneBuffSz)
#define kAllBuffSz (1UL << kLnAllBuffSz)
#define kOneBuffMask (kOneBuffLen - 1)
#define kAllBuffMask (kAllBuffLen - 1)
#define dbhBufferSize (kAllBuffSz + kOneBuffSz)

#define dbglog_SoundStuff (1 && dbglog_HAVE)
#define dbglog_SoundBuffStats (0 && dbglog_HAVE)

LOCALVAR SoundSource *MySoundSource;
LOCALVAR tpSoundSamp TheSoundBuffer = nullpr;
volatile static ui4b ThePlayOffset;
volatile static ui4b TheFillOffset;
volatile static ui4b MinFilledSoundBuffs;
#if dbglog_SoundBuffStats
LOCALVAR ui4b MaxFilledSoundBuffs;
#endif
LOCALVAR ui4b TheWriteOffset;

GLOBALFUNC tpSoundSamp MySound_BeginWrite(ui4r n, ui4r *actL) {
    ui4b ToFillLen = kAllBuffLen - (TheWriteOffset - ThePlayOffset);
    ui4b WriteBuffContig =
    kOneBuffLen - (TheWriteOffset & kOneBuffMask);

    if (WriteBuffContig < n) {
        n = WriteBuffContig;
    }
    if (ToFillLen < n) {
        /* overwrite previous buffer */
#if dbglog_SoundStuff
        dbglog_writeln("sound buffer over flow");
#endif
        TheWriteOffset -= kOneBuffLen;
    }

    *actL = n;
    return TheSoundBuffer + (TheWriteOffset & kAllBuffMask);
}

GLOBALPROC MySound_EndWrite(ui4r actL) {
    TheWriteOffset += actL;
    if ((TheWriteOffset & kOneBuffMask) == 0) {
        /* just finished a block */
        TheFillOffset = TheWriteOffset;
    }
}

LOCALPROC MySound_SecondNotify(void) {
    if (MinFilledSoundBuffs <= kSoundBuffers) {
        if (MinFilledSoundBuffs > DesiredMinFilledSoundBuffs) {
#if dbglog_SoundStuff
            dbglog_writeln("MinFilledSoundBuffs too high");
#endif
            NextTickChangeTime += MyTickDuration;
        } else if (MinFilledSoundBuffs < DesiredMinFilledSoundBuffs) {
#if dbglog_SoundStuff
            dbglog_writeln("MinFilledSoundBuffs too low");
#endif
            ++TrueEmulatedTime;
        }
#if dbglog_SoundBuffStats
        dbglog_writelnNum("MinFilledSoundBuffs",
                          MinFilledSoundBuffs);
        dbglog_writelnNum("MaxFilledSoundBuffs",
                          MaxFilledSoundBuffs);
        MaxFilledSoundBuffs = 0;
#endif
        MinFilledSoundBuffs = kSoundBuffers + 1;
    }
}

LOCALPROC ZapAudioVars(void) {

}

LOCALPROC MySound_Start(void) {
    /* Reset variables */
    ThePlayOffset = 0;
    TheFillOffset = 0;
    TheWriteOffset = 0;
    MinFilledSoundBuffs = kSoundBuffers + 1;
#if dbglog_SoundBuffStats
    MaxFilledSoundBuffs = 0;
#endif
}

LOCALPROC MySound_Stop(void) {

}

static int MySound_Callback(void *context, int16_t *left, int16_t *right, int len) {
    // TODO: handle underflow
    for (int i=0; i < len; i++) {
        unsigned char sample = TheSoundBuffer[(ThePlayOffset + (i/2)) % kAllBuffSz];
        left[i] = right[i] = sample << 7;
    }
    ThePlayOffset = (ThePlayOffset + len/2) % kAllBuffSz;
    return 1;
}

LOCALFUNC blnr MySound_Init(void) {
    MySoundSource = pd->sound->addSource(MySound_Callback, NULL, 0);
    MySound_Start();
    return trueblnr;
}

LOCALPROC MySound_UnInit(void) {
    pd->sound->removeSource(MySoundSource);
}

#endif

#pragma mark - Drives

#define NotAfileRef NULL
LOCALVAR SDFile *Drives[NumDrives]; /* open disk image files */
#if IncludeSonyGetName || IncludeSonyNew
#define DRIVE_NAME_MAX 255
LOCALVAR char DriveNames[NumDrives][DRIVE_NAME_MAX+1];
#endif

LOCALPROC InitDrives(void) {
    /*
     This isn't really needed, Drives[i] and DriveNames[i]
     need not have valid values when not vSonyIsInserted[i].
     */
    tDrive i;

    for (i = 0; i < NumDrives; ++i) {
        Drives[i] = NULL;
#if IncludeSonyGetName || IncludeSonyNew
        bzero(DriveNames[i], DRIVE_NAME_MAX+1);
#endif
    }
}

LOCALPROC UnInitDrives(void) {
    for (tDrive i = 0; i < NumDrives; ++i) {
        if (vSonyIsInserted(i)) {
            (void)vSonyEject(i);
        }
    }
}

LOCALFUNC blnr InsertDiskNamed(const char *name) {
    if (strlen(name) > DRIVE_NAME_MAX) {
        // too long name
        return falseblnr;
    }
    // free disks?
    tDrive Drive_No;
    if (!FirstFreeDisk(&Drive_No)) {
        MacMsg(kStrTooManyImagesTitle, kStrTooManyImagesMessage,
               falseblnr);
        return falseblnr;
    }

    // try opening from from pdx
    SDFile *fp = pd->file->open(name, kFileRead);
    blnr locked = trueblnr;
    if (fp == NULL) {
        // try data folder
        fp = pd->file->open(name, kFileReadData);
        // open as writable
        if (fp != NULL) {
            pd->file->close(fp);
            fp = pd->file->open(name, kFileRead|kFileReadData|kFileWrite);
            locked = falseblnr;
        }
    }
    if (fp == NULL) {
        // give up
        return falseblnr;
    }

    // insert disk
    Drives[Drive_No] = fp;
    DiskInsertNotify(Drive_No, locked);
#if IncludeSonyGetName || IncludeSonyNew
    strlcpy(DriveNames[Drive_No], name, DRIVE_NAME_MAX+1);
#endif

    return trueblnr;
}

LOCALFUNC blnr LoadInitialImages(void) {
    if (!AnyDiskInserted()) {
        int n = NumDrives > 9 ? 9 : NumDrives;
        int i;
        char s[] = "disk?.dsk";

        for (i = 1; i <= n; ++i) {
            s[4] = '0' + i;
            if (!InsertDiskNamed(s)) {
                /* stop on first error (including file not found) */
                return trueblnr;
            }
        }
    }

    return trueblnr;
}

GLOBALFUNC tMacErr vSonyEject(tDrive Drive_No) {
    pd->file->close(Drives[Drive_No]);
    DiskEjectedNotify(Drive_No);
    Drives[Drive_No] = NotAfileRef;
    DriveNames[Drive_No][0] = 0;
    return mnvm_noErr;
}

#if IncludeSonyNew
GLOBALFUNC tMacErr vSonyEjectDelete(tDrive Drive_No) {
    pd->system->error("vSonyEjectDelete not implemented");
    return mnvm_miscErr;
}
#endif

#if IncludeSonyGetName
GLOBALFUNC tMacErr vSonyGetName(tDrive Drive_No, tPbuf *r) {
    // TODO: convert to MacRoman
    return PbufNewFromPtr(DriveNames[Drive_No], (ui5r)strlen(DriveNames[Drive_No]), r);
}
#endif

GLOBALFUNC tMacErr vSonyGetSize(tDrive Drive_No, ui5r *Sony_Count) {
    SDFile *fp = Drives[Drive_No];
    if (fp == NULL || pd->file->seek(fp, 0, SEEK_END) != 0) {
        return mnvm_miscErr;
    }
    *Sony_Count = pd->file->tell(fp);
    return mnvm_noErr;
}

GLOBALFUNC tMacErr vSonyTransfer(blnr IsWrite, ui3p Buffer, tDrive Drive_No, ui5r Sony_Start, ui5r Sony_Count, ui5r *Sony_ActCount) {
    tMacErr err = mnvm_miscErr;
    SDFile *fp = Drives[Drive_No];
    ui5r BytesTransferred = 0;

    if (0 == pd->file->seek(fp, Sony_Start, SEEK_SET)) {
        if (IsWrite) {
            BytesTransferred = pd->file->write(fp, Buffer, Sony_Count);
        } else {
            BytesTransferred = pd->file->read(fp, Buffer, Sony_Count);
        }

        if (BytesTransferred == Sony_Count) {
            err = mnvm_noErr;
        }
    }

    if (nullpr != Sony_ActCount) {
        *Sony_ActCount = BytesTransferred;
    }

    return err;
}

LOCALFUNC const char * InsertDiskMenuTitle(void) {
    // TODO show error if no disk images found
    return "Insert Disk";
}

// menu fits 10 lines of 47 characters
#define DiskImageMenuSize 10
LOCALVAR char AvailableDiskImages[DiskImageMenuSize][DRIVE_NAME_MAX+1];
LOCALVAR int SelectedDiskImage;

LOCALPROC DrawInsertDiskMenuBody(void) {
    if (AvailableDiskImages[0][0] == 0) {
        DrawCellsBeginLine();
        DrawCellsFromStr("No disk images available.");
        DrawCellsEndLine();
        return;
    }
    for (int i=0; i < DiskImageMenuSize && AvailableDiskImages[i][0] != 0; i++) {
        DrawCellsBeginLine();
        if (i == SelectedDiskImage) {
            DrawCellsFromStr("- [");
        } else {
            DrawCellsFromStr("-  ");
        }
        DrawCellsFromStr(AvailableDiskImages[i]);
        if (i == SelectedDiskImage) {
            DrawCellsFromStr("]");
        }
        DrawCellsEndLine();
    }
}

LOCALFUNC blnr IsDiskImage(const char *name) {
    if (strchr(name, '/') != NULL) {
        // a directory
        return falseblnr;
    }
    const char *extension = strrchr(name, '.');
    if (extension == NULL) {
        // file without extension
        return falseblnr;
    }
    return strcasecmp(".dsk", extension) == 0 || strcasecmp(".img", extension) == 0;
}

LOCALFUNC blnr IsDiskInserted(const char *name) {
    for (int i=0; i < NumDrives; i++) {
        if (strcmp(DriveNames[i], name) == 0) {
            return trueblnr;
        }
    }
    return falseblnr;
}

static void ListFilesCallback(const char *name, void *userdata) {
    int* index = (int*)userdata;
    if (IsDiskImage(name) && !IsDiskInserted(name) && *index + 1 < DiskImageMenuSize) {
        strlcpy(AvailableDiskImages[*index], name, DRIVE_NAME_MAX+1);
        *index += 1;
    }
}

LOCALPROC InsertDiskMenuCallback(void *userdata) {
    // clear
    SelectedDiskImage = 0;
    for (int i=0; i < DiskImageMenuSize; i++) {
        AvailableDiskImages[i][0] = 0;
    }
    int index = 0;
    pd->file->listfiles("/", ListFilesCallback, &index, 0);
    SpecialModeSet(SpclModeInsertDisk);
    NeedWholeScreenDraw = trueblnr;
}

PDButtons lastInput = 0;

LOCALPROC HandleDiskMenuInput(PDButtons current, float crankChange) {
    if (current == lastInput && crankChange == 0.0f) {
        return;
    }
    PDButtons pushed = current & ~lastInput;
    lastInput = current;
    if (pushed & kButtonA) {
        // insert selected disk
        InsertDiskNamed(AvailableDiskImages[SelectedDiskImage]);
        SpecialModeClr(SpclModeInsertDisk);
    } else if (pushed & kButtonB) {
        // cancel
        SpecialModeClr(SpclModeInsertDisk);
    } else if ((crankChange < -CrankThreshold) || (pushed & kButtonUp)) {
        // select up
        if (SelectedDiskImage > 0) {
            SelectedDiskImage -= 1;
            // play sound
        }
    } else if ((crankChange > CrankThreshold) || (pushed & kButtonDown)) {
        // select down
        int newSelectedDiskImage = SelectedDiskImage + 1;
        if (newSelectedDiskImage < DiskImageMenuSize && AvailableDiskImages[newSelectedDiskImage][0] != 0) {
            SelectedDiskImage = newSelectedDiskImage;
            // play sound
        }
    }
}

#pragma mark - platform independent code can be thought of as going here

#include "PROGMAIN.h"

LOCALPROC ZapOSGLUVars(void) {
    InitDrives();
#if MySoundEnabled
    ZapAudioVars();
#endif
}

LOCALPROC ReserveAllocAll(void) {
#if dbglog_HAVE
    dbglog_ReserveAlloc();
#endif
    ReserveAllocOneBlock(&ROM, kROM_Size, 5, falseblnr);

    ReserveAllocOneBlock(&screencomparebuff,
                         vMacScreenNumBytes, 5, trueblnr);
    ReserveAllocOneBlock(&CntrlDisplayBuff,
                         vMacScreenNumBytes, 5, falseblnr);
#if MySoundEnabled
    ReserveAllocOneBlock((ui3p *)&TheSoundBuffer,
                         dbhBufferSize, 5, falseblnr);
#endif

    EmulationReserveAlloc();
}

LOCALFUNC blnr AllocMyMemory(void) {
#if 0 /* for testing start up error reporting */
    MacMsg(kStrOutOfMemTitle, kStrOutOfMemMessage, trueblnr);
    return falseblnr;
#else
    uimr n;
    blnr IsOk = falseblnr;

    ReserveAllocOffset = 0;
    ReserveAllocBigBlock = nullpr;
    ReserveAllocAll();
    n = ReserveAllocOffset;
    ReserveAllocBigBlock = (ui3p)calloc(1, n);
    if (NULL == ReserveAllocBigBlock) {
        MacMsg(kStrOutOfMemTitle, kStrOutOfMemMessage, trueblnr);
    } else {
        ReserveAllocOffset = 0;
        ReserveAllocAll();
        if (n != ReserveAllocOffset) {
            /* oops, program error */
        } else {
            IsOk = trueblnr;
        }
    }

    return IsOk;
#endif
}

LOCALPROC UnallocMyMemory(void) {
    if (nullpr != ReserveAllocBigBlock) {
        free((char *)ReserveAllocBigBlock);
        RAM = nullpr;
#if EmVidCard
        VidROM = nullpr;
#endif
#if IncludeVidMem
        VidMem = nullpr;
#endif
    }
}

LOCALFUNC blnr InitOSGLU(void) {
    blnr IsOk = falseblnr;
    if (AllocMyMemory())
        if (Screen_Init())
#if dbglog_HAVE
            if (dbglog_open())
#endif
#if MySoundEnabled
                if (MySound_Init())
                /* takes a while to stabilize, do as soon as possible */
#endif
                    if (LoadInitialImages())
                        if (LoadMacRom())
                            if (InitLocationDat()) {
                                InitKeyCodes();
                                IsOk = trueblnr;
                            }

    return IsOk;
}

LOCALPROC CheckSavedMacMsg(void) {
    if (nullpr != SavedBriefMsg) {
        pd->system->error("%s\n%s", SavedBriefMsg ?: SavedLongMsg, SavedLongMsg);
    }
}

LOCALPROC UnInitOSGLU(void) {
#if MySoundEnabled
    MySound_Stop();
#endif
#if MySoundEnabled
    MySound_UnInit();
#endif
#if IncludePbufs
    UnInitPbufs();
#endif
    UnInitDrives();

#if dbglog_HAVE
    dbglog_close();
#endif

    CheckSavedMacMsg();
    Screen_UnInit();

    UnallocMyMemory();
}

LOCALPROC MyUpdateScreen(void) {
    uint8_t *buf = pd->graphics->getFrame();
    if (NeedWholeScreenDraw) {
        ScreenChangedAll();
    }
    ui3p drawBuff = GetCurDrawBuff();
    if (ScreenChangedBottom > ScreenChangedTop) {
        for (int i=ScreenChangedTop; i <= ScreenChangedBottom; i++) {
            if (i < LCD_ROWS) {
                memcpy(buf + (i * LCD_ROWSIZE), drawBuff + (i * vMacScreenByteWidth), LCD_ROWSIZE);
            }
        }
        pd->graphics->markUpdatedRows(ScreenChangedTop, LCD_ROWS-1);
        ScreenClearChanges();
    }
}

IMPORTPROC RunEmulatedTicksToTrueTime(void);
IMPORTPROC DoEmulateExtraTime(void);
IMPORTPROC DoEmulateOneTick(void);

LOCALFUNC int DoUpdate(void* userdata) {
    pd = userdata;
    if (showFPS) {
        pd->system->drawFPS(380,4);
    }

    if (ForceMacOff) {
        pd->graphics->clear(kColorBlack);
        return 1;
    }

    /*WaitForNextTick();
     RunEmulatedTicksToTrueTime();
     DoEmulateExtraTime();*/

    if (!SpeedStopped) {
        CheckDateTime();
        EmVideoDisable = trueblnr;
        for (int i=0; i < (TicksPerFrame - 1); i++) {
            DoEmulateOneTick();
        }
        EmVideoDisable = falseblnr;
        UpdateInput();
        DoEmulateOneTick();
    }

    // update screen
    MyUpdateScreen();

    return 1;
}
