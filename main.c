/*
 * Copyright 2022-2023 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-3.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

/*
 * This program presents a menu of installation ISOs that we can possibly
 * chain-boot to.  It uses JSON information obtained from SimpleStreams to
 * provide the list of ISOs with a friendly label.
 *
 * The menu is styled to have an appearance that is as close to Subiquity as
 * possible.
 *
 * Input is a file or files similar to
 * http://cdimage.ubuntu.com/streams/v1/com.ubuntu.cdimage.daily:ubuntu-server.json
 *
 * The chosen ISO is output in a format friendly for the /bin/sh source
 * built-in - sample output:
 *
 * MEDIA_URL="https://releases.ubuntu.com/kinetic/ubuntu-22.10-live-server-amd64.iso"
 * MEDIA_LABEL="Ubuntu Server 22.10 (Kinetic Kudu)"
 * MEDIA_SIZE="1642631168"
 */

#include "common.h"

#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdnoreturn.h>
#include <sys/param.h>

#include "args.h"
#include "json.h"

#define MENU_MAIN 0
#define MENU_SUBMENU 1
#define KEY_ESC 27

typedef struct {
    int menu_state;
    const char* current_content_id;
    bool continuing;
    int main_selected;
    int submenu_selected;
    int last_input;  // Add this field
} menu_state_t;

int ubuntu_orange = COLOR_RED;
int text_white = COLOR_WHITE;
int back_green = COLOR_GREEN;
int black_green = 3;

noreturn void usage(char *prog)
{
    fprintf(stderr,
            "usage: %s <output path> <input json> [<input json> ...]\n",
            prog);
    exit(1);
}

typedef enum {
    DECREASE=-1,
    SELECT=0,
    INCREASE=1,
} choice_event;

choices_t *read_iso_choices(args_t *args)
{
    int capacity = 50;  /* Allow for more ISO choices */
    choices_t *choices = choices_create(capacity);
    for(int i = 0; i < args->num_infiles; i++) {
        choices_extend_from_json(choices, args->infiles[i], ARCH);
    }
    return choices;
}

int horizontal_center(int len)
{
    // Use 90% of available width
    return (COLS - (int)(len * 1.5)) / 2;
}

int vertical_center(int len)
{
    /* accounts for 3 line banner, use more vertical space */
    return 3 + (LINES - 3 - (int)(len * 1.2)) / 2;
}

void orange_banner(char *label)
{
    int x1 = 0;
    int w = COLS;

    /* Simulate the banner from Subiquity.
     * - draw black on orange for the half-block rows
     * - draw white on orange for the text row */
    short black_orange = 1;
    short white_orange = 2;
    init_pair(black_orange, COLOR_BLACK, ubuntu_orange);
    init_pair(white_orange, text_white, ubuntu_orange);

    cchar_t half_block_upper;
    setcchar(&half_block_upper, L"\u2580", 0, black_orange, NULL);

    cchar_t space;
    setcchar(&space, L" ", 0, white_orange, NULL);

    cchar_t half_block_lower;
    setcchar(&half_block_lower, L"\u2584", 0, black_orange, NULL);

    mvhline_set(0, x1, &half_block_upper, w);
    mvhline_set(1, x1, &space, w);
    mvhline_set(2, x1, &half_block_lower, w);

    attron(COLOR_PAIR(white_orange));
    mvaddstr(1, horizontal_center(strlen(label)), label);
    attroff(COLOR_PAIR(white_orange));
}

void button(int y, int x, const char *label, int textwidth)
{
    char *button_text = saprintf("[ %-*s \u25b8 ]", textwidth, label);
    /* Simulate the appearance of buttons in Subiquity.  The unicode character
     * is the right-pointing smaller tringle arrow */
    mvaddstr(y, x, button_text);
    free(button_text);
}

#define VIEWPORT_SIZE (LINES - 5)  // Account for banner and borders

