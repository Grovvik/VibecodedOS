#include "userlib.h"
#include "../Kernel/font8x16.h"

#define MAX_LINES 4096
#define MAX_LINE_LEN 256

static char* g_lines[MAX_LINES];
static int g_num_lines = 0;

static int g_cursor_x = 0;
static int g_cursor_y = 0;
static int g_target_x = 0;
static int g_scroll_x = 0;
static int g_scroll_y = 0;

static int g_prev_cursor_x = 0;
static int g_prev_cursor_y = 0;
static int g_prev_scroll_x = 0;
static int g_prev_scroll_y = 0;
static int g_prev_mode = 0;
static int g_prev_num_lines = 0;

static int g_has_selection = 0;
static int g_sel_start_x = 0;
static int g_sel_start_y = 0;
static int g_prev_has_selection = 0;

static char* g_clipboard = NULL;
static int g_clipboard_is_lines = 0;

static int g_screen_cols = 80;
static int g_screen_rows = 25;

static char g_status_msg[128];
static char g_filename[128];

static int g_shift_pressed = 0;
static int g_ctrl_pressed = 0;

#define MODE_EDIT 0
#define MODE_JUMP 1

static int g_mode = MODE_EDIT;
static char g_jump_buf[32];
static int g_jump_len = 0;

static FbInfo g_fb;
static int g_fb_mapped = 0;

static const char scancode_to_ascii[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

static const char scancode_shift_ascii[] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

static void set_status(const char* msg) {
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = 0;
}

static void insert_line(int index, const char* text) {
    if (g_num_lines >= MAX_LINES) return;
    for (int i = g_num_lines; i > index; i--) {
        g_lines[i] = g_lines[i - 1];
    }
    g_lines[index] = malloc(MAX_LINE_LEN);
    memset(g_lines[index], 0, MAX_LINE_LEN);
    if (text) {
        strncpy(g_lines[index], text, MAX_LINE_LEN - 1);
    }
    g_num_lines++;
}

static void remove_line(int index) {
    if (index < 0 || index >= g_num_lines) return;
    free(g_lines[index]);
    for (int i = index; i < g_num_lines - 1; i++) {
        g_lines[i] = g_lines[i + 1];
    }
    g_num_lines--;
}

static void load_file(const char* filename) {
    g_num_lines = 0;
    FILE* f = fopen(filename, "r");
    if (!f) {
        insert_line(0, "");
        set_status("New File");
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        insert_line(0, "");
        set_status("Empty File");
        return;
    }
    
    char* buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        insert_line(0, "");
        set_status("OOM reading file");
        return;
    }
    
    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = 0;
    fclose(f);
    
    char* p = buf;
    char* line_start = p;
    while (*p) {
        if (*p == '\n') {
            *p = 0;
            int len = p - line_start;
            if (len > 0 && line_start[len - 1] == '\r') {
                line_start[len - 1] = 0;
            }
            insert_line(g_num_lines, line_start);
            line_start = p + 1;
        }
        p++;
    }
    if (p > line_start) {
        insert_line(g_num_lines, line_start);
    }
    
    free(buf);
    
    if (g_num_lines == 0) {
        insert_line(0, "");
    }
    
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "Loaded %s (%d lines)", filename, g_num_lines);
    set_status(status_buf);
}

static void save_file(const char* filename) {
    if (!filename || !filename[0]) {
        set_status("Error: No filename");
        return;
    }
    FILE* f = fopen(filename, "w");
    if (!f) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Error saving %s", filename);
        set_status(buf);
        return;
    }
    for (int i = 0; i < g_num_lines; i++) {
        fputs(g_lines[i], f);
        fputs("\n", f);
    }
    fclose(f);
    char buf[128];
    snprintf(buf, sizeof(buf), "Saved %s", filename);
    set_status(buf);
}

