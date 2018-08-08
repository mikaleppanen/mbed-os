/*
 * Copyright (c) 2018, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MBED_CONF_APP_CONNECT_STATEMENT
#error [NOT_SUPPORTED] No network configuration found for this target.
#endif

#include "mbed.h"
#include MBED_CONF_APP_HEADER_FILE
#include "greentea-client/test_env.h"
#include "unity/unity.h"
#include "utest.h"
#include "utest/utest_stack_trace.h"

#include "mbed_trace.h"
#include "EthernetInterface.h"
#include "SDBlockDevice.h"
#include "FATFileSystem.h"

using namespace utest::v1;

namespace
{
   OdinWiFiInterface *net;
   EthernetInterface *eth_net;

   static SDBlockDevice sd(MBED_CONF_SD_SPI_MOSI, MBED_CONF_SD_SPI_MISO, MBED_CONF_SD_SPI_CLK, MBED_CONF_SD_SPI_CS, 5000000);

   // File system and file declaration
   static FATFileSystem fs("fs");
   static FILE *f;

   // Stop tracing after the end of use case
   static bool stop_tracing = false;

   // Mutex for IP hex dump print and file system writing
   static rtos::Mutex hex_dump;
   // Thread for file system writing, needs high priority to empty xprintf buffer fast enough
   static rtos::Thread output_to_file_thread(osPriorityRealtime);
   // Triggers file system thread
   static rtos::Semaphore output_to_file_semaphore;

   // Current index of the log file
   static int file_name_index = 0;
   // Log file name (name + index)
   static char log_file_name[100];
   // Log file lines
   static int total_line = 0;
}

void output_to_file(void);

static void START_ACCESS_POINT() {

    int err;

    // Try to mount the filesystem
    printf("Mounting the filesystem... ");
    fflush(stdout);
    err = fs.mount(&sd);
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        // Reformat if we can't mount the filesystem
        // this should only happen on the first boot
        printf("No filesystem found, formatting... ");
        fflush(stdout);
        err = fs.reformat(&sd);
        printf("%s\n", (err ? "Fail :(" : "OK"));
        if (err) {
            error("error: %s (%d)\n", strerror(-err), err);
        }
    }

    // Open the log index file
    printf("Opening \"/fs/log_index.txt\"... ");
    fflush(stdout);
    f = fopen("/fs/log_index.txt", "r+");
    printf("%s\n", (!f ? "Fail :(" : "OK"));
    if (!f) {
        // Create the log index file if it doesn't exist
        printf("No file found, creating a new file... ");
        fflush(stdout);
        f = fopen("/fs/log_index.txt", "w+");
        printf("%s\n", (!f ? "Fail :(" : "OK"));
        if (!f) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }
    } else {
        // Read the log index file index
        fscanf(f, "%d", &file_name_index);
        rewind(f);
    }

    // Write the new index to file
    file_name_index++;
    fprintf(f, "%d", file_name_index);

    // Close the file which also flushes any cached writes
    printf("Closing \"/fs/log_index.txt\"... ");
    fflush(stdout);
    err = fclose(f);
    printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
    if (err < 0) {
        error("error: %s (%d)\n", strerror(errno), -errno);
    }

    // Create log file name
    sprintf(log_file_name, "/fs/log_%d.txt", file_name_index);

    // Open the log file
    printf("Opening \"%s\"... ", log_file_name);
    fflush(stdout);
    //f = fopen("/fs/log.txt", "r+");
    f = fopen(log_file_name, "w+");
    printf("%s\n", (!f ? "Fail :(" : "OK"));
    if (!f) {
        // Create the numbers file if it doesn't exist
        printf("No file found, creating a new file... ");
        fflush(stdout);
        f = fopen(log_file_name, "w+");
        printf("%s\n", (!f ? "Fail :(" : "OK"));
        if (!f) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }
    }

    wait(1.0);

    // Start thread for file system writing
    output_to_file_thread.start(output_to_file);

    // Connect ethernet interface
    eth_net = new EthernetInterface();
    err = eth_net->connect();

    TEST_ASSERT_EQUAL(NSAPI_ERROR_OK, err);
    printf("Ethernet IP address is '%s'\n", eth_net->get_ip_address());

    net = MBED_CONF_APP_OBJECT_CONSTRUCTION;

    err = net->set_ap_dhcp(false);
    TEST_ASSERT_EQUAL(NSAPI_ERROR_OK, err);

    err = net->set_ap_network("1.1.1.1", eth_net->get_netmask(), eth_net->get_gateway());
    TEST_ASSERT_EQUAL(NSAPI_ERROR_OK, err);

    err =  MBED_CONF_APP_CONNECT_STATEMENT;
    TEST_ASSERT_EQUAL(NSAPI_ERROR_OK, err);

    // Defined how long to trace to file
    wait(120.0);

    // Stop tracing to file
    stop_tracing = true;
    wait(1.0);

    // Close the file which also flushes any cached writes
    printf("Closing \"%s\"... ", log_file_name);
    fflush(stdout);
    err = fclose(f);
    printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
    if (err < 0) {
        error("error: %s (%d)\n", strerror(errno), -errno);
    }

    // Display the log file (if it is not too long...)
    if (total_line < 5000) {
        printf("Opening \"%s\"... ", log_file_name);
        fflush(stdout);
        f = fopen(log_file_name, "r");
        printf("%s\n", (!f ? "Fail :(" : "OK"));
        if (!f) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }

        printf("file (line %i):\n", total_line - 1);

        char line[300];
        int index = 0;
        int expected_line_number = 0;

        // Prints the log file to display and validates it
        while (!feof(f)) {
           int c = fgetc(f);
           line[index] = c;
           if (c == '\n') {
               line[index + 1] = '\0';

               // Validates that line numbers are in sequence
               int line_number;
               sscanf(line, "%d", &line_number);
               if (line_number != expected_line_number) {
                   TEST_ASSERT(0); // Line lost
               }
               expected_line_number++;

               printf("%s", line);
               index = 0;
           } else {
               index++;
           }
        }

        // Total lines must match
        if (expected_line_number != total_line) {
            TEST_ASSERT(0);
        }

        printf("\rClosing \"%s\"... ", log_file_name);
        fflush(stdout);
        err = fclose(f);
        printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
        if (err < 0) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }
    }

    // Tidy up
    printf("Unmounting... ");
    fflush(stdout);
    err = fs.unmount();
    printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
    if (err < 0) {
        error("error: %s (%d)\n", strerror(-err), err);
    }

    wait(20000.0);
}

// Test setup
utest::v1::status_t greentea_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(21000, "default_auto");
    return greentea_test_setup_handler(number_of_cases);
}

void greentea_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    return greentea_test_teardown_handler(passed, failed, failure);
}

Case cases[] = {
        Case("START_ACCESS_POINT", START_ACCESS_POINT),
};

Specification specification(greentea_setup, cases, greentea_teardown);

int main()
{
    return !Harness::run(specification);
}

// Print traces first to xprintf buffer
#define BUFFER_SIZE 25000 /* You need to choose a suitable value here. */
#define HALF_BUFFER_SIZE 20000 //(BUFFER_SIZE >> 1)

