/* Resource and command IDs shared by tachyon.rc and the C code. */
#ifndef TW_RESOURCE_H
#define TW_RESOURCE_H

#define IDI_TACHYON         1
#define IDR_MAINMENU        100
#define IDR_ACCEL           101

/* Menu commands. Toolbar buttons send the same IDs. */
#define IDM_FILE_OPEN       1001
#define IDM_FILE_EXIT       1002
#define IDM_EDIT_CUT        1101
#define IDM_EDIT_COPY       1102
#define IDM_EDIT_PASTE      1103
#define IDM_EDIT_SELECTALL  1104
#define IDM_VIEW_STOP       1201
#define IDM_VIEW_RELOAD     1202
#define IDM_VIEW_PAGE       1203
#define IDM_VIEW_TREE       1204
#define IDM_VIEW_SOURCE     1205
#define IDM_GO_BACK         1301
#define IDM_GO_FORWARD      1302
#define IDM_GO_HOME         1303
#define IDM_GO_ADDRESS      1304
#define IDM_HELP_ABOUT      1401
#define IDM_HELP_LOG        1402

/* Child windows. */
#define IDC_ADDRESS_LABEL   2001
#define IDC_ADDRESS         2002
#define IDC_VIEW            2003
#define IDC_STATUS          2004

#endif
