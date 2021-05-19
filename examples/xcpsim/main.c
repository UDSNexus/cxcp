/*
 * BlueParrot XCP
 *
 * (C) 2007-2021 by Christoph Schueler <github.com/Christoph2,
 *                                      cpu12.gems@googlemail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * s. FLOSS-EXCEPTION.txt
 */

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>

#include <pthread.h>

#include "xcp.h"
#include "terminal.h"
#include "app_config.h"

//////////////


pthread_t threads[NUM_THREADS];

Xcp_OptionsType Xcp_Options = {0};

//////////////


int main(int argc, char **argv)
{
//    size_t idx;
//    int res;


//    XcpHw_ParseCommandLineOptions(argc, argv, &options);
//    XcpTl_SetOptions(&options);

    FlsEmu_Init(&FlsEmu_Config);

    Xcp_Init();
    Xcp_DisplayInfo();

    pthread_create(&threads[UI_THREAD], NULL, &XcpTerm_Thread, NULL);

    pthread_join(threads[UI_THREAD], NULL);


    FlsEmu_DeInit();
    XcpHw_Deinit();
    XcpTl_DeInit();

    return 0;
}
