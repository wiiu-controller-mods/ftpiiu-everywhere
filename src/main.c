#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "fs/fs_utils.h"
#include "fs/sd_fat_devoptab.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"
#include "ftp.h"
#include "virtualpath.h"
#include "net.h"

#define PORT                    21
#define MAX_CONSOLE_LINES       18

static char * consoleArray[MAX_CONSOLE_LINES];

void console_printf(const char *format, ...)
{
	char * tmp = NULL;

	va_list va;
	va_start(va, format);
	if((vasprintf(&tmp, format, va) >= 0) && tmp)
	{
	    if(consoleArray[0])
            free(consoleArray[0]);

        for(int i = 1; i < MAX_CONSOLE_LINES; i++)
            consoleArray[i-1] = consoleArray[i];

        if(strlen(tmp) > 79)
            tmp[79] = 0;

        consoleArray[MAX_CONSOLE_LINES-1] = (tmp);
	}
	va_end(va);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);


	for(int i = 0; i < MAX_CONSOLE_LINES; i++)
    {
        if(consoleArray[i])
        {
            OSScreenPutFontEx(0, 0, i, consoleArray[i]);
            OSScreenPutFontEx(1, 0, i, consoleArray[i]);
        }
    }
	OSScreenFlipBuffersEx(0);
	OSScreenFlipBuffersEx(1);
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitSocketFunctionPointers();

    log_init("192.168.178.3");
    log_print("Starting launcher\n");

    InitFSFunctionPointers();
    InitVPadFunctionPointers();

    log_print("Function exports loaded\n");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    log_print("Initialize memory management\n");
    //! We don't need bucket and MEM1 memory so no need to initialize
    //memoryInitialize();

    //!*******************************************************************
    //!                        Initialize FS                             *
    //!*******************************************************************
    log_printf("Mount SD partition\n");
    mount_sd_fat("sd");

    for(int i = 0; i < MAX_CONSOLE_LINES; i++)
    {
        consoleArray[i] = NULL;
    }

    VPADInit();

    // Prepare screen
    int screen_buf0_size = 0;
    int screen_buf1_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    screen_buf1_size = OSScreenGetBufferSizeEx(1);
    OSScreenSetBufferEx(0, (void *)0xF4000000);
    OSScreenSetBufferEx(1, (void *)(0xF4000000 + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);

    // Flush the cache
    DCFlushRange((void *)0xF4000000, screen_buf0_size);
    DCFlushRange((void *)(0xF4000000 + screen_buf0_size), screen_buf1_size);

    // Flip buffers
    OSScreenFlipBuffersEx(0);
    OSScreenFlipBuffersEx(1);

    console_printf("FTPiiU listening on %u.%u.%u.%u:%i", (network_gethostip() >> 24) & 0xFF, (network_gethostip() >> 16) & 0xFF, (network_gethostip() >> 8) & 0xFF, (network_gethostip() >> 0) & 0xFF, PORT);

	MountVirtualDevices();

    int serverSocket = create_server(PORT);

	int network_down = 0;
    int vpadError = -1;
    VPADData vpad;
    int vpadReadCounter = 0;

    while(serverSocket >= 0 && !network_down)
    {
        network_down = process_ftp_events(serverSocket);
        if(network_down)
        {
            break;
        }

        //! update only at 50 Hz, thats more than enough
        if(++vpadReadCounter >= 20)
        {
            vpadReadCounter = 0;

            VPADRead(0, &vpad, 1, &vpadError);

            if(vpadError == 0 && ((vpad.btns_d | vpad.btns_h) & VPAD_BUTTON_HOME))
                break;
        }

		usleep(1000);
    }

	cleanup_ftp();
	if(serverSocket >= 0)
        network_close(serverSocket);
	UnmountVirtualPaths();

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************

    log_printf("Unmount SD\n");
    unmount_sd_fat("sd");
    log_printf("Release memory\n");
    //memoryRelease();
    log_deinit();

    return EXIT_SUCCESS;
}