void add_chooser(choices_t *choices, int selected)
{
    short white_green = 3;
    init_pair(white_green, text_white, back_green);

    int longest = 0;
    for(int i = 0; i < choices->len; i++) {
        longest = MAX(longest, (int)strlen(choices->values[i]->label));
    }
    
    // Calculate viewport and scrolling
    int viewport_start = MAX(0, selected - VIEWPORT_SIZE/2);
    viewport_start = MIN(viewport_start, choices->len - VIEWPORT_SIZE);
    if (viewport_start < 0) viewport_start = 0;
    
    int viewport_end = MIN(viewport_start + VIEWPORT_SIZE, choices->len);
    
    /* The + 6 accounts for the button text around the label */
    int center_x = horizontal_center(longest + 6);
    int center_y = vertical_center(MIN(choices->len, VIEWPORT_SIZE));
    
    // Show scroll indicators if needed
    if (viewport_start > 0) {
        mvaddstr(center_y - 1, center_x + longest/2, "▲");
    }
    if (viewport_end < choices->len) {
        mvaddstr(center_y + MIN(choices->len, VIEWPORT_SIZE), center_x + longest/2, "▼");
    }
    
    // Draw visible items
    for(int i = viewport_start; i < viewport_end; i++) {
        int y = center_y + (i - viewport_start);
        if(i == selected) {
            attron(COLOR_PAIR(black_green));
        }
        button(y, center_x, choices->values[i]->label, longest);
        if(i == selected) {
            attroff(COLOR_PAIR(black_green));
        }
    }
}

void show_main_menu(choices_t *choices, int selected) {
    int longest = 0;
    for(int i = 0; i < content_id_count(); i++) {
        longest = MAX(longest, (int)strlen(content_id_to_criteria[i].descriptor));
    }
    
    int center_x = horizontal_center(longest + 6);
    int center_y = vertical_center(content_id_count());
    
    for(int i = 0; i < content_id_count(); i++) {
        if(i == selected) {
            attron(COLOR_PAIR(black_green));
        }
        button(center_y + i, center_x, content_id_to_criteria[i].descriptor, longest);
        if(i == selected) {
            attroff(COLOR_PAIR(black_green));
        }
    }
}

choices_t* get_submenu_choices(choices_t* all_choices, const char* content_id) {
    syslog(LOG_DEBUG, "get_submenu_choices: creating for content_id: %s", content_id);
    choices_t* filtered = choices_create(50);
    
    int matches = 0;
    for(int i = 0; i < all_choices->len; i++) {
        syslog(LOG_DEBUG, "Checking choice %d: content_id=%s", i, 
               all_choices->values[i]->content_id ? all_choices->values[i]->content_id : "NULL");
        if(all_choices->values[i]->content_id && 
           strcmp(all_choices->values[i]->content_id, content_id) == 0) {
            choices_append(filtered, all_choices->values[i]);
            matches++;
        }
    }
    syslog(LOG_DEBUG, "get_submenu_choices: found %d matches for %s", matches, content_id);
    
    // Sort filtered choices by label (which contains version info)
    for(int i = 0; i < filtered->len; i++) {
        for(int j = i + 1; j < filtered->len; j++) {
            if(strcmp(filtered->values[i]->label, filtered->values[j]->label) < 0) {
                iso_data_t* temp = filtered->values[i];
                filtered->values[i] = filtered->values[j];
                filtered->values[j] = temp;
            }
        }
    }
    return filtered;
}

int color_byte_to_ncurses(uint8_t color_byte)
{
    return color_byte / 255.0 * 1000;
}

void init_color_from_bytes(short color,
                           uint8_t byte_r, uint8_t byte_g, uint8_t byte_b)
{
    int nc_r = color_byte_to_ncurses(byte_r);
    int nc_g = color_byte_to_ncurses(byte_g);
    int nc_b = color_byte_to_ncurses(byte_b);
    init_color(color, nc_r, nc_g, nc_b);
}

void write_output(char *fname, iso_data_t *iso_data)
{
    FILE *f = fopen(fname, "w");
    if(!f) {
        syslog(LOG_ERR, "failed to open output file [%s]: %m", fname);
        exit(1);
    }

    fprintf(f, "MEDIA_URL=\"%s\"\n", iso_data->url);
    fprintf(f, "MEDIA_LABEL=\"%s\"\n", iso_data->label);
    fprintf(f, "MEDIA_256SUM=\"%s\"\n", iso_data->sha256sum);
    fprintf(f, "MEDIA_SIZE=\"%" PRId64 "\"\n", iso_data->size);
    fclose(f);
}

