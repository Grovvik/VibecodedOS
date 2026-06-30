#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    setcolor(FB_CYAN, FB_BLACK);
    print("Available commands:\n");
    setcolor(FB_WHITE, FB_BLACK);
    print("  help     - Show this help\n");
    print("  clear    - Clear screen\n");
    print("  echo     - Print text\n");
    print("  cd       - Change directory\n");
    print("  pwd      - Print working directory\n");
    print("  version  - Show OS version\n");
    print("  ticks    - Show timer tick count\n");
    print("  reboot   - Reboot system\n");
    print("  halt     - Halt system\n");
    print("  meminfo  - Show memory info\n");
    print("  colors   - Show color test\n");
    print("  ls [dir] - List directory\n");
    print("  cat      - Display file contents\n");
    print("  write    - Write text to file\n");
    print("  rm       - Delete file\n");
    print("  mkdir    - Create directory\n");
    print("  cp       - Copy file\n");
    print("  mv       - Move/rename file\n");
    print("  run      - Execute program\n");
    print("  ps       - List processes\n");
    print("  >        - Redirect output to file\n");
}