static void draw_pixel(u32 x, u32 y, u32 color) {
    if (!g_fb_mapped) return;
    if (x >= g_fb.width || y >= g_fb.height) return;
    
    if (g_fb.bpp == 32) {
        u32* pixel = (u32*)((u8*)g_fb.fb_virt + y * g_fb.pitch + x * 4);
        *pixel = color;
    } else if (g_fb.bpp == 24) {
        u8* pixel = (u8*)g_fb.fb_virt + y * g_fb.pitch + x * 3;
        pixel[0] = color & 0xFF;
        pixel[1] = (color >> 8) & 0xFF;
        pixel[2] = (color >> 16) & 0xFF;
    }
}

static void draw_char(char c, u32 col, u32 row, u32 fg, u32 bg) {
    u8 ch = (u8)c;
    const u8* glyph = g_font8x16[ch];
    u32 start_x = col * 8;
    u32 start_y = row * 16;
    
    for (u32 y = 0; y < 16; y++) {
        u8 row_bits = glyph[y];
        for (u32 x = 0; x < 8; x++) {
            u32 color = (row_bits & (0x80 >> x)) ? fg : bg;
            draw_pixel(start_x + x, start_y + y, color);
        }
    }
}

static void draw_padded_string(const char* s, u32 row, u32 fg, u32 bg) {
    u32 col = 0;
    while (*s && col < (u32)g_screen_cols) {
        draw_char(*s++, col++, row, fg, bg);
    }
    while (col < (u32)g_screen_cols) {
        draw_char(' ', col++, row, fg, bg);
    }
}

static int is_in_selection(int x, int y) {
    if (!g_has_selection) return 0;
    
    int start_y = g_sel_start_y;
    int start_x = g_sel_start_x;
    int end_y = g_cursor_y;
    int end_x = g_cursor_x;
    
    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        int temp = start_y; start_y = end_y; end_y = temp;
        temp = start_x; start_x = end_x; end_x = temp;
    }
    
    if (y < start_y || y > end_y) return 0;
    if (y > start_y && y < end_y) return 1;
    
    if (start_y == end_y) {
        return (x >= start_x && x < end_x);
    }
    
    if (y == start_y) {
        return (x >= start_x);
    }
    if (y == end_y) {
        return (x < end_x);
    }
    
    return 0;
}

static void draw_editor_line(int line_idx, int screen_row) {
    if (line_idx < g_num_lines) {
        char* line = g_lines[line_idx];
        int len = strlen(line);
        int limit = g_screen_cols;
        
        for (int c = 0; c < limit; c++) {
            int char_idx = g_scroll_x + c;
            int is_cursor = (line_idx == g_cursor_y && char_idx == g_cursor_x);
            int is_selected = is_in_selection(char_idx, line_idx);
            
            u32 fg = FB_WHITE;
            u32 bg = FB_BLACK;
            
            if (is_cursor) {
                fg = FB_BLACK;
                bg = FB_WHITE;
            } else if (is_selected) {
                fg = 0x00FFFFFF;
                bg = 0x000055AA;
            }
            
            char ch = ' ';
            if (char_idx < len) {
                ch = line[char_idx];
            }
            draw_char(ch, c, screen_row, fg, bg);
        }
    } else {
        int is_cursor = (line_idx == g_cursor_y && 0 == g_cursor_x);
        u32 fg = is_cursor ? FB_BLACK : FB_WHITE;
        u32 bg = is_cursor ? FB_WHITE : FB_BLACK;
        draw_char(' ', 0, screen_row, fg, bg);
        for (int c = 1; c < g_screen_cols; c++) {
            draw_char(' ', c, screen_row, FB_WHITE, FB_BLACK);
        }
    }
}

static void update_scroll() {
    if (g_cursor_y < g_scroll_y) {
        g_scroll_y = g_cursor_y;
    }
    if (g_cursor_y >= g_scroll_y + g_screen_rows - 3) {
        g_scroll_y = g_cursor_y - (g_screen_rows - 3) + 1;
    }
    
    if (g_cursor_x < g_scroll_x) {
        g_scroll_x = g_cursor_x;
    }
    if (g_cursor_x >= g_scroll_x + g_screen_cols - 1) {
        g_scroll_x = g_cursor_x - (g_screen_cols - 1) + 1;
    }
}