void choice_handle_event(args_t *args, choices_t *choices, choice_event evt)
{
    switch(evt) {
        case DECREASE:
            if(choices->cur > 0) {
                choices->cur--;
                clear();
                orange_banner("netboot.xyz - Choose an Ubuntu version to install");
                add_chooser(choices, choices->cur);
                refresh();
            }
            break;
        case SELECT:
            iso_data_t *cur = choices->values[choices->cur];
            write_output(args->outfile, cur);
            syslog(LOG_DEBUG, "selected:%s %s %" PRId64,
                   cur->label, cur->url, cur->size);
            break;
        case INCREASE:
            if(choices->cur < choices->len - 1) {
                choices->cur++;
                clear();
                orange_banner("netboot.xyz - Choose an Ubuntu version to install");
                add_chooser(choices, choices->cur);
                refresh();
            }
            break;
        default:
            syslog(LOG_ERR, "invalid event id [%d]", evt);
            exit(1);
    }
}

void exit_cb(void)
{
    erase();
    refresh();
    endwin();
}

void reset_submenu_state(menu_state_t *state, choices_t **submenu) {
    syslog(LOG_DEBUG, "Resetting submenu state. Current ptr: %p", (void*)*submenu);
    if (*submenu != NULL) {
        choices_free(*submenu);
        *submenu = NULL;
    }
    state->menu_state = MENU_MAIN;
    state->current_content_id = NULL;
    state->submenu_selected = 0;
    clear();
    refresh();
}

#define DEBUG_HISTORY_SIZE 4
char debug_history[DEBUG_HISTORY_SIZE][256] = {{0}};
int debug_history_index = 0;

void add_debug_message(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_history[debug_history_index], 256, fmt, args);
    va_end(args);
    debug_history_index = (debug_history_index + 1) % DEBUG_HISTORY_SIZE;
    syslog(LOG_DEBUG, "%s", debug_history[(DEBUG_HISTORY_SIZE + debug_history_index - 1) % DEBUG_HISTORY_SIZE]);
}

void show_debug_status(menu_state_t *state, choices_t *submenu) {
    int y = LINES - 8;  // Show more debug lines
    mvprintw(y, 0, "Menu: %s | Main: %d/%d | Sub: %d/%d | ID: %s", 
        state->menu_state == MENU_MAIN ? "MAIN" : "SUBMENU",
        state->main_selected,
        content_id_count() - 1,
        state->submenu_selected,
        submenu ? submenu->len - 1 : 0,
        state->current_content_id ? state->current_content_id : "NULL"
    );
    
    mvprintw(y + 1, 0, "Submenu: %s (len: %d) | Ptr: %p", 
        submenu ? "EXISTS" : "NULL",
        submenu ? submenu->len : 0,
        (void*)submenu
    );

    const char* content_id = state->main_selected < content_id_count() ? 
        content_id_to_criteria[state->main_selected].content_id : "INVALID";
    mvprintw(y + 2, 0, "Selected ContentID: %s | Valid: %s", 
        content_id,
        submenu && state->current_content_id && strcmp(state->current_content_id, content_id) == 0 ? "YES" : "NO"
    );

    mvprintw(y + 3, 0, "Last Input: 0x%x | Navigation: %s", 
        state->last_input,
        state->menu_state == MENU_SUBMENU && !submenu ? "INVALID STATE" : "OK"
    );

    // Show debug history
    mvprintw(y + 4, 0, "Debug History:");
    for (int i = 0; i < DEBUG_HISTORY_SIZE; i++) {
        int idx = (DEBUG_HISTORY_SIZE + debug_history_index - i - 1) % DEBUG_HISTORY_SIZE;
        if (debug_history[idx][0] != '\0') {
            mvprintw(y + 5 + i, 2, "%s", debug_history[idx]);
        }
    }
}

