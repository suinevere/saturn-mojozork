/*----------------------
 | title.cxx
 | Description: Title screen, background art, TGA loading, CD directory juggling,
 |   and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, soft_reset.h, SRL
 ----------------------*/
#include "title.h"
#include "app_state.h"
#include "display.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "soft_reset.h"
#include "saturn_keyboard.h"
#include <srl.hpp>
#include <string.h>

#define SOFT_RESET_HOLD 60

static char        g_image_name[DISP_IMAGE_MAX][16];
static const char *g_image_ptr[DISP_IMAGE_MAX];

static GfsDirName g_root_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_root_tbl;
static bool       g_root_dir_valid = false;

static GfsDirName g_tga_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_tga_tbl;
static bool       g_tga_dir_valid = false;

extern GfsDirTbl g_z3_tbl;
extern bool g_z3_dir_valid;

/*----------------------
 | title_draw_art
 | Description: Draws the title screen text art.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_draw_art(void) {
    SRL::Debug::Print(13, 12, "Z - A T U R N");
    SRL::Debug::Print(4, 15, "Saturn port (c) 2026 by Suinevere");
}

/*----------------------
 | tga_name_is_usable
 | Description: Checks if a filename ends with exactly ".TGA".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the filename to check
 | Returns: true if the name ends in .TGA, false otherwise
 ----------------------*/
static bool tga_name_is_usable(const char *name) {
    if (!name || !name[0] || name[0] == '.') return false;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.' && name[i+1] == 'T' && name[i+2] == 'G' && name[i+3] == 'A'
            && name[i+4] == '\0') return true;
    }
    return false;
}

/*----------------------
 | cd_capture_root
 | Description: Snapshots the root directory record straight after GFS_Reset()
 |   so that cd_enter_root() can return to it later.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dirnames, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_capture_root(void) {
    GFS_DIRTBL_TYPE(&g_root_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_root_tbl) = g_root_dirnames;
    GFS_DIRTBL_NDIR(&g_root_tbl)    = SRL_MAX_CD_FILES;
    g_root_dir_valid = GFS_LoadDir(0, &g_root_tbl) >= 0;
}

/*----------------------
 | cd_enter_root
 | Description: Re-points the CD to the root directory captured by cd_capture_root.
 |   Used to make directory changes idempotent.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_enter_root(void) {
    if (g_root_dir_valid) GFS_SetDir(&g_root_tbl);
}

/*----------------------
 | cd_enter_tga
 | Description: Sets the CD current directory to /TGA where the background bitmaps
 |   are stored.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_tga_tbl, g_tga_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cd_enter_tga(void) {
    if (g_tga_dir_valid) GFS_SetDir(&g_tga_tbl);
}

/*----------------------
 | cd_restore_z3
 | Description: Re-points the CD at the Z3 directory so story-file opens resolve.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_z3_tbl, g_z3_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cd_restore_z3(void) {
    if (g_z3_dir_valid) GFS_SetDir(&g_z3_tbl);
}

/*----------------------
 | bitmap_read_end
 | Description: Restores the CD directory after a bitmap load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void bitmap_read_end(void) {
    cd_restore_z3();
}

/*----------------------
 | display_scan_images
 | Description: Scans the disc's TGA folder once at boot and registers any usable
 |   .TGA files found with the display system so the background selector can cycle
 |   into them.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_scan_images(void) {
    GfsDirName *dirnames = g_tga_dirnames;
    GfsDirTbl  *tblp     = &g_tga_tbl;
    int found = 0;

    g_tga_dir_valid = false;

    cd_enter_root();

    int32_t fid = GFS_NameToId((int8_t *) "TGA");
    if (fid >= 0) {
        GFS_DIRTBL_TYPE(tblp)    = GFS_DIR_NAME;
        GFS_DIRTBL_DIRNAME(tblp) = dirnames;
        GFS_DIRTBL_NDIR(tblp)    = SRL_MAX_CD_FILES;
        int32_t count = GFS_LoadDir(fid, tblp);
        g_tga_dir_valid = count >= 0;
        for (int32_t i = 0; count > 0 && i < count && found < DISP_IMAGE_MAX; i++) {
            char nm[16];
            int j = 0;
            for (; j < GFS_FNAME_LEN && j < (int) sizeof(nm) - 1; j++) {
                char c = (char) dirnames[i].fname[j];
                if (c == '\0' || c == ';') break;
                nm[j] = c;
            }
            nm[j] = '\0';
            if (!tga_name_is_usable(nm)) continue;
            int k = 0;
            for (; nm[k] && k < (int) sizeof(g_image_name[0]) - 1; k++) g_image_name[found][k] = nm[k];
            g_image_name[found][k] = '\0';
            g_image_ptr[found] = g_image_name[found];
            found++;
        }
    }

    cd_enter_root();
    display_set_images(found > 0 ? g_image_ptr : NULL, found);
}

struct RawBitmap final : SRL::Bitmap::IBitmap {
    uint8_t                *Pixels;
    SRL::Bitmap::Palette   *Pal;
    uint16_t                W, H;
    RawBitmap() : Pixels(nullptr), Pal(nullptr), W(0), H(0) {}
    ~RawBitmap() override {
        if (Pal != nullptr)    delete Pal;
        if (Pixels != nullptr) delete Pixels;
    }
    uint8_t *GetData() override { return Pixels; }
    SRL::Bitmap::BitmapInfo GetInfo() const override {
        return SRL::Bitmap::BitmapInfo(W, H, Pal);
    }
};

/*----------------------
 | tga_load_nbg0
 | Description: Loads an uncompressed 8bpp colour-mapped TGA file into VDP2 NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- the filename of the TGA image to load
 | Returns: true on success, false otherwise
 ----------------------*/