static void draw_screen_full() {
    char title[128];
    snprintf(title, sizeof(title), "  Nano 1.0  |  File: %-20s", g_filename[0] ? g_filename : "New Buffer");
    draw_padded_string(title, 0, FB_BLACK, FB_WHITE);
    
    int text_rows = g_screen_rows - 3;
    for (int i = 0; i < text_rows; i++) {
        draw_editor_line(g_scroll_y + i, 1 + i);
    }
    
    if (g_mode == MODE_JUMP) {
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "Go to line: %s_", g_jump_buf);
        draw_padded_string(prompt, g_screen_rows - 2, FB_BLACK, FB_WHITE);
        draw_padded_string("Enter: Go    Esc: Cancel", g_screen_rows - 1, FB_WHITE, FB_BLACK);
    } else {
        draw_padded_string(g_status_msg, g_screen_rows - 2, FB_BLACK, FB_WHITE);
        draw_padded_string("^O WriteOut  ^S Save  ^K Cut  ^U Paste  ^X Exit  ^G GoToLine", g_screen_rows - 1, FB_WHITE, FB_BLACK);
    }
    
    g_prev_cursor_x = g_cursor_x;
    g_prev_cursor_y = g_cursor_y;
    g_prev_scroll_x = g_scroll_x;
    g_prev_scroll_y = g_scroll_y;
    g_prev_mode = g_mode;
    g_prev_num_lines = g_num_lines;
    g_prev_has_selection = g_has_selection;
}

static void draw_screen() {
    update_scroll();
    
    int must_redraw_all = (g_scroll_y != g_prev_scroll_y || g_scroll_x != g_prev_scroll_x || g_mode != g_prev_mode || g_num_lines != g_prev_num_lines || g_has_selection || g_prev_has_selection);
    
    if (must_redraw_all) {
        draw_screen_full();
        return;
    }
    
    if (g_cursor_y != g_prev_cursor_y) {
        draw_editor_line(g_prev_cursor_y, 1 + (g_prev_cursor_y - g_scroll_y));
        draw_editor_line(g_cursor_y, 1 + (g_cursor_y - g_scroll_y));
    } else if (g_cursor_x != g_prev_cursor_x) {
        draw_editor_line(g_cursor_y, 1 + (g_cursor_y - g_scroll_y));
    } else {
        draw_editor_line(g_cursor_y, 1 + (g_cursor_y - g_scroll_y));
    }
    
    draw_padded_string(g_status_msg, g_screen_rows - 2, FB_BLACK, FB_WHITE);
    
    g_prev_cursor_x = g_cursor_x;
    g_prev_cursor_y = g_cursor_y;
    g_prev_mode = g_mode;
    g_prev_num_lines = g_num_lines;
    g_prev_has_selection = g_has_selection;
}

static void drain_getchar() {
    for (int i = 0; i < 1000 && syscall0(SYS_GETCHAR) != 0; i++) {}
}

static void handle_backspace() {
    if (g_cursor_x > 0) {
        char* line = g_lines[g_cursor_y];
        int len = strlen(line);
        memmove(&line[g_cursor_x - 1], &line[g_cursor_x], len - g_cursor_x + 1);
        g_cursor_x--;
    } else if (g_cursor_y > 0) {
        char* cur_line = g_lines[g_cursor_y];
        char* prev_line = g_lines[g_cursor_y - 1];
        int prev_len = strlen(prev_line);
        int cur_len = strlen(cur_line);
        if (prev_len + cur_len < MAX_LINE_LEN - 1) {
            strcat(prev_line, cur_line);
            remove_line(g_cursor_y);
            g_cursor_y--;
            g_cursor_x = prev_len;
        }
    }
}

static void handle_delete() {
    char* line = g_lines[g_cursor_y];
    int len = strlen(line);
    if (g_cursor_x < len) {
        memmove(&line[g_cursor_x], &line[g_cursor_x + 1], len - g_cursor_x);
    } else if (g_cursor_y < g_num_lines - 1) {
        char* next_line = g_lines[g_cursor_y + 1];
        if (len + strlen(next_line) < MAX_LINE_LEN - 1) {
            strcat(line, next_line);
            remove_line(g_cursor_y + 1);
        }
    }
}

