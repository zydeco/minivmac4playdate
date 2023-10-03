#include "PICOMMON.h"
#include "MINEM68K.h"


/* Memory Address Translation Cache record */

struct MATCr {
    ui5r cmpmask;
    ui5r cmpvalu;
    ui5r usemask;
    ui3p usebase;
};
typedef struct MATCr MATCr;
typedef MATCr *MATCp;

LOCALVAR struct {
    MATCr MATCrdB;
    MATCr MATCwrB;
    MATCr MATCrdW;
    MATCr MATCwrW;
    ATTep HeadATTel;
    ui3b *fIPL;
} V_regs;

#include "Musashi/m68kcpu.c"

IMPORTPROC SetHeadATTel(ATTep p);

GLOBALPROC MINEM68K_Init(ui3b *fIPL) {
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    SetHeadATTel(nullpr);
    V_regs.fIPL = fIPL;
}

GLOBALPROC m68k_reset(void) {
    m68k_pulse_reset();
}

GLOBALPROC m68k_IPLchangeNtfy(void) {
    m68k_set_irq(*V_regs.fIPL);
    m68k_end_timeslice();
}

GLOBALPROC m68k_go_nCycles(ui5b n) {
    m68k_execute(n >> kLn2CycleScale);
}

GLOBALFUNC si5r GetCyclesRemaining(void) {
    int remaining = m68k_cycles_remaining();
    if (remaining < 0) {
        m68k_end_timeslice();
        return 0;
    } else {
        return remaining << kLn2CycleScale;
    }
}

GLOBALPROC SetCyclesRemaining(si5r cycles) {
    m68k_end_timeslice();
    if (cycles > 0) {
        m68k_modify_timeslice(cycles >> kLn2CycleScale);
    }
}

GLOBALPROC DiskInsertedPsuedoException(CPTR newpc, ui5b data)
{
    // raise pseudo-exception to newpc
    uint sr = m68ki_init_exception();
    m68ki_stack_frame_3word(m68k_get_reg(NULL, M68K_REG_PC), sr);
    m68k_set_reg(M68K_REG_PC, newpc);

    // push argument
    m68ki_push_32(data);

    // does this work? driver calls _PostEvent and then nothing happens
}

GLOBALFUNC ATTep FindATTel(CPTR addr) {
    ATTep prev;
    ATTep p;

    p = V_regs.HeadATTel;
    if ((addr & p->cmpmask) != p->cmpvalu) {
        do {
            prev = p;
            p = p->Next;
        } while ((addr & p->cmpmask) != p->cmpvalu);

        {
            ATTep next = p->Next;

            if (nullpr == next) {
                /* don't move the end guard */
            } else {
                /* move to first */
                prev->Next = next;
                p->Next = V_regs.HeadATTel;
                V_regs.HeadATTel = p;
            }
        }
    }

    return p;
}

GLOBALPROC SetHeadATTel(ATTep p) {
    V_regs.MATCrdB.cmpmask = 0;
    V_regs.MATCrdB.cmpvalu = 0xFFFFFFFF;
    V_regs.MATCwrB.cmpmask = 0;
    V_regs.MATCwrB.cmpvalu = 0xFFFFFFFF;
    V_regs.MATCrdW.cmpmask = 0;
    V_regs.MATCrdW.cmpvalu = 0xFFFFFFFF;
    V_regs.MATCwrW.cmpmask = 0;
    V_regs.MATCwrW.cmpvalu = 0xFFFFFFFF;
    V_regs.HeadATTel = p;
}

#define LocalFindATTel FindATTel

LOCALPROC SetUpMATC(
                    MATCp CurMATC,
                    ATTep p)
{
    CurMATC->cmpmask = p->cmpmask;
    CurMATC->usemask = p->usemask;
    CurMATC->cmpvalu = p->cmpvalu;
    CurMATC->usebase = p->usebase;
}

#define LocalMMDV_Access MMDV_Access
#define LocalMemAccessNtfy MemAccessNtfy