static bool tga_load_nbg0(const char *file) {
    SRL::Cd::File f(file);
    if (!f.Exists()) return false;

    static uint32_t hdrbuf[512];
    uint8_t *const hdr = (uint8_t *) hdrbuf;
    const int32_t ss = (f.Size.SectorSize > 0) ? f.Size.SectorSize : 2048;
    if (ss > (int32_t) sizeof(hdrbuf)) return false;
    if (f.LoadBytes(0, ss, hdr) <= 0) return false;

    const int idlen    = hdr[0];
    const int cmaptype = hdr[1];
    const int imgtype  = hdr[2];
    const int cmaplen  = hdr[5] | (hdr[6] << 8);
    const int cmapbits = hdr[7];
    const int w        = hdr[12] | (hdr[13] << 8);
    const int h        = hdr[14] | (hdr[15] << 8);
    const int bpp      = hdr[16];
    const int topdown  = (hdr[17] >> 5) & 1;

    if (cmaptype != 1 || imgtype != 1 || bpp != 8)      return false;
    if (cmaplen <= 0 || cmaplen > 256)                  return false;
    if (cmapbits != 24 && cmapbits != 32)               return false;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)        return false;

    const int      cmapbytes = cmaplen * (cmapbits / 8);
    const int      pixoff    = 18 + idlen + cmapbytes;
    const uint32_t npix      = (uint32_t) w * (uint32_t) h;
    if (pixoff > ss)                                    return false;
    if ((uint32_t) pixoff + npix > (uint32_t) f.Size.Bytes) return false;
    if (SRL::Memory::HighWorkRam::GetFreeSpace() < npix + 4096) return false;

    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) {
        SRL::Types::HighColor c;
        c.Opaque = (i == 0) ? 0 : 1;
        if (i < cmaplen) {
            const uint8_t *e = hdr + 18 + idlen + i * (cmapbits / 8);
            c.Blue = e[0] >> 3; c.Green = e[1] >> 3; c.Red = e[2] >> 3;
        } else {
            c.Blue = 0; c.Green = 0; c.Red = 0;
        }
        colors[i] = c;
    }

    const uint32_t skip = (uint32_t) (pixoff % ss);
    const uint32_t span = skip + npix;
    if (SRL::Memory::HighWorkRam::GetFreeSpace() < span + 4096) { delete colors; return false; }

    uint8_t *pix = new uint8_t[span];
    if (pix == nullptr) { delete colors; return false; }
    if (((unsigned int) pix & 3) != 0) {
        delete pix; delete colors; return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        delete pix; delete colors; return false;
    }
    if (skip > 0) for (uint32_t i = 0; i < npix; i++) pix[i] = pix[skip + i];

    if (!topdown) {
        static uint8_t rowbuf[1024];
        for (int y = 0; y < h / 2; y++) {
            uint8_t *a = pix + (uint32_t) y * (uint32_t) w;
            uint8_t *b = pix + (uint32_t) (h - 1 - y) * (uint32_t) w;
            for (int i = 0; i < w; i++) rowbuf[i] = a[i];
            for (int i = 0; i < w; i++) a[i]      = b[i];
            for (int i = 0; i < w; i++) b[i]      = rowbuf[i];
        }
    }

    RawBitmap bmp;
    bmp.Pixels = pix;
    bmp.W      = (uint16_t) w;
    bmp.H      = (uint16_t) h;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete pix; delete colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    return true;
}

/*----------------------
 | title_bg_show
 | Description: Loads a TGA image from the CD into VDP2 NBG0 and displays it behind
 |   the title text (menus and gameplay stay on solid black). Accepts only uncompressed
 |   8bpp colour-mapped TGA. Returns false if the load fails or the format is
 |   unsupported, so the caller can fall back to a colour background.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool title_bg_show(const char *file) {
    static char loaded[16] = "";
    bool same = true;
    for (int i = 0; i < (int) sizeof(loaded); i++) {
        if (loaded[i] != file[i]) { same = false; break; }
        if (loaded[i] == '\0') break;
    }
    if (!same) {
        cd_enter_tga();
        bool ok = tga_load_nbg0(file);
        bitmap_read_end();
        if (!ok) {
            return false;
        }
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
        SRL::Debug::PrintClearLine(20);
        SRL::Debug::PrintClearLine(21);
        int k = 0;
        for (; file[k] && k < (int) sizeof(loaded) - 1; k++) loaded[k] = file[k];
        loaded[k] = '\0';
    }
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

/*----------------------
 | title_bg_hide
 | Description: Hides the title background image by disabling scroll on VDP2 NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
}

/*----------------------
 | title_and_seed
 | Description: Displays the title screen with a "Press any button" prompt and waits
 |   for user input. Returns a random seed based on the number of elapsed frames.
 |   Also handles soft reset chords while waiting on this screen.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void) {
    int frames = 0;
    int reset_hold = 0;
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    SRL::Core::Synchronize();
    for (;;) {
        reset_hold = soft_reset_chord_held() ? (reset_hold + 1) : 0;
        if (reset_hold >= SOFT_RESET_HOLD) {
            slNMIRequest();
            while (1) {}
        }
        bool advance =
            g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
            (saturn_keyboard_poll().kind != SATURN_KEY_NONE);
        if (advance) break;
        title_draw_art();
        SRL::Debug::Print(8, 18, "Press any button to begin");
        SRL::Core::Synchronize();
        frames++;
    }
    return frames | 1;
}