static void handle_enter() {
    char* line = g_lines[g_cursor_y];
    insert_line(g_cursor_y + 1, &line[g_cursor_x]);
    line[g_cursor_x] = 0;
    g_cursor_y++;
    g_cursor_x = 0;
}

static void check_selection_start() {
    if (g_shift_pressed) {
        if (!g_has_selection) {
            g_has_selection = 1;
            g_sel_start_x = g_cursor_x;
            g_sel_start_y = g_cursor_y;
        }
    } else {
        g_has_selection = 0;
    }
}

static char* local_strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    while (n-- > 0 && *src) {
        *d++ = *src++;
    }
    *d = 0;
    return dest;
}

static void cut_selection() {
    if (!g_has_selection) return;
    
    int start_y = g_sel_start_y;
    int start_x = g_sel_start_x;
    int end_y = g_cursor_y;
    int end_x = g_cursor_x;
    
    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        int temp = start_y; start_y = end_y; end_y = temp;
        temp = start_x; start_x = end_x; end_x = temp;
    }
    
    int total_len = 0;
    if (start_y == end_y) {
        total_len = end_x - start_x;
    } else {
        total_len += strlen(g_lines[start_y]) - start_x + 1;
        for (int y = start_y + 1; y < end_y; y++) {
            total_len += strlen(g_lines[y]) + 1;
        }
        total_len += end_x;
    }
    
    if (g_clipboard) free(g_clipboard);
    g_clipboard = malloc(total_len + 1);
    g_clipboard[0] = 0;
    g_clipboard_is_lines = 0;
    
    if (start_y == end_y) {
        local_strncat(g_clipboard, &g_lines[start_y][start_x], end_x - start_x);
    } else {
        strcat(g_clipboard, &g_lines[start_y][start_x]);
        strcat(g_clipboard, "\n");
        for (int y = start_y + 1; y < end_y; y++) {
            strcat(g_clipboard, g_lines[y]);
            strcat(g_clipboard, "\n");
        }
        local_strncat(g_clipboard, g_lines[end_y], end_x);
    }
    
    if (start_y == end_y) {
        char* line = g_lines[start_y];
        int len = strlen(line);
        memmove(&line[start_x], &line[end_x], len - end_x + 1);
    } else {
        char* start_line = g_lines[start_y];
        char* end_line = g_lines[end_y];
        start_line[start_x] = 0;
        local_strncat(start_line, &end_line[end_x], MAX_LINE_LEN - strlen(start_line) - 1);
        
        int lines_to_remove = end_y - start_y;
        for (int i = 0; i < lines_to_remove; i++) {
            remove_line(start_y + 1);
        }
    }
    
    g_cursor_y = start_y;
    g_cursor_x = start_x;
    g_target_x = g_cursor_x;
    g_has_selection = 0;
    set_status("Cut selection");
}

static void cut_current_line() {
    if (g_num_lines <= 0) return;
    
    if (g_clipboard) free(g_clipboard);
    g_clipboard = malloc(strlen(g_lines[g_cursor_y]) + 2);
    strcpy(g_clipboard, g_lines[g_cursor_y]);
    strcat(g_clipboard, "\n");
    g_clipboard_is_lines = 1;
    
    remove_line(g_cursor_y);
    if (g_num_lines == 0) {
        insert_line(0, "");
    }
    if (g_cursor_y >= g_num_lines) {
        g_cursor_y = g_num_lines - 1;
    }
    g_cursor_x = 0;
    g_target_x = g_cursor_x;
    set_status("Cut line");
}

static void paste_clipboard() {
    if (!g_clipboard) return;
    
    if (g_clipboard_is_lines) {
        int len = strlen(g_clipboard);
        char* temp = malloc(len + 1);
        strcpy(temp, g_clipboard);
        if (len > 0 && temp[len - 1] == '\n') temp[len - 1] = 0;
        
        insert_line(g_cursor_y, temp);
        free(temp);
        g_cursor_y++;
        g_cursor_x = 0;
        g_target_x = g_cursor_x;
        set_status("Pasted line");
    } else {
        char* p = g_clipboard;
        while (*p) {
            if (*p == '\n') {
                handle_enter();
            } else {
                char* line = g_lines[g_cursor_y];
                int len = strlen(line);
                if (len < MAX_LINE_LEN - 1) {
                    memmove(&line[g_cursor_x + 1], &line[g_cursor_x], len - g_cursor_x + 1);
                    line[g_cursor_x] = *p;
                    g_cursor_x++;
                }
            }
            p++;
        }
        g_target_x = g_cursor_x;
        set_status("Pasted selection");
    }
}