/* Here's one way of allocating the ring buffer. */
char ringBuffer[BUFFER_SIZE];
char *ringBufferStart = ringBuffer;
char *ringBufferTail  = ringBuffer;

void xprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    size_t largestWritePossible =
            BUFFER_SIZE - (ringBufferTail - ringBufferStart);
    int written =
            vsnprintf(ringBufferTail, largestWritePossible, format, args);
    va_end(args);

    if (written < 0) {
        /* do some error handling */
        return;
    }

    /*
    * vsnprintf() doesn't write more than 'largestWritePossible' bytes to the
    * ring buffer (including the terminating null byte '\0'). If the output is
    * truncated due to this limit, then the return value ('written') is the
    * number of characters (excluding the terminating null byte) which would
    * have been written to the final string if enough space had been available.
    */

    if (written > largestWritePossible) {
        /* There are no easy solutions to tackle this. It
        * may be easiest to enlarge
        * your BUFFER_SIZE to avoid this. */
        return; /* this is a short-cut; you may want to do something else.*/
    }

    ringBufferTail += written;

    /* Is it time to wrap around? */
    if (ringBufferTail > (ringBufferStart  + HALF_BUFFER_SIZE)) {

        // Does not drain the buffer fast enough. FATAL ERROR
        TEST_ASSERT(0);

        size_t overflow =
            ringBufferTail - (ringBufferStart + HALF_BUFFER_SIZE);
            memmove(ringBufferStart, ringBufferStart
                     + HALF_BUFFER_SIZE, overflow);
        ringBufferTail =
                    ringBufferStart + overflow;
    }
}

void xprintf_print(void)
{
    printf("LOG T:\n%s", ringBufferTail + 1);
    printf("LOG S:\n%s", ringBufferStart);

}

// Prefix e.g. "INP>", length is the frame length, data is the frame data
void trace_to_ascii_hex_dump(const char *prefix, int len, unsigned char *data)
{
    if (stop_tracing) {
        return;
    }

    hex_dump.lock();

    int line_len = 0;

    for (int i = 0; i < len; i++) {
       //if ((line_len % 14) == 0) {
       if ((line_len % 28) == 0) {
           if (line_len != 0) {
               xprintf("\n");
           }
           xprintf("%i %s %06x", total_line++, prefix, line_len);
       }
       line_len++;
       xprintf(" %02x", data[i]);
    }
    xprintf("\n");

    hex_dump.unlock();

    output_to_file_semaphore.release();

}

void output_to_file(void) {

    while (true) {

        output_to_file_semaphore.wait();

        if (stop_tracing) {
            return;
        }

        hex_dump.lock();

        if (ringBufferStart[0]) {
            fprintf(f, "%s", ringBufferStart);
        }

        ringBufferStart = ringBuffer;
        ringBufferStart[0] = 0;
        ringBufferTail  = ringBuffer;

        hex_dump.unlock();

        if (stop_tracing) {
            return;
        }
    }
}

