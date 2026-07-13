/*----------------------
 | title.cxx
 | Description: Title screen, background art, TGA loading and its Low Work RAM
 |   cache, CD directory juggling, and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, soft_reset.h, game_catalog.h
 |   (the Z3 directory record cd_restore_z3 re-applies), SRL
 ----------------------*/
#include "title.h"
#include "app_state.h"
#include "display.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "soft_reset.h"
#include "saturn_keyboard.h"
#include "game_catalog.h"
#include <srl.hpp>
#include <string.h>

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the reset chord must be held on the title screen before it
 |   triggers a hard NMI reboot.
 | Author: suinevere
 ----------------------*/
#define SOFT_RESET_HOLD 60

/*----------------------
 | g_image_name / g_image_ptr
 | Description: The registered background filenames (owned storage) and a pointer
 |   array over them, handed to the display system as the selectable image list.
 | Author: suinevere
 ----------------------*/
static char        g_image_name[DISP_IMAGE_MAX][16];
static const char *g_image_ptr[DISP_IMAGE_MAX];

/*----------------------
 | g_root_dirnames / g_root_tbl / g_root_dir_valid
 | Description: The CD root directory record captured right after GFS_Reset, so
 |   cd_enter_root can return to it; the flag guards against an unread record.
 | Author: suinevere
 ----------------------*/
static GfsDirName g_root_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_root_tbl;
static bool       g_root_dir_valid = false;

/*----------------------
 | g_tga_dirnames / g_tga_tbl / g_tga_dir_valid
 | Description: The /TGA directory record captured by display_scan_images, so
 |   cd_enter_tga can switch to the background-art folder; the flag guards it.
 | Author: suinevere
 ----------------------*/
static GfsDirName g_tga_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_tga_tbl;
static bool       g_tga_dir_valid = false;

/*----------------------
 | g_z3_tbl (extern)
 | Description: The Z3 directory record, defined in game_catalog.cxx (scan_z3_folder
 |   captures it) and re-applied here by cd_restore_z3. It lives there because
 |   game_catalog.h cannot name a GfsDirTbl without dragging SRL into a C-safe
 |   header, so only the validity flag comes from the header.
 | Author: suinevere
 ----------------------*/
extern GfsDirTbl g_z3_tbl;

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

/*----------------------
 | RawBitmap
 | Description: An IBitmap that borrows its pixel plane rather than owning it,
 |   because the plane usually belongs to the image cache and has to outlive the
 |   upload. The palette is still owned: SRL::Bitmap::Palette deletes the color
 |   array it was handed, so every RawBitmap gets its own throwaway copy of the
 |   cached colors.
 | Author: suinevere
 ----------------------*/
struct RawBitmap final : SRL::Bitmap::IBitmap {
    uint8_t                *Pixels;
    SRL::Bitmap::Palette   *Pal;
    uint16_t                W, H;
    RawBitmap() : Pixels(nullptr), Pal(nullptr), W(0), H(0) {}
    ~RawBitmap() override {
        if (Pal != nullptr) delete Pal;
    }
    uint8_t *GetData() override { return Pixels; }
    SRL::Bitmap::BitmapInfo GetInfo() const override {
        return SRL::Bitmap::BitmapInfo(W, H, Pal);
    }
};

/*----------------------
 | TgaImage
 | Description: One decoded background: the 8bpp pixel plane (top-down, leading
 |   partial sector already shifted off) plus its 256-entry palette. Pixels is the
 |   allocation base, so freeing it frees the plane. LowRam records which allocator
 |   the two blocks came from -- a cached image lives in Low Work RAM, a one-off the
 |   cache could not take lives in High Work RAM.
 | Author: suinevere
 ----------------------*/
struct TgaImage {
    uint8_t               *Pixels;
    SRL::Types::HighColor *Colors;
    uint32_t               Bytes;    /* what this image cost its allocator */
    uint16_t               W, H;
    bool                   LowRam;
    char                   Name[16];
};

/*----------------------
 | TGA_CACHE_BUDGET
 | Description: Byte budget for the background-art cache. The Saturn plays CD-DA
 |   off the same head it reads data with, so any read stops the music; every
 |   other CD read is front-loaded into the title's silent window, and the art was
 |   the last that was not (cycling pictures in Options read the disc with the menu
 |   track playing, heard as a skip). So every picture is decoded once during that
 |   window and kept. The cache lives in Low Work RAM, the 1 MB zone at 0x00200000
 |   nothing else here allocates from (High Work RAM is shared with the story file
 |   and sound buffers). The eight 320x224 shipped pictures come to ~573 KB; the
 |   budget leaves headroom for a bigger disc, and anything that does not fit still
 |   loads the old way from the CD.
 | Author: suinevere
 ----------------------*/