LOCALINLINEFUNC ui5r my_reg_call get_byte_ext(CPTR addr)
{
    ATTep p;
    ui3p m;
    ui5r AccFlags;
    ui5r Data;

Label_Retry:
    p = LocalFindATTel(addr);
    AccFlags = p->Access;

    if (0 != (AccFlags & kATTA_readreadymask)) {
        SetUpMATC(&V_regs.MATCrdB, p);
        m = p->usebase + (addr & p->usemask);

        Data = *m;
    } else if (0 != (AccFlags & kATTA_mmdvmask)) {
        Data = LocalMMDV_Access(p, 0, falseblnr, trueblnr, addr);
    } else if (0 != (AccFlags & kATTA_ntfymask)) {
        if (LocalMemAccessNtfy(p)) {
            goto Label_Retry;
        } else {
            Data = 0; /* fail */
        }
    } else {
        Data = 0; /* fail */
    }

    return ui5r_FromSByte(Data);
}

LOCALINLINEFUNC ui5r get_byte(CPTR addr)
{
    ui3p m = (addr & V_regs.MATCrdB.usemask) + V_regs.MATCrdB.usebase;

    if ((addr & V_regs.MATCrdB.cmpmask) == V_regs.MATCrdB.cmpvalu) {
        return ui5r_FromSByte(*m);
    } else {
        return get_byte_ext(addr);
    }
}

LOCALINLINEPROC my_reg_call put_byte_ext(CPTR addr, ui5r b)
{
    ATTep p;
    ui3p m;
    ui5r AccFlags;

Label_Retry:
    p = LocalFindATTel(addr);
    AccFlags = p->Access;

    if (0 != (AccFlags & kATTA_writereadymask)) {
        SetUpMATC(&V_regs.MATCwrB, p);
        m = p->usebase + (addr & p->usemask);
        *m = b;
    } else if (0 != (AccFlags & kATTA_mmdvmask)) {
        (void) LocalMMDV_Access(p, b & 0x00FF,
                                trueblnr, trueblnr, addr);
    } else if (0 != (AccFlags & kATTA_ntfymask)) {
        if (LocalMemAccessNtfy(p)) {
            goto Label_Retry;
        } else {
            /* fail */
        }
    } else {
        /* fail */
    }
}

LOCALINLINEPROC my_reg_call put_byte(CPTR addr, ui5r b)
{
    ui3p m = (addr & V_regs.MATCwrB.usemask) + V_regs.MATCwrB.usebase;
    if ((addr & V_regs.MATCwrB.cmpmask) == V_regs.MATCwrB.cmpvalu) {
        *m = b;
    } else {
        put_byte_ext(addr, b);
    }
}

LOCALINLINEFUNC ui5r my_reg_call get_word_ext(CPTR addr)
{
    ui5r Data;

    if (0 != (addr & 0x01)) {
        ui5r hi = get_byte(addr);
        ui5r lo = get_byte(addr + 1);
        Data = ((hi << 8) & 0x0000FF00)
        | (lo & 0x000000FF);
    } else {
        ATTep p;
        ui3p m;
        ui5r AccFlags;

    Label_Retry:
        p = LocalFindATTel(addr);
        AccFlags = p->Access;

        if (0 != (AccFlags & kATTA_readreadymask)) {
            SetUpMATC(&V_regs.MATCrdW, p);
            V_regs.MATCrdW.cmpmask |= 0x01;
            m = p->usebase + (addr & p->usemask);
            Data = do_get_mem_word(m);
        } else if (0 != (AccFlags & kATTA_mmdvmask)) {
            Data = LocalMMDV_Access(p, 0, falseblnr, falseblnr, addr);
        } else if (0 != (AccFlags & kATTA_ntfymask)) {
            if (LocalMemAccessNtfy(p)) {
                goto Label_Retry;
            } else {
                Data = 0; /* fail */
            }
        } else {
            Data = 0; /* fail */
        }
    }

    return ui5r_FromSWord(Data);
}

LOCALINLINEFUNC ui5r my_reg_call get_word(CPTR addr)
{
    ui3p m = (addr & V_regs.MATCrdW.usemask) + V_regs.MATCrdW.usebase;
    if ((addr & V_regs.MATCrdW.cmpmask) == V_regs.MATCrdW.cmpvalu) {
        return ui5r_FromSWord(do_get_mem_word(m));
    } else {
        return get_word_ext(addr);
    }
}

