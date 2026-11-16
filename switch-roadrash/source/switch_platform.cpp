#include <switch.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <exception>
#include <typeinfo>

// Initialize RomFS and Switch homebrew environment before main
__attribute__((constructor))
static void switch_init_environment() {
    romfsInit();

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/roadrash", 0777);

    // Redirect stdout and stderr to SD card log file
    FILE *log_file = freopen("sdmc:/switch/roadrash/roadrash.log", "w", stdout);
    if (log_file) {
        setvbuf(stdout, NULL, _IONBF, 0);
    }
    freopen("sdmc:/switch/roadrash/roadrash_err.log", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("[SwitchRR] RomFS initialized successfully.\n");
    fflush(stdout);
}

__attribute__((destructor))
static void switch_cleanup_environment() {
    printf("[SwitchRR] RomFS exiting.\n");
    fflush(stdout);
    romfsExit();
}

// Exception interception to aid debugging on Switch & Ryujinx
extern "C" {
    void __real___cxa_throw(void *thrown_exception, void *p1, void (*dest)(void *));

    void __wrap___cxa_throw(void *thrown_exception, void *p1, void (*dest)(void *)) {
        const std::type_info *ti = reinterpret_cast<const std::type_info *>(p1);
        const char *name = ti ? ti->name() : "unknown";
        fprintf(stderr, "\n========================================\n");
        fprintf(stderr, "[SwitchRR FATAL EXCEPTION] Type: %s\n", name);
        try {
            std::exception *e = reinterpret_cast<std::exception *>(thrown_exception);
            if (e) {
                fprintf(stderr, "[SwitchRR FATAL EXCEPTION] what(): %s\n", e->what());
            }
        } catch (...) {
            fprintf(stderr, "[SwitchRR FATAL EXCEPTION] (could not inspect what())\n");
        }
        fprintf(stderr, "========================================\n\n");
        fflush(stderr);
        __real___cxa_throw(thrown_exception, p1, dest);
    }

    void switch_platform_log(const char* msg) {
        printf("[SwitchRR] %s\n", msg);
        fflush(stdout);
    }
}