#define TGA_CACHE_BUDGET  (768u * 1024u)

/*----------------------
 | g_cache / g_cache_count / g_cache_bytes / g_cache_ready
 | Description: The decoded-background cache: the image slots, how many are used,
 |   how many bytes they consume (against TGA_CACHE_BUDGET), and whether the
 |   preload pass has already run (so a soft reset does not re-read).
 | Author: suinevere
 ----------------------*/
static TgaImage g_cache[DISP_IMAGE_MAX];
static int      g_cache_count = 0;
static uint32_t g_cache_bytes = 0;
static bool     g_cache_ready = false;

/*----------------------
 | tga_alloc
 | Description: Allocates from Low or High Work RAM, whichever the caller asked
 |   for, so one decoder can serve both the cache and a one-off load.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: bytes -- allocation size; low -- true for Low Work RAM
 | Returns: the allocation, or nullptr
 ----------------------*/
static void *tga_alloc(uint32_t bytes, bool low) {
    return low ? SRL::Memory::LowWorkRam::Malloc(bytes)
               : SRL::Memory::HighWorkRam::Malloc(bytes);
}

/*----------------------
 | tga_free
 | Description: Returns a block to the zone tga_alloc took it from. Null-safe.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: p -- the block; low -- true if it came from Low Work RAM
 | Returns: N/A
 ----------------------*/
static void tga_free(void *p, bool low) {
    if (p == nullptr) return;
    if (low) SRL::Memory::LowWorkRam::Free(p);
    else     SRL::Memory::HighWorkRam::Free(p);
}

/*----------------------
 | tga_free_space
 | Description: Free space in whichever zone the caller is about to allocate in.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: low -- true for Low Work RAM
 | Returns: free bytes in that zone
 ----------------------*/
static uint32_t tga_free_space(bool low) {
    return low ? (uint32_t) SRL::Memory::LowWorkRam::GetFreeSpace()
               : (uint32_t) SRL::Memory::HighWorkRam::GetFreeSpace();
}

/*----------------------
 | tga_image_free
 | Description: Releases both blocks of a decoded image and blanks it, so the
 |   slot reads as empty afterwards.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: img -- the image to release
 | Returns: N/A
 ----------------------*/
static void tga_image_free(TgaImage *img) {
    if (img == nullptr) return;
    tga_free(img->Pixels, img->LowRam);
    tga_free(img->Colors, img->LowRam);
    img->Pixels = nullptr;
    img->Colors = nullptr;
    img->Bytes  = 0;
}

/*----------------------
 | tga_decode
 | Description: Reads an uncompressed 8bpp colour-mapped TGA off the CD and
 |   decodes it into RAM: palette expanded to 256 entries, rows flipped to
 |   top-down, and the leading partial sector shifted off the front. This is the
 |   only function here that touches the disc, so it is also the only one that
 |   interrupts CD audio.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename on the disc; low -- true to allocate in Low Work RAM;
 |   limit -- most bytes the pixel plane may take; out -- filled on success
 | Returns: true on success; out is left blank and nothing is allocated on failure
 ----------------------*/
static bool tga_decode(const char *file, bool low, uint32_t limit, TgaImage *out) {
    out->Pixels  = nullptr;
    out->Colors  = nullptr;
    out->Bytes   = 0;
    out->W       = 0;
    out->H       = 0;
    out->LowRam  = low;
    out->Name[0] = '\0';

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

    const uint32_t skip    = (uint32_t) (pixoff % ss);
    const uint32_t span    = skip + npix;
    const uint32_t palsize = 256 * sizeof(SRL::Types::HighColor);
    if (span + palsize > limit)                         return false;
    if (tga_free_space(low) < span + palsize + 4096)    return false;

    SRL::Types::HighColor *colors = (SRL::Types::HighColor *) tga_alloc(palsize, low);
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

    uint8_t *pix = (uint8_t *) tga_alloc(span, low);
    // LoadBytes wants a long-aligned destination; refuse rather than corrupt.
    if (pix == nullptr || ((unsigned int) pix & 3) != 0) {
        tga_free(pix, low); tga_free(colors, low); return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        tga_free(pix, low); tga_free(colors, low); return false;
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

    out->Pixels = pix;
    out->Colors = colors;
    out->Bytes  = span + palsize;
    out->W      = (uint16_t) w;
    out->H      = (uint16_t) h;
    int k = 0;
    for (; file[k] && k < (int) sizeof(out->Name) - 1; k++) out->Name[k] = file[k];
    out->Name[k] = '\0';
    return true;
}

/*----------------------
 | tga_blit_nbg0
 | Description: Uploads an already-decoded image to VDP2 NBG0. Touches no CD, so
 |   it is safe to call with music playing.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: img -- the decoded image
 | Returns: true on success, false if the throwaway palette could not be made
 ----------------------*/
static bool tga_blit_nbg0(const TgaImage *img) {
    // SRL::Bitmap::Palette deletes the colors it is handed, so hand it a copy
    // and leave the cached palette alone.
    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) colors[i] = img->Colors[i];

    RawBitmap bmp;
    bmp.Pixels = img->Pixels;
    bmp.W      = img->W;
    bmp.H      = img->H;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    return true;
}