int main(int argc, char **argv)
{
    args_t *args = args_create(argc, argv);
    if(!args) usage(argv[0]);

    setlocale(LC_ALL, "C.UTF-8");

    choices_t *iso_info = read_iso_choices(args);
    if(!iso_info) {
        syslog(LOG_ERR, "failed to read JSON data");
        return 1;
    }

    if(!initscr()) {
        syslog(LOG_ERR, "initscr failure");
        return 1;
    }

    atexit(exit_cb);

    noecho();

    if(!has_colors()) {
        syslog(LOG_ERR, "has_colors failure");
        return 1;
    }

    if(start_color() == ERR) {
        syslog(LOG_ERR, "start_color failure");
        return 1;
    }

    keypad(stdscr, TRUE);

    cbreak();

    curs_set(0); /* hide */
    refresh();

    syslog(LOG_DEBUG, "can_change_color [%d]", can_change_color());
    if(can_change_color()) {
        init_color_from_bytes(ubuntu_orange, 0xE9, 0x54, 0x20);
        init_color_from_bytes(text_white, 0xFF, 0xFF, 0xFF);
        init_color_from_bytes(back_green, 0x0E, 0x84, 0x20);
    } else {
        /* These are terminal 256 color codes, see
         * https://www.ditig.com/256-colors-cheat-sheet for an example. */
        ubuntu_orange = 202;  /* not really but kinda close */
        text_white = 231;
        back_green = 28;
    }

    init_pair(black_green, COLOR_BLACK, back_green);

    menu_state_t state = {
        .menu_state = MENU_MAIN,
        .current_content_id = NULL,
        .continuing = true,
        .main_selected = 0,
        .submenu_selected = 0
    };
    
    choices_t* submenu = NULL;

    while(state.continuing) {
        clear();
        orange_banner("Choose an Ubuntu version to install");

        if(state.menu_state == MENU_MAIN) {
            show_main_menu(iso_info, state.main_selected);
        } else if(state.menu_state == MENU_SUBMENU && submenu != NULL) {
            add_chooser(submenu, state.submenu_selected);
        }

        show_debug_status(&state, submenu);

        int ch = getch();
        state.last_input = ch;
        syslog(LOG_DEBUG, "Processing input: 0x%x in state: %s", ch, 
               state.menu_state == MENU_MAIN ? "MAIN" : "SUBMENU");

        switch(ch) {
            case 27:  // ESC key
            case 'q':
            case 'Q':
                if(state.menu_state == MENU_SUBMENU) {
                    reset_submenu_state(&state, &submenu);
                } else {
                    state.continuing = false;
                }
                break;

            case KEY_DOWN:
            case 'j':
                if(state.menu_state == MENU_MAIN) {
                    if(state.main_selected < content_id_count() - 1) 
                        state.main_selected++;
                } else if(state.menu_state == MENU_SUBMENU && submenu != NULL) {
                    if(state.submenu_selected < submenu->len - 1) {
                        state.submenu_selected++;
                        submenu->cur = state.submenu_selected;
                    }
                }
                break;

            case KEY_UP:
            case 'k':
                if(state.menu_state == MENU_MAIN) {
                    if(state.main_selected > 0) 
                        state.main_selected--;
                } else if(state.menu_state == MENU_SUBMENU && submenu != NULL) {
                    if(state.submenu_selected > 0) {
                        state.submenu_selected--;
                        submenu->cur = state.submenu_selected;
                    }
                }
                break;

            case KEY_ENTER:
            case KEY_RIGHT:
            case '\r':
            case '\n':
            case ' ':
                if(state.menu_state == MENU_MAIN) {
                    const char* selected_content_id = content_id_to_criteria[state.main_selected].content_id;
                    add_debug_message("Enter pressed in main menu - creating submenu for %s", selected_content_id);
                    
                    choices_t* new_submenu = get_submenu_choices(iso_info, selected_content_id);
                    add_debug_message("get_submenu_choices returned: ptr=%p, len=%d", 
                           (void*)new_submenu, new_submenu ? new_submenu->len : -1);
                    
                    if (new_submenu && new_submenu->len > 0) {
                        add_debug_message("Valid submenu created with %d items", new_submenu->len);
                        if (submenu != NULL) {
                            add_debug_message("Cleaning up old submenu: %p", (void*)submenu);
                            choices_free(submenu);
                        }
                        submenu = new_submenu;
                        state.menu_state = MENU_SUBMENU;
                        state.current_content_id = selected_content_id;
                        state.submenu_selected = 0;
                        submenu->cur = 0;
                    } else {
                        add_debug_message("Submenu creation failed or empty");
                        if (new_submenu) {
                            choices_free(new_submenu);
                        }
                    }
                } else if(state.menu_state == MENU_SUBMENU && submenu != NULL) {
                    choice_handle_event(args, submenu, SELECT);
                    state.continuing = false;
                }
                break;
        }
        refresh();
    }

    if (submenu != NULL) {
        choices_free(submenu);
    }

    choices_free(iso_info);
    args_free(args);

    return 0;
}
