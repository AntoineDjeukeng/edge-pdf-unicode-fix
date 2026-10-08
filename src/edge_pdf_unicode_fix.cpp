
#define _GNU_SOURCE
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Edge PDF Czech/Central-European FreeText runtime fix candidate.
// This does NOT modify /opt/microsoft/msedge/msedge on disk.
//
// Verified path:
//   GenerateFreeTextAP -> CPVT_FontMap gets the real resource_dict in R12.
//   SetupAnnotSysPDFFont -> AddNativeInteractiveFormFont returns NULL on Linux.
//   Fallback CFX_Font creation, LoadSubstFace(Windows-1250), and AddFont all
//   succeeded in the previous diagnostic run.
//
// This version adds only the remaining alias assignment ("FallbackEE"),
// returns the created CPDF_Font*, and lets Edge continue normally.

static constexpr uintptr_t VA_FREETEXT_FONTMAP_CALL = 0x0d2b0c51;
static constexpr uintptr_t VA_FONTMAP_CTOR          = 0x0d2c4470;

static constexpr uintptr_t VA_SETUP_ADDNATIVE_CALL = 0x0d2c46d3;
static constexpr uintptr_t VA_ADD_NATIVE_IF_FONT   = 0x0d2b8b30;

static constexpr uintptr_t VA_OPERATOR_NEW         = 0x0d7b2270;
static constexpr uintptr_t VA_STRINGDATA_CREATE    = 0x04820f00;
static constexpr uintptr_t VA_STRINGDATA_RELEASE   = 0x04821020;
static constexpr uintptr_t VA_LOAD_SUBST_FACE      = 0x0d1224d0;
static constexpr uintptr_t VA_DOCPAGEDATA_ADDFONT  = 0x0d0e9600;

static constexpr uintptr_t DOC_PAGE_DATA_OFFSET    = 0x0b8;

static const unsigned char EXPECT_FREETEXT_CALL[5] =
    {0xE8, 0x1A, 0x38, 0x01, 0x00};
static const unsigned char EXPECT_SETUP_CALL[5] =
    {0xE8, 0x58, 0x44, 0xFF, 0xFF};

static uintptr_t g_base = 0;
static volatile sig_atomic_t g_stage = 0;

using AddNativeIFFn = void* (*)(void*, void*);
using EdgeNewFn = void* (*)(size_t);
using StringCreateFn = void* (*)(const char*, size_t);
using StringReleaseFn = void (*)(void*);
using LoadSubstFaceFn = int (*)(void*, const void*, int, unsigned int,
                                int, int, unsigned int, int);
using DocPageDataAddFontFn = void* (*)(void*, void*, unsigned int);

static AddNativeIFFn         g_add_native = nullptr;
static EdgeNewFn             g_edge_new = nullptr;
static StringCreateFn        g_string_create = nullptr;
static StringReleaseFn       g_string_release = nullptr;
static LoadSubstFaceFn       g_load_subst_face = nullptr;
static DocPageDataAddFontFn  g_add_font = nullptr;

static void raw_write(const char* s) {
    (void)!write(STDERR_FILENO, s, strlen(s));
}

static bool this_is_msedge_executable() {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0)
        return false;
    path[n] = '\0';

    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, "msedge") == 0;
}

static void log_stage(int stage, const char* msg) {
    g_stage = stage;
    char buf[384];
    int n = snprintf(buf, sizeof(buf),
                     "[edge-pdf-fix] pid=%d stage %d: %s\n",
                     (int)getpid(), stage, msg);
    if (n > 0)
        (void)!write(STDERR_FILENO, buf, (size_t)n);
}

static const char* stage_name(sig_atomic_t s) {
    switch (s) {
        case 0:   return "not started";
        case 1:   return "confirmed executable is msedge";
        case 2:   return "found msedge PIE load base";
        case 3:   return "verified original call-site bytes";
        case 4:   return "resolved internal Edge addresses";
        case 5:   return "installed both runtime patches";
        case 10:  return "fallback bridge entered";
        case 20:  return "native font call returned NULL";
        case 30:  return "allocating fallback CFX_Font";
        case 31:  return "fallback CFX_Font initialized";
        case 40:  return "creating Arial ByteString";
        case 41:  return "Arial ByteString created";
        case 50:  return "calling LoadSubstFace";
        case 51:  return "LoadSubstFace returned";
        case 60:  return "releasing Arial ByteString";
        case 61:  return "Arial ByteString released";
        case 70:  return "reading doc_page_data";
        case 71:  return "doc_page_data valid";
        case 80:  return "calling AddFont";
        case 81:  return "AddFont returned";
        case 90:  return "creating FallbackEE alias";
        case 91:  return "FallbackEE alias created";
        case 92:  return "replacing sys_font_alias ByteString";
        case 93:  return "sys_font_alias replaced";
        case 100: return "returning fallback CPDF_Font to Edge";
        case 110: return "native font already available; unchanged path";
        default:  return "unknown stage";
    }
}