static void handle_ctrl_left() {
    char* line = g_lines[g_cursor_y];
    int x = g_cursor_x;
    if (x == 0) {
        if (g_cursor_y > 0) {
            g_cursor_y--;
            g_cursor_x = strlen(g_lines[g_cursor_y]);
        }
        return;
    }
    while (x > 0 && line[x - 1] == ' ') {
        x--;
    }
    while (x > 0 && line[x - 1] != ' ') {
        x--;
    }
    g_cursor_x = x;
}

static void handle_ctrl_right() {
    char* line = g_lines[g_cursor_y];
    int len = strlen(line);
    int x = g_cursor_x;
    if (x >= len) {
        if (g_cursor_y < g_num_lines - 1) {
            g_cursor_y++;
            g_cursor_x = 0;
        }
        return;
    }
    while (x < len && line[x] != ' ') {
        x++;
    }
    while (x < len && line[x] == ' ') {
        x++;
    }
    g_cursor_x = x;
}

static void handle_input() {
    KeyEvent ev;
    int redraw = 0;
    while (getkey(&ev)) {
        drain_getchar();
        
        u32 sc = ev.scancode;
        u8 raw = sc & 0xFF;
        
        if (!ev.pressed) {
            if (sc == 0x2A || sc == 0x36) g_shift_pressed = 0;
            if (sc == 0x1D || sc == (0x100 | 0x1D)) g_ctrl_pressed = 0;
            continue;
        }
        
        redraw = 1;
        
        // snprintf(g_status_msg, sizeof(g_status_msg), "Key: 0x%x Raw: 0x%x Ctrl: %d Shift: %d", sc, raw, g_ctrl_pressed, g_shift_pressed);
        
        if (sc == 0x2A || sc == 0x36) { g_shift_pressed = 1; continue; }
        if (sc == 0x1D || sc == (0x100 | 0x1D)) { g_ctrl_pressed = 1; continue; }
        
        if (g_mode == MODE_JUMP) {
            if (sc == 0x01) { // Esc
                g_mode = MODE_EDIT;
                set_status("");
            } else if (sc == 0x1C) { // Enter
                int line_num = atoi(g_jump_buf);
                if (line_num > 0) {
                    g_cursor_y = line_num - 1;
                    if (g_cursor_y >= g_num_lines) {
                        g_cursor_y = g_num_lines - 1;
                    }
                    g_cursor_x = 0;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Jumped to line %d", line_num);
                    set_status(msg);
                } else {
                    set_status("");
                }
                g_mode = MODE_EDIT;
            } else if (sc == 0x0E) { // Backspace
                if (g_jump_len > 0) {
                    g_jump_len--;
                    g_jump_buf[g_jump_len] = 0;
                }
            } else {
                if (raw < sizeof(scancode_to_ascii)) {
                    char c = g_shift_pressed ? scancode_shift_ascii[raw] : scancode_to_ascii[raw];
                    if (c >= '0' && c <= '9' && g_jump_len < (int)sizeof(g_jump_buf) - 1) {
                        g_jump_buf[g_jump_len++] = c;
                        g_jump_buf[g_jump_len] = 0;
                    }
                }
            }
            continue;
        }
        
        if (g_ctrl_pressed) {
            if (raw == 0x18 || raw == 0x1F) { // 'O' or 'S'
                save_file(g_filename);
            } else if (raw == 0x2D) { // 'X'
                clear();
                exit(0);
            } else if (raw == 0x22) { // 'G'
                g_mode = MODE_JUMP;
                g_jump_len = 0;
                g_jump_buf[0] = 0;
            } else if (raw == 0x25) { // 'K'
                if (g_has_selection) {
                    cut_selection();
                } else {
                    cut_current_line();
                }
            } else if (raw == 0x16) { // 'U'
                paste_clipboard();
            } else if (sc == 0x14B || sc == 0x4B) { // Ctrl+Left
                check_selection_start();
                handle_ctrl_left();
                g_target_x = g_cursor_x;
            } else if (sc == 0x14D || sc == 0x4D) { // Ctrl+Right
                check_selection_start();
                handle_ctrl_right();
                g_target_x = g_cursor_x;
            }
            continue;
        }
        
        if (sc == 0x148 || sc == 0x48) { // UP
            check_selection_start();
            if (g_cursor_y > 0) {
                g_cursor_y--;
                g_cursor_x = g_target_x;
                int len = strlen(g_lines[g_cursor_y]);
                if (g_cursor_x > len) g_cursor_x = len;
            }
        } else if (sc == 0x150 || sc == 0x50) { // DOWN
            check_selection_start();
            if (g_cursor_y < g_num_lines - 1) {
                g_cursor_y++;
                g_cursor_x = g_target_x;
                int len = strlen(g_lines[g_cursor_y]);
                if (g_cursor_x > len) g_cursor_x = len;
            }
        } else if (sc == 0x14B || sc == 0x4B) { // LEFT
            check_selection_start();
            if (g_cursor_x > 0) {
                g_cursor_x--;
            } else if (g_cursor_y > 0) {
                g_cursor_y--;
                g_cursor_x = strlen(g_lines[g_cursor_y]);
            }
        } else if (sc == 0x14D || sc == 0x4D) { // RIGHT
            check_selection_start();
            int len = strlen(g_lines[g_cursor_y]);
            if (g_cursor_x < len) {
                g_cursor_x++;
            } else if (g_cursor_y < g_num_lines - 1) {
                g_cursor_y++;
                g_cursor_x = 0;
            }
        } else if (sc == 0x153 || sc == 0x53) { // DEL
            g_has_selection = 0;
            handle_delete();
        } else if (sc == 0x0E) { // BACKSPACE
            g_has_selection = 0;
            handle_backspace();
        } else if (sc == 0x1C) { // ENTER
            g_has_selection = 0;
            handle_enter();
        } else {
            if (raw < sizeof(scancode_to_ascii)) {
                char c = g_shift_pressed ? scancode_shift_ascii[raw] : scancode_to_ascii[raw];
                if (c >= 32 && c < 127) {
                    char* line = g_lines[g_cursor_y];
                    int len = strlen(line);
                    if (len < MAX_LINE_LEN - 1) {
                        memmove(&line[g_cursor_x + 1], &line[g_cursor_x], len - g_cursor_x + 1);
                        line[g_cursor_x] = c;
                        g_cursor_x++;
                    }
                }
            }
        }
        
        if (sc != 0x148 && sc != 0x48 && sc != 0x150 && sc != 0x50) {
            g_target_x = g_cursor_x;
        }
    }
    
    if (redraw) draw_screen();
}

void main(const char* args, const char* cwd, i32 argc) {
    char argbuf[256];
    char* argv[16];
    i32 ac = 0;
    
    if (args && *args) {
        strncpy(argbuf, args, sizeof(argbuf) - 1);
        argbuf[sizeof(argbuf) - 1] = 0;
        ac = parse_args(argbuf, argv, 16);
    }
    
    KeyEvent dummy;
    int k_count = 0;
    while (getkey(&dummy)) {
        k_count++;
        if (k_count > 1000) break;
    }
    drain_getchar();
    
    if (fb_map(&g_fb) == 0) {
        g_fb_mapped = 1;
        g_screen_cols = g_fb.width / 8;
        g_screen_rows = g_fb.height / 16;
    }
    
    if (ac > 0) {
        strncpy(g_filename, argv[0], sizeof(g_filename) - 1);
        g_filename[sizeof(g_filename) - 1] = 0;
        load_file(g_filename);
    } else {
        strcpy(g_filename, "new.txt");
        insert_line(0, "");
        set_status("New File");
    }
    
    draw_screen_full();
    
    while (1) {
        handle_input();
        syscall1(SYS_SLEEP, 10);
    }
}