LOCALINLINEPROC my_reg_call put_word_ext(CPTR addr, ui5r w)
{
    if (0 != (addr & 0x01)) {
        put_byte(addr, w >> 8);
        put_byte(addr + 1, w);
    } else {
        ATTep p;
        ui3p m;
        ui5r AccFlags;

    Label_Retry:
        p = LocalFindATTel(addr);
        AccFlags = p->Access;

        if (0 != (AccFlags & kATTA_writereadymask)) {
            SetUpMATC(&V_regs.MATCwrW, p);
            V_regs.MATCwrW.cmpmask |= 0x01;
            m = p->usebase + (addr & p->usemask);
            do_put_mem_word(m, w);
        } else if (0 != (AccFlags & kATTA_mmdvmask)) {
            (void) LocalMMDV_Access(p, w & 0x0000FFFF,
                                    trueblnr, falseblnr, addr);
        } else if (0 != (AccFlags & kATTA_ntfymask)) {
            if (LocalMemAccessNtfy(p)) {
                goto Label_Retry;
            } else {
                /* fail */
            }
        } else {
            /* fail */
        }
    }
}

LOCALINLINEPROC my_reg_call put_word(CPTR addr, ui5r w)
{
    ui3p m = (addr & V_regs.MATCwrW.usemask) + V_regs.MATCwrW.usebase;
    if ((addr & V_regs.MATCwrW.cmpmask) == V_regs.MATCwrW.cmpvalu) {
        do_put_mem_word(m, w);
    } else {
        put_word_ext(addr, w);
    }
}

LOCALINLINEFUNC ui5r my_reg_call get_long_misaligned_ext(CPTR addr)
{
    ui5r hi = get_word(addr);
    ui5r lo = get_word(addr + 2);
    ui5r Data = ((hi << 16) & 0xFFFF0000)
    | (lo & 0x0000FFFF);

    return ui5r_FromSLong(Data);
}

LOCALINLINEFUNC ui5r my_reg_call get_long_misaligned(CPTR addr)
{
    CPTR addr2 = addr + 2;
    ui3p m = (addr & V_regs.MATCrdW.usemask) + V_regs.MATCrdW.usebase;
    ui3p m2 = (addr2 & V_regs.MATCrdW.usemask) + V_regs.MATCrdW.usebase;
    if (((addr & V_regs.MATCrdW.cmpmask) == V_regs.MATCrdW.cmpvalu)
        && ((addr2 & V_regs.MATCrdW.cmpmask) == V_regs.MATCrdW.cmpvalu))
    {
        ui5r hi = do_get_mem_word(m);
        ui5r lo = do_get_mem_word(m2);
        ui5r Data = ((hi << 16) & 0xFFFF0000)
        | (lo & 0x0000FFFF);

        return ui5r_FromSLong(Data);
    } else {
        return get_long_misaligned_ext(addr);
    }
}

LOCALINLINEPROC my_reg_call put_long_misaligned_ext(CPTR addr, ui5r l)
{
    put_word(addr, l >> 16);
    put_word(addr + 2, l);
}

LOCALINLINEPROC my_reg_call put_long_misaligned(CPTR addr, ui5r l)
{
    CPTR addr2 = addr + 2;
    ui3p m = (addr & V_regs.MATCwrW.usemask) + V_regs.MATCwrW.usebase;
    ui3p m2 = (addr2 & V_regs.MATCwrW.usemask) + V_regs.MATCwrW.usebase;
    if (((addr & V_regs.MATCwrW.cmpmask) == V_regs.MATCwrW.cmpvalu)
        && ((addr2 & V_regs.MATCwrW.cmpmask) == V_regs.MATCwrW.cmpvalu))
    {
        do_put_mem_word(m, l >> 16);
        do_put_mem_word(m2, l);
    } else {
        put_long_misaligned_ext(addr, l);
    }
}

GLOBALFUNC ui3r get_vm_byte(CPTR addr) {
    return get_byte(addr);
}

GLOBALFUNC ui4r get_vm_word(CPTR addr) {
    return get_word(addr);
}

GLOBALFUNC ui5r get_vm_long(CPTR addr) {
    return get_long_misaligned(addr);
}

GLOBALPROC put_vm_byte(CPTR addr, ui3r b) {
    put_byte(addr, b);
}

GLOBALPROC put_vm_word(CPTR addr, ui4r w) {
    put_word(addr, w);
}

GLOBALPROC put_vm_long(CPTR addr, ui5r l) {
    put_long_misaligned(addr, l);
}

#if WantDisasm
#include "Musashi/m68kdasm.c"
#endif