static void crash_handler(int sig, siginfo_t*, void*) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "\n[edge-pdf-fix] pid=%d CAUGHT SIGNAL %d at stage %d: %s\n"
        "[edge-pdf-fix] Stop here and send this output back.\n",
        (int)getpid(), sig, (int)g_stage, stage_name(g_stage));
    if (n > 0)
        (void)!write(STDERR_FILENO, buf, (size_t)n);
    _exit(128 + sig);
}

static void install_signal_handlers() {
    struct sigaction sa {};
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGTRAP, &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}

static int find_main_cb(struct dl_phdr_info* info, size_t, void*) {
    if (!info->dlpi_name || info->dlpi_name[0] == '\0') {
        g_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static bool rel32_ok(uintptr_t from_after_call, uintptr_t to) {
    int64_t d = (int64_t)to - (int64_t)from_after_call;
    return d >= INT32_MIN && d <= INT32_MAX;
}

static void* alloc_near(uintptr_t target, size_t page_size) {
    const uintptr_t step  = 0x10000;
    const uintptr_t limit = 0x70000000;
    uintptr_t center = target & ~(page_size - 1);

    for (uintptr_t delta = step; delta < limit; delta += step) {
        uintptr_t candidates[2] = {center + delta, center - delta};
        for (uintptr_t addr : candidates) {
            if (addr < 0x10000)
                continue;

            void* p = mmap((void*)addr, page_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1, 0);
            if (p != MAP_FAILED) {
                if (rel32_ok(target + 5, (uintptr_t)p))
                    return p;
                munmap(p, page_size);
            }
        }
    }
    return nullptr;
}

static bool patch_rel32_call(uintptr_t callsite, uintptr_t target,
                             const unsigned char expected[5]) {
    unsigned char* p = (unsigned char*)callsite;

    if (memcmp(p, expected, 5) != 0) {
        fprintf(stderr,
                "[edge-pdf-fix] pid=%d refusing patch at %p: byte mismatch\n",
                (int)getpid(), (void*)callsite);
        return false;
    }

    int64_t rel64 = (int64_t)target - (int64_t)(callsite + 5);
    if (rel64 < INT32_MIN || rel64 > INT32_MAX) {
        raw_write("[edge-pdf-fix] rel32 target out of range\n");
        return false;
    }

    int32_t rel = (int32_t)rel64;
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page = callsite & ~((uintptr_t)ps - 1);

    if (mprotect((void*)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        fprintf(stderr, "[edge-pdf-fix] mprotect RWX failed: %s\n",
                strerror(errno));
        return false;
    }

    p[0] = 0xE8;
    memcpy(p + 1, &rel, 4);
    __builtin___clear_cache((char*)p, (char*)p + 5);

    (void)mprotect((void*)page, (size_t)ps, PROT_READ | PROT_EXEC);
    return true;
}

extern "C" __attribute__((noinline))
void* edge_pdf_add_native_fix(void* document, void* name_tag) {
    log_stage(10, "calling original AddNativeInteractiveFormFont");

    void* result = g_add_native(document, name_tag);
    if (result) {
        log_stage(110, "native font already available; using original result");
        return result;
    }

    log_stage(20, "original AddNativeInteractiveFormFont returned NULL");

    log_stage(30, "allocating 0x58-byte fallback CFX_Font");
    void* font = g_edge_new(0x58);
    if (!font)
        return nullptr;

    // Edge's own native-font helper initializes CFX_Font this way.
    memset(font, 0, 0x58);
    log_stage(31, "fallback CFX_Font zero-initialized");

    log_stage(40, "creating temporary ByteString backing data for Arial");
    void* font_name_data = g_string_create("Arial", 5);
    if (!font_name_data)
        return nullptr;
    log_stage(41, "Arial ByteString backing data created");

    void* font_name_bs = font_name_data;

    log_stage(50, "calling CFX_Font::LoadSubstFace with Windows-1250");
    (void)g_load_subst_face(font, &font_name_bs,
                            1,       // TrueType
                            0, 0, 0,
                            1250,    // Central European
                            0);      // non-vertical
    log_stage(51, "LoadSubstFace returned");

    log_stage(60, "releasing temporary Arial ByteString");
    g_string_release(font_name_data);
    log_stage(61, "temporary Arial ByteString released");

    log_stage(70, "reading CPDF_Document::doc_page_data_");
    if (!document)
        return nullptr;

    void* doc_page_data =
        *(void**)((unsigned char*)document + DOC_PAGE_DATA_OFFSET);
    if (!doc_page_data)
        return nullptr;
    log_stage(71, "doc_page_data_ is valid");

    log_stage(80, "calling CPDF_DocPageData::AddFont(font, 0xEE)");
    result = g_add_font(doc_page_data, font, 0xEE);
    log_stage(81, "CPDF_DocPageData::AddFont returned");

    if (!result)
        return nullptr;

    // Remaining operation that the previous diagnostic intentionally skipped.
    log_stage(90, "creating ByteString backing data for alias FallbackEE");
    void* alias_data = g_string_create("FallbackEE", 10);
    if (!alias_data)
        return nullptr;
    log_stage(91, "FallbackEE alias backing data created");

    // In this M154 ABI, ByteString is one StringData*.
    // name_tag points directly to CPVT_FontMap::sys_font_alias_.
    log_stage(92, "replacing sys_font_alias ByteString");

    void** alias_slot = (void**)name_tag;
    if (!alias_slot)
        return nullptr;

    void* old_alias = *alias_slot;
    *alias_slot = alias_data;       // ownership of the new ref moves to ByteString

    if (old_alias)
        g_string_release(old_alias);

    log_stage(93, "sys_font_alias is now FallbackEE");
    log_stage(100, "returning fallback CPDF_Font to normal Edge code");

    return result;
}

static bool create_trampolines(uintptr_t call1, uintptr_t ctor,
                               uintptr_t* t1_out, uintptr_t* t2_out) {
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0)
        return false;

    unsigned char* page =
        (unsigned char*)alloc_near(call1, (size_t)ps);
    if (!page) {
        raw_write("[edge-pdf-fix] could not allocate near trampoline page\n");
        return false;
    }

    unsigned char* t1 = page + 0x00;
    unsigned char* t2 = page + 0x40;

    // First source fix:
    //
    // Edge at 0xd2b0c51 is about to call:
    // CPVT_FontMap(doc, nullptr, default_font, font_name)
    //
    // Edge's optimized M154 code has already built resource_dict and keeps it
    // in R12 here. Replace only the nullptr argument:
    //
    //     RDX = R12
    //
    // then tail-jump to the original constructor.
    size_t i = 0;
    t1[i++] = 0x4C; t1[i++] = 0x89; t1[i++] = 0xE2;  // mov rdx,r12
    t1[i++] = 0x48; t1[i++] = 0xB8;                  // movabs rax,ctor
    memcpy(t1 + i, &ctor, 8); i += 8;
    t1[i++] = 0xFF; t1[i++] = 0xE0;                  // jmp rax

    // Second source fix: wrap AddNativeInteractiveFormFont with the proven
    // Central-European fallback.
    uintptr_t bridge = (uintptr_t)&edge_pdf_add_native_fix;
    i = 0;
    t2[i++] = 0x48; t2[i++] = 0xB8;
    memcpy(t2 + i, &bridge, 8); i += 8;
    t2[i++] = 0xFF; t2[i++] = 0xE0;

    __builtin___clear_cache((char*)page, (char*)page + 0x80);

    if (mprotect(page, (size_t)ps, PROT_READ | PROT_EXEC) != 0) {
        fprintf(stderr,
                "[edge-pdf-fix] trampoline mprotect RX failed: %s\n",
                strerror(errno));
        munmap(page, (size_t)ps);
        return false;
    }

    *t1_out = (uintptr_t)t1;
    *t2_out = (uintptr_t)t2;
    return true;
}

__attribute__((constructor))
static void install_edge_pdf_fix() {
    // LD_PRELOAD is inherited by helper executables. Never touch Edge offsets
    // unless the current executable is really msedge.
    if (!this_is_msedge_executable())
        return;

    g_stage = 1;
    install_signal_handlers();
    log_stage(1, "confirmed current executable is msedge");

    dl_iterate_phdr(find_main_cb, nullptr);
    if (!g_base)
        return;
    log_stage(2, "found msedge PIE load base");

    uintptr_t call1 = g_base + VA_FREETEXT_FONTMAP_CALL;
    uintptr_t ctor  = g_base + VA_FONTMAP_CTOR;
    uintptr_t call2 = g_base + VA_SETUP_ADDNATIVE_CALL;

    // Version/build guard.
    if (memcmp((void*)call1, EXPECT_FREETEXT_CALL, 5) != 0 ||
        memcmp((void*)call2, EXPECT_SETUP_CALL, 5) != 0) {
        raw_write(
            "[edge-pdf-fix] Edge machine bytes do not match the verified "
            "154.0.4258.37 build. No patch installed.\n");
        return;
    }
    log_stage(3, "verified original Edge call-site bytes");

    g_add_native      = (AddNativeIFFn)(g_base + VA_ADD_NATIVE_IF_FONT);
    g_edge_new        = (EdgeNewFn)(g_base + VA_OPERATOR_NEW);
    g_string_create   = (StringCreateFn)(g_base + VA_STRINGDATA_CREATE);
    g_string_release  = (StringReleaseFn)(g_base + VA_STRINGDATA_RELEASE);
    g_load_subst_face = (LoadSubstFaceFn)(g_base + VA_LOAD_SUBST_FACE);
    g_add_font        = (DocPageDataAddFontFn)(g_base + VA_DOCPAGEDATA_ADDFONT);
    log_stage(4, "resolved internal Edge addresses");

    uintptr_t t1 = 0, t2 = 0;
    if (!create_trampolines(call1, ctor, &t1, &t2))
        return;

    if (!patch_rel32_call(call1, t1, EXPECT_FREETEXT_CALL))
        return;
    if (!patch_rel32_call(call2, t2, EXPECT_SETUP_CALL))
        return;

    log_stage(5, "installed both runtime patches");
    fprintf(stderr,
        "[edge-pdf-fix] pid=%d READY. Test Add Text with: aěčřďťňb\n",
        (int)getpid());
}