/*----------------------
 | tga_name_eq
 | Description: Compares two disc filenames, bounded by the cache's name field.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- the names to compare
 | Returns: true if they match
 ----------------------*/
static bool tga_name_eq(const char *a, const char *b) {
    for (int i = 0; i < (int) sizeof(g_cache[0].Name); i++) {
        if (a[i] != b[i])  return false;
        if (a[i] == '\0')  return true;
    }
    return true;
}

/*----------------------
 | tga_cache_find
 | Description: Looks up an already-decoded image by disc filename.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cache, g_cache_count
 | Params: file -- the disc filename
 | Returns: the cached image, or nullptr if it is not held
 ----------------------*/
static const TgaImage *tga_cache_find(const char *file) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_cache[i].Pixels != nullptr && tga_name_eq(g_cache[i].Name, file))
            return &g_cache[i];
    }
    return nullptr;
}

/*----------------------
 | tga_cache_admit
 | Description: Decodes an image from the CD into the Low Work RAM cache, if
 |   there is a free slot and budget left. The caller must already be in the TGA
 |   directory. Reads the disc, so it stops CD audio.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cache, g_cache_count, g_cache_bytes
 | Params: file -- the disc filename
 | Returns: the newly cached image, or nullptr if it would not fit or is unreadable
 ----------------------*/
static const TgaImage *tga_cache_admit(const char *file) {
    if (g_cache_count >= DISP_IMAGE_MAX)   return nullptr;
    if (g_cache_bytes >= TGA_CACHE_BUDGET) return nullptr;

    TgaImage *slot = &g_cache[g_cache_count];
    if (!tga_decode(file, true, TGA_CACHE_BUDGET - g_cache_bytes, slot)) return nullptr;

    g_cache_bytes += slot->Bytes;
    g_cache_count++;
    return slot;
}

/*----------------------
 | display_preload_images
 | Description: Decodes every registered background into RAM during the title
 |   screen's silent window, so that the Options menu can cycle pictures without
 |   reading the disc and interrupting the menu track. Idempotent: a soft reset
 |   returns to a cache that the longjmp left intact, exactly like the game
 |   catalog, so no second read happens. Call after display_scan_images() and
 |   before the music starts.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: g_cache_ready
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_preload_images(void) {
    if (g_cache_ready) return;
    g_cache_ready = true;

    const int n = display_image_count();
    if (n <= 0) return;

    cd_enter_tga();
    for (int i = 0; i < n; i++) {
        const char *name = display_image_file(i);
        if (name == nullptr || name[0] == '\0') continue;
        if (tga_cache_find(name) != nullptr)    continue;
        SRL::Core::Synchronize();
        tga_cache_admit(name);
    }
    SRL::Debug::PrintClearLine(20);
    bitmap_read_end();
}

/*----------------------
 | title_bg_show
 | Description: Shows a TGA image on VDP2 NBG0 behind the title text (menus and
 |   gameplay stay on solid black). Serves it from the Low Work RAM cache when
 |   display_preload_images took it, which keeps the CD idle and the music
 |   playing; otherwise reads it from the disc once. Accepts only uncompressed
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
        // Cached images upload straight from RAM with no CD access, so cycling
        // backgrounds in the Options menu leaves the music alone. Only a picture
        // the cache never took reaches the disc, and then it is read once here
        // and dropped again.
        TgaImage        oneoff   = { nullptr, nullptr, 0, 0, 0, false, { '\0' } };
        bool            borrowed = false;
        const TgaImage *img      = tga_cache_find(file);
        if (img == nullptr) {
            cd_enter_tga();
            img = tga_cache_admit(file);
            if (img == nullptr && tga_decode(file, false, 0xFFFFFFFFu, &oneoff)) {
                img = &oneoff; borrowed = true;
            }
            bitmap_read_end();
        }
        if (img == nullptr) return false;

        bool ok = tga_blit_nbg0(img);
        if (borrowed) tga_image_free(&oneoff);
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